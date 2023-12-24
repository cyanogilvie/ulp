#include "int.h"

thrd_t				io_thread_id;
int					io_thread_wakeup_fd = -1;
int					io_thread_epollfd = -1;
static once_flag	autoinit = ONCE_FLAG_INIT;
int					g_io_thread_running = 1;
int					g_io_thread_started = 0;

ulp_err ulp_init_msg_queue(struct ulp_msg_queue* q) //<<<
{
	ulp_err		err = {NULL, ULP_OK};

	ulp_dlist_init(&q->msgs);
	if (mtx_init(&q->mutex, mtx_plain|mtx_recursive) != thrd_success)
		THROW_ERR(finally, err, "Could not initialize msg_queue mutex");

	if (cnd_init(&q->msg_avail) != thrd_success)
		THROW_ERR(finally, err, "Could not initialize msg_queue condition");

finally:
	return err;
}

//>>>
ulp_err ulp_deinit_msg_queue(struct ulp_msg_queue* q) //<<<
{
	ulp_err			err = {NULL, ULP_OK};

	if (thrd_success != mtx_lock(&q->mutex)) THROW_ERR(finally, err, "mtx_lock");
	// iterate over msgs and destroy them
	for (;;) {
		struct ulp_msg*	msg;

		err = ulp_msg_queue_get(q, .msg=&msg, .timeout=-1);
		switch (err.code) {
			case ULP_OK:
				ulp_msg_free(msg);
				free(msg);
				break;

			case ULP_ERR_QUEUE_EMPTY:
				goto unlock;

			default:
				goto unlock;
		}
	}
unlock:
	if (thrd_success != mtx_unlock(&q->mutex)) THROW_ERR(finally, err, "mtx_unlock");

finally:
	cnd_destroy(&q->msg_avail);
	mtx_destroy(&q->mutex);

	return err;
}

//>>>
int io_thread(void* data) //<<<
{
	int					io_thread_ready = *(int*)data;
	const int			max_events = 100;
	struct epoll_event	events[max_events];
	struct epoll_event	ev;

	io_thread_epollfd = epoll_create1(0);
	if (io_thread_epollfd == -1) {
		perror("epoll_create1");
		goto finally;
	}

	io_thread_wakeup_fd = eventfd(0, EFD_CLOEXEC);
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = NULL;
	epoll_ctl(io_thread_epollfd, EPOLL_CTL_ADD, io_thread_wakeup_fd, &ev);

	const int wrote = write(io_thread_ready, &(uint64_t){1}, 8);
	if (wrote == -1) {
		perror("write signalling io_thread_ready");
		goto finally;
	}

	while (g_io_thread_running) {
		const int nfds = epoll_wait(io_thread_epollfd, events, max_events, -1);
		if (-1 == nfds) {
			if (errno == EINTR) continue;
			perror("epoll_wait");
			goto finally;
		}

		for (int i=0; i<nfds; i++) {
			struct io_ready_data*	iocb = events[i].data.ptr;
			if (iocb && iocb->cb) iocb->cb(iocb->cx, events[i].events);
		}
	}

finally:
	ulp_obstack_pool_shutdown();
	DBG("Leaving io_thread\n");
	thrd_exit(EXIT_SUCCESS);
}

//>>>
void io_thread_register_con(struct ulp_con* c) //<<<
{
	struct epoll_event	ev;
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	ev.data.ptr = &c->io_ready_data;
	epoll_ctl(io_thread_epollfd, EPOLL_CTL_ADD, c->fd, &ev);
	c->epollfd = io_thread_epollfd;
	DBG("Registered con fd %d with io_thread_epollfd\n", c->fd);
}

//>>>
ulp_err ulp_close_con(struct ulp_con* c) //<<<
{
	ulp_err		err = {NULL, ULP_OK};

	if (c->out.iovcnt && !c->closing) {
		if (-1 == shutdown(c->fd, SHUT_RD))
			THROW_POSIX(finally, err, "shutdown con fd, read side");

		/* Flag this connection as closing, write_con will complete the close once the queued output has been drained */
		c->closing = 1;
	} else {
		if (c->epollfd != -1) {
			epoll_ctl(c->epollfd, EPOLL_CTL_DEL, c->fd, NULL);
			c->epollfd = -1;
		}

		if (-1 == shutdown(c->fd, SHUT_RDWR))
			THROW_POSIX(finally, err, "shutdown con fd");

		if (-1 == close(c->fd))
			THROW_POSIX(finally, err, "close con fd");

		c->fd = -1;

		ulp_rc_decref(c);
	}

finally:
	return err;
}

//>>>
int read_con(struct ulp_con* c) //<<<
{
	int		closed = 0;

	for (;;) {
		struct ulp_input *const	in = &c->in;
		ssize_t					shift = in->buf - in->tok;
		size_t					free = in->buf_size - (in->lim - in->tok) -1;

		if (free < 1) {
			/* Input too long for receive buffer */
			DBG("Input too long for receive buffer\n");
			closed = c->out.iovcnt == 0;
			ulp_close_con(c);
			goto finally;
		}

		if (shift) {
			memmove(in->buf, in->tok, in->buf_size + shift);
			in->lim += shift;
			in->cur += shift;
			in->mar += shift;
			in->tok += shift;
			if (in->shift_tags)
				in->shift_tags(in, shift);
		}

		const size_t	avail = in->buf_size - (in->lim - in->buf);
		const ssize_t	got = recv(c->fd, in->lim, avail, MSG_DONTWAIT);

		if (got == -1) {
			switch (errno) {
#if EAGAIN != EWOULDBLOCK
				case EWOULDBLOCK:
#endif
				case EAGAIN:
					/*
					if (in->cur < in->lim)
						goto parse; // May not have read any more, but we have some already waiting
					*/

					if (c->epollfd == -1)
						io_thread_register_con(c);
					goto finally;
				default:
					perror("Error reading from con fd");
					closed = c->out.iovcnt == 0;
					ulp_close_con(c);
					goto finally;
			}
		} else if (got == 0) {
			printf("Connection socket closed\n");
			closed = c->out.iovcnt == 0;
			c->eof = 1;
			//ulp_close_con(c);
			//goto finally;
		}

		in->lim += got;
		in->lim[0] = 0;	// Append sentinel

		enum ulp_parser_status	status = c->parser(c, &c->in, c->cdata);

		switch (status) {
			case ULP_PARSER_STATUS_WAITING:
				if (c->eof) goto finally;
				break;

			case ULP_PARSER_STATUS_OVERFLOW:
			case ULP_PARSER_STATUS_ERROR:
				DBG("Parse error\n");
				closed = c->out.iovcnt == 0;
				ulp_close_con(c);
				goto finally;

			case ULP_PARSER_STATUS_CLOSE:
				closed = c->out.iovcnt == 0;
				ulp_close_con(c);
				goto finally;

			case ULP_PARSER_STATUS_DONE:
				goto finally;

			default:
				break;
		}
	}

finally:
	return closed;
}

//>>>
int write_con(struct ulp_con* c) //<<<
{
	int				rc = 0;
	struct output*	out = &c->out;
	int				closed = 0;
	int				flags = MSG_NOSIGNAL | MSG_DONTWAIT;

	if (out->iovcnt == 0) return rc;

	if (thrd_success != mtx_lock(&out->mutex)) {
		// TODO: what?
		fprintf(stderr, "ulp_send could not aquire the output mutex\n");
		rc=1;
		goto finally;
	}

	/*
	size_t queuedbytes = 0;
	for (int i=0; i<out->iovcnt && queuedbytes < 10000; i++)
		queuedbytes += out->iov[i].iov_len;
	if (queuedbytes >= 10000)
		flags |= MSG_ZEROCOPY;

	// TODO: arrange for releasers to be stashed until we get SO_EE_ORIGIN_ZEROCOPY notifications:
	//	https://www.kernel.org/doc/html/v4.17/networking/msg_zerocopy.html
	*/

	const ssize_t sentbytes = sendmsg(c->fd, &(struct msghdr){
		.msg_iov		= out->iov,
		.msg_iovlen		= out->iovcnt
	}, flags);

again:
	if (sentbytes < 0) {
		switch (errno) {
			case EINTR: goto again;

#if EAGAIN != EWOULDBLOCK
			case EWOULDBLOCK:
#endif
			case EAGAIN:
				if (c->epollfd == -1)
					io_thread_register_con(c);
				goto finally;

			default:
				perror("sendmsg");

				for (int i=0; i<out->iovcnt; i++)
					if (out->iov_release[i].release)
						(out->iov_release[i].release)(out->iov_release[i].cdata);
				out->iovcnt = 0;

				ulp_close_con(c);
				closed = 1;
				rc = 1;
				goto finally;
		}
	}

	size_t remain = sentbytes;
	int i = 0;
	while (remain && remain >= out->iov[i].iov_len) {
		remain -= out->iov[i].iov_len;
		if (out->iov_release[i].release)
			(out->iov_release[i].release)(out->iov_release[i].cdata);
		i++;
	}
	if (remain) {
		out->iov[i].iov_len -= remain;
		out->iov[i].iov_base = (char*)out->iov[i].iov_base + remain;
	}
	if (i) {
		if (i < out->iovcnt) {
			memmove(out->iov, out->iov+i, sizeof(struct iovec)*(out->iovcnt-i));
			out->iov_base += i;
			out->iovcnt -= i;
		} else {
			out->iovcnt = 0;
		}
	}

	if (out->iovcnt) {
		if (c->epollfd == -1)
			io_thread_register_con(c);
	} else if (c->closing) {
		// Queued output has drained, close
		ulp_close_con(c);
		closed = 1;
		goto finally;
	}

finally:
	if (thrd_success != mtx_unlock(&out->mutex)) {
		// TODO: what?
		fprintf(stderr, "ulp_send could not release the output mutex\n");
		rc=1;
	}

	if (rc) {
		// TODO: something
	}

	return closed;
}

//>>>
ulp_err ulp_con_base(struct ulp_con* c, size_t* base) //<<<
{
	ulp_err			err = {NULL, ULP_OK};
	struct output*	out = &c->out;

	if (thrd_success != mtx_lock(&out->mutex)) THROW_ERR(finally, err, "Could not lock output mutex");
	*base = out->iov_base;
	if (thrd_success != mtx_unlock(&out->mutex)) THROW_ERR(finally, err, "Could not unlock output mutex");

finally:
	return err;
}

//>>>
ulp_err ulp_con_discard_pending(struct ulp_con* c, size_t rewind_to_base) //<<<
{
	ulp_err		err = {NULL, ULP_OK};
	struct output*	out = &c->out;

	if (thrd_success != mtx_lock(&out->mutex)) THROW_ERR(finally, err, "Could not lock output mutex");
	if (out->iov_base > rewind_to_base)
		THROW_ERR(unlock, err, "Cannot rewind past iov_base, that ship has sailed");

	for (int i=rewind_to_base-out->iov_base; i<out->iovcnt; i++)
		if (out->iov_release[i].release)
			(out->iov_release[i].release)(out->iov_release[i].cdata);
	out->iovcnt -= rewind_to_base-out->iov_base;

unlock:
	if (thrd_success != mtx_unlock(&out->mutex)) THROW_ERR(finally, err, "Could not unlock output mutex");

finally:
	return err;
}

//>>>
void io_ready(void* con_, uint32_t events) //<<<
{
	struct ulp_con*	c = con_;

	if (events & EPOLLIN && !c->closing)
		if (read_con(c))
			return;			// Socket was closed, skip write step

	if (events & EPOLLOUT)
		write_con(c);
}

//>>>
void free_con(void* data) //<<<
{
	struct ulp_con*	c = data;

	if (c) {
		if (c->rc.refcount > 0)
			fprintf(stderr, "free_con called with nonzero refcount: %ld\n", c->rc.refcount);

		struct output*	out = &c->out;
		for (int i=0; i<out->iovcnt; i++)
			if (out->iov_release[i].release)
				(out->iov_release[i].release)(out->iov_release[i].cdata);
		out->iovcnt = 0;

		if (out->iov != (struct iovec*)&out->iov_static) {
			free(out->iov);
			out->iov = (struct iovec*)&out->iov_static;
		}
		if (out->iov_release != (struct release_iov_segment*)&out->iov_release_static) {
			free(out->iov_release);
			out->iov_release = (struct release_iov_segment*)&out->iov_release_static;
		}

		if (c->in.parser_private_release) {
			(c->in.parser_private_release)(c->in.parser_private);
			c->in.parser_private = NULL;
			c->in.parser_private_release = NULL;
		}

		if (c->ob) {
			ulp_obstack_pool_release(c->ob);
			c->ob = NULL;
		}

		ulp_deregister_hook_cbs(&c->close_hooks);

		free(c);
		c = NULL;
	}
}

//>>>
int accept_thread(void* lh_) //<<<
{
	struct ulp_listen_handle*	lh = lh_;
	struct sockaddr_storage		con_addr;
	socklen_t					addrlen = sizeof con_addr;

	for (;;) {
		const int con_fd = accept4(lh->listen_fd, (struct sockaddr*)&con_addr, &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
		DBG("listen accept4 returned, con_fd: %d\n", con_fd);
		if (con_fd == -1) {
			switch (errno) {
				case EINTR:
					continue;
				case EINVAL:
					DBG("Listening socket was closed\n");
					goto finally;
				default:
					perror("Error from accept");
					goto finally;
			}
		}

		int one = 1;
		if (lh->type == AF_INET || lh->type == AF_INET6)
			if (setsockopt(con_fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)))
				perror("setsockopt zerocopy");

		struct ulp_con*	c = malloc(sizeof *c);
		*c = (struct ulp_con){
			.fd					= con_fd,
			.rc.free			= free_con,
			.rc.data			= c,
			.epollfd			= -1,
			.parser				= lh->parser,
			.cdata				= lh->cdata,
			.ob					= ulp_obstack_pool_get(ULP_OBSTACK_POOL_MEDIUM),
			.out.iovavail		= ULP_OUTPUT_IOV_STATIC_SIZE,
			.io_ready_data.cb	= &io_ready,
			.io_ready_data.cx	= c
		};
		c->out.iov = c->out.iov_static;
		c->out.iov_release = c->out.iov_release_static;

		if (
				mtx_init(&c->mutex, mtx_plain) != thrd_success ||
				mtx_init(&c->out.mutex, mtx_plain) != thrd_success
		) {
			fprintf(stderr, "Could not initialize ulp_con mutexes\n");
			ulp_close_con(c);
			ulp_obstack_pool_release(c->ob);
			free(c);
			c = NULL;
			continue;
		}

		int rcvbuf;
		unsigned int rcvbuflen = sizeof rcvbuf;
		getsockopt(c->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &rcvbuflen);
		DBG("new socket receivebuf: %d\n", rcvbuf);
		const size_t buf_alloc = rcvbuf*2 < 8192 ? 8192 : rcvbuf+8192;
		c->in.buf_size = buf_alloc;
		c->in.buf = obstack_alloc(c->ob, buf_alloc);
		c->in.cur = c->in.tok = c->in.mar = c->in.lim = c->in.buf;
		c->rc.free = free_con;
		ulp_rc_incref(c);

		if (lh->accept_handler && !(lh->accept_handler)(c, &c->in, lh->cdata)) {
			DBG("Connection rejected by accept handler");
			ulp_rc_decref(c);
		} else {
			read_con(c);
		}
	}

finally:
	ulp_obstack_pool_shutdown();
	DBG("Leaving accept thread\n");
	thrd_exit(EXIT_SUCCESS);
}

//>>>
void unlink_sock(struct ulp_rc_thing* aux, void* sun_path) //<<<
{
	if (-1 == unlink(sun_path)) {
		// TODO: what?
	}
}

//>>>
ulp_err ulp_start_listen_(struct ulp_start_listen_args args) //<<<
{
	ulp_err						err = {NULL, ULP_OK};
	struct addrinfo*			addrs = NULL;
	struct ulp_listen_handle*	lh = NULL;
	struct addrinfo				static_addr = {0};
	struct sockaddr_un			uds = {.sun_family = AF_UNIX};

	if (args.parser == NULL)
		THROW_ERR(finally, err, "parser cannot be NULL", ULP_ERR_BADARGS);

	if (args.node[0] == '/') { // unix domain socket
		if (args.service)
			THROW_ERR(finally, err, "service not supported for unix domain sockets");

		if (strlen(args.node) > sizeof(uds.sun_path)-1)
			THROW_ERR(finally, err, "unix domain socket path too long");

		strncpy(uds.sun_path, args.node, sizeof(uds.sun_path)-1);
		uds.sun_path[sizeof(uds.sun_path)-1] = 0;

		if (-1 == unlink(uds.sun_path))
			if (errno != ENOENT)
				THROW_POSIX(finally, err, "unlinking socket");

		static_addr = (struct addrinfo){
			.ai_family		= AF_UNIX,
			.ai_socktype	= SOCK_STREAM,
			.ai_addr		= (struct sockaddr*)&uds,
			.ai_addrlen		= sizeof(uds)
		};

		addrs = &static_addr;
	} else {
		struct addrinfo		hints = {
			.ai_flags		= AI_PASSIVE,
			.ai_family		= AF_UNSPEC,
			.ai_socktype	= SOCK_STREAM,
			.ai_protocol	= IPPROTO_TCP
		};

		const int rc = getaddrinfo(args.node, args.service, &hints, &addrs);
		if (rc != 0)
			THROW_ERR(finally, err, "getaddrinfo: ", .detail=gai_strerror(rc), .code=ULP_ERR_GETADDRINFO);
	}

	for (struct addrinfo* addr=addrs; addr; addr=addr->ai_next) {
		int		s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		thrd_t	accept_thread_id;

		if (s == -1)
			THROW_POSIX(loop_err, err, "socket failed");

		const int	enabled = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(int)) == -1)
			THROW_POSIX(loop_err, err, "setsockopt failed");

		if (bind(s, addr->ai_addr, addr->ai_addrlen) != 0)
			THROW_POSIX(loop_err, err, "bind failed");

		if (listen(s, 4096) == -1)
			THROW_POSIX(loop_err, err, "listen failed");

		struct ulp_listen_handle*	last_lh = lh;
		lh = malloc(sizeof *lh);
		*lh = (struct ulp_listen_handle){
			.next				= last_lh,
			.listen_fd			= s,
			.accept_handler		= args.accept,
			.parser				= args.parser,
			.cdata				= args.cdata,
			.cdata_release		= args.cdata_release,
			.type				= addr->ai_family,
			.ob					= ulp_obstack_pool_get(ULP_OBSTACK_POOL_MEDIUM)
		};
		s = -1;		// Hand off ownership of the socket to the listen_handle

		if (lh->type == AF_UNIX) { // unix domain socket
			struct ulp_hook_cb*	close_hook = obstack_alloc(lh->ob, sizeof *close_hook);
			*close_hook = (struct ulp_hook_cb){
				.hook_cb = unlink_sock,
				.aux.refcount = 1,
				.aux.data = obstack_copy0(lh->ob, uds.sun_path, strlen(uds.sun_path))
			};
			ulp_register_hook_cb(&lh->close_hooks, close_hook);
		}

		if (thrd_success != thrd_create(&accept_thread_id, &accept_thread, lh))
			THROW_ERR(loop_err, err, "Could not create accept thread");

		DBG("Started accept thread: %ld\n", accept_thread_id);
		lh->accept_thread_id = accept_thread_id;
		/*
		if (thrd_success != thrd_detach(accept_thread_id))
			THROW_ERR(finally, err, "Could not detach accept thread")
		*/
		continue;
	loop_err:
		if (s != -1)
			if (close(s) == -1)
				if (!err.msg)
					err = (ulp_err){.code=ULP_ERR_ERRNO, .posix=errno, .msg="close failed"};
		goto finally;
	}
	if (args.lh) {
		*args.lh = lh;

		if (args.link_lh && lh) {
			struct ulp_listen_handle*	lhp = lh;
			while (lhp->next) lhp = lhp->next;	// Find the end of the chain
			lhp->next = args.link_lh;			// Append the link_lh chain
		}
	}

	lh = NULL;

finally:
	if (addrs && addrs != &static_addr) {
		freeaddrinfo(addrs);
		addrs = NULL;
	}
	if (lh) {
		ulp_stop_listen(lh);
		lh = NULL;
	}
	return err;
}

//>>>
ulp_err ulp_stop_listen(struct ulp_listen_handle* lh) //<<<
{
	ulp_err				err = {NULL, ULP_OK};

	while (lh) {
		struct ulp_listen_handle*	this_lh = lh;
		thrd_t		accept_thread_id = this_lh->accept_thread_id;

		DBG("Calling shutdown on listen_fd %d\n", lh->listen_fd);
		shutdown(lh->listen_fd, SHUT_RD);

		if (accept_thread_id) {
			int res;
			DBG("Joining accept thread: %ld\n", accept_thread_id);
			if (thrd_success != thrd_join(accept_thread_id, &res))
				THROW_ERR(finally, err, "Error joining accept thread");
		}

		DBG("Closing listen_fd %d\n", lh->listen_fd);
		close(lh->listen_fd);
		lh->listen_fd = -1;

		ulp_call_hooks(&lh->close_hooks, lh);

		if (lh->ob) {
			ulp_obstack_pool_release(lh->ob);
			lh->ob = NULL;
		}

		lh = lh->next;
		if (this_lh->cdata_release) {
			(this_lh->cdata_release)(this_lh->cdata);
			this_lh->cdata = NULL;
			this_lh->cdata_release = NULL;
		}
		free(this_lh);
	}

finally:
	return err;
}

//>>>
void* ulp_listen_cdata(struct ulp_listen_handle* lh) //<<<
{
	return lh->cdata;
}

//>>>
int ulp_listen_family(struct ulp_listen_handle* lh) //<<<
{
	return lh->type;
}

//>>>
ulp_err ulp_listen_info_(struct ulp_listen_info_args args) //<<<
{
	ulp_err		err = {NULL, ULP_OK};

	if (args.lh == NULL) THROW_ERR(finally, err, "lh cannot be NULL", ULP_ERR_BADARGS);
	if (args.cb == NULL) THROW_ERR(finally, err, "cb cannot be NULL", ULP_ERR_BADARGS);

	for (struct ulp_listen_handle* lh=args.lh; lh; lh=lh->next) {
		struct ulp_listen_info_item item = {
			.addrlen	= sizeof(struct sockaddr_storage),
			.lh			= lh,
		};

		if (-1 == getsockname(lh->listen_fd, (struct sockaddr*)&item.addr, &item.addrlen))
			THROW_POSIX(finally, err, "getsockname failed");

		err = args.cb(&item, args.cdata);
		switch (err.code) {
			case ULP_OK:		break;
			case ULP_CONTINUE:	continue;
			case ULP_BREAK: 	goto finally;
			default:			goto finally;
		}
	}

finally:
	return err;
}

//>>>
ulp_err ulp_connect_(struct ulp_connect_args args) //<<<
{
	ulp_err				err = {NULL, ULP_OK};
	struct addrinfo*	addrs = NULL;
	struct addrinfo		static_addr = {0};
	struct sockaddr_un	uds = {
		.sun_family		= AF_UNIX
	};
	int					connected = 0;
	int					s = -1;

	if (args.c == NULL)
		THROW_ERR(finally, err, "c cannot be NULL", ULP_ERR_BADARGS);

	if (args.parser == NULL)
		THROW_ERR(finally, err, "parser cannot be NULL", ULP_ERR_BADARGS);

	if (args.node[0] == '/') { // unix domain socket
		if (args.service)
			THROW_ERR(finally, err, "service not supported for unix domain sockets");

		if (strlen(args.node) > sizeof(uds.sun_path)-1)
			THROW_ERR(finally, err, "unix domain socket path too long");

		strncpy(uds.sun_path, args.node, sizeof(uds.sun_path)-1);
		uds.sun_path[sizeof(uds.sun_path)-1] = 0;

		static_addr = (struct addrinfo){
			.ai_family		= AF_UNIX,
			.ai_socktype	= SOCK_STREAM,
			.ai_addr		= (struct sockaddr*)&uds,
			.ai_addrlen		= sizeof(uds)
		};

		addrs = &static_addr;
	} else {
		struct addrinfo		hints = {
			.ai_flags		= AI_PASSIVE,
			.ai_family		= AF_UNSPEC,
			.ai_socktype	= SOCK_STREAM,
			.ai_protocol	= IPPROTO_TCP
		};

		const int rc = getaddrinfo(args.node, args.service, &hints, &addrs);
		if (rc != 0)
			THROW_ERR(finally, err, "getaddrinfo: ", .detail=gai_strerror(rc), .code=ULP_ERR_GETADDRINFO);
	}

	for (struct addrinfo* addr=addrs; addr; addr=addr->ai_next) {
		s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

		if (s == -1)
			THROW_POSIX(finally, err, "socket failed");

		if (connect(s, addr->ai_addr, addr->ai_addrlen) == -1)
			continue;

		connected = 1;
		break;
	}

	if (connected) {
		int rcvbuf;
		unsigned int rcvbuflen = sizeof rcvbuf;
		if (-1 == getsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &rcvbuflen))
			THROW_POSIX(finally, err, "getsockopt for SO_RCVBUF");

		DBG("new socket receivebuf: %d\n", rcvbuf);

		struct ulp_con*	c = malloc(sizeof *c);

		*c = (struct ulp_con){
			.fd							= s,
			.epollfd					= -1,
			.rc.free					= free_con,
			.rc.data					= c,
			.parser						= args.parser,
			.cdata						= args.cdata,
			.ob							= ulp_obstack_pool_get(ULP_OBSTACK_POOL_MEDIUM),
			.out.iovavail				= ULP_OUTPUT_IOV_STATIC_SIZE,
			.io_ready_data.cb			= &io_ready,
			.io_ready_data.cx			= c,
			.in.shift_tags				= args.shift_tags,
			.in.parser_private			= args.parser_private,
			.in.parser_private_release	= args.parser_private_release
		};
		s = -1;	// Hand off ownership of the open socket to the caller
		c->out.iov = c->out.iov_static;
		c->out.iov_release = c->out.iov_release_static;

		const size_t buf_alloc = rcvbuf*2 < 8192 ? 8192 : rcvbuf+8192;
		c->in.buf_size = buf_alloc;
		c->in.buf = obstack_alloc(c->ob, buf_alloc);
		c->in.cur = c->in.tok = c->in.mar = c->in.lim = c->in.buf;
		ulp_rc_incref(c);

		*args.c = c;
		c = NULL;

		read_con(*args.c);
	} else {
		if (!err.msg)
			THROW_ERR(finally, err, "connect failed", .code=ULP_ERR_CONNECT);
	}

finally:
	if (addrs && addrs != &static_addr) {
		freeaddrinfo(addrs);
		addrs = NULL;
	}

	if (s != -1)
		if (close(s) == -1)
			if (err.msg == NULL)
				err = (ulp_err){.code=ULP_ERR_ERRNO, .posix=errno, .msg="close failed"};

	return err;
}

//>>>
ulp_err ulp_send_(struct ulp_send_args args) //<<<
{
	ulp_err			err = {NULL, ULP_OK};
	struct output*	out = &args.c->out;

	if (args.len == 0) goto finally;	// Can be used to trigger a write_con after a sequence with .flags=ULP_MORE

	if (thrd_success != mtx_lock(&out->mutex))
		THROW_ERR(finally, err, "ulp_send could not aquire the output mutex");

	if (out->iovcnt >= out->iovavail) { // Need to expand the iov slots <<<
		const int	newavail = out->iovavail*2;

		if (out->iov == (struct iovec*)&out->iov_static) {
			out->iov = malloc(newavail*sizeof(struct iovec));
			out->iov_release = malloc(newavail*sizeof(struct release_iov_segment));
			memcpy(out->iov, out->iov_static, ULP_OUTPUT_IOV_STATIC_SIZE*sizeof(struct iovec));
			memcpy(out->iov_release, out->iov_release_static, ULP_OUTPUT_IOV_STATIC_SIZE*sizeof(struct release_iov_segment));
		} else {
			out->iov = realloc(out->iov, newavail*sizeof(struct iovec));
			out->iov_release = realloc(out->iov_release, newavail*sizeof(struct release_iov_segment));
		}
		if (out->iov == NULL)			THROW_ERR(finally, err, "ulp_send could not resize the iov list\n");
		if (out->iov_release == NULL)	THROW_ERR(finally, err, "ulp_send could not resize the iov_release list\n");
		out->iovavail = newavail;
	}
	//>>>

	_Pragma("GCC diagnostic push");
	_Pragma("GCC diagnostic ignored \"-Wdiscarded-qualifiers\"");	// .iov_base isn't const unfortunately
	const int iov_slot = out->iovcnt++;
	out->iov[iov_slot] = (struct iovec){
		.iov_base	= args.data,
		.iov_len	= args.len
	};
	_Pragma("GCC diagnostic pop");
	out->iov_release[iov_slot] = (struct release_iov_segment){
		.release	= args.release,
		.cdata		= args.release_cdata
	};

finally:
	if (thrd_success != mtx_unlock(&out->mutex))
		if (err.msg == NULL)
			err = ERR("ulp_send could not release the output mutex");

	if (err.msg == NULL)
		if (!(args.flags & ULP_MORE) && !out->waiting)
			write_con(args.c);

	return err;
}

//>>>
ulp_err ulp_getpeername(struct ulp_con* c, struct sockaddr*restrict addr, socklen_t*restrict addrlen) //<<<
{
	ulp_err		err = {NULL, ULP_OK};

	if (-1 == getpeername(c->fd, addr, addrlen))
		THROW_POSIX(finally, err, "getpeername");

finally:
	return err;
}

//>>>
int ulp_eof(struct ulp_con* c) //<<<
{
	return c->eof;
}

//>>>
void ulp_con_set_cdata(struct ulp_con* c, void* cdata) //<<<
{
	c->cdata = cdata;
}

//>>>
void* ulp_con_get_cdata(struct ulp_con* c) //<<<
{
	return c->cdata;
}

//>>>
ulp_err	g_init_once_err = {0, ULP_OK};
void ulp_init_once() //<<<
{
	int		io_thread_ready = -1;

	g_init_once_err = ERR(0, ULP_OK);
	io_thread_ready = eventfd(0, EFD_CLOEXEC);

	if (thrd_success != thrd_create(&io_thread_id, io_thread, &io_thread_ready))
		THROW_ERR(finally, g_init_once_err, "Could not create io_thread");
	/*
	if (thrd_success != thrd_detach(io_thread_id))
		THROW_ERR(finally, g_init_once_err, "Could not detach io_thread");
	*/

	// Wait for io_thread to start up <<<
	uint64_t val;
	const int got = read(io_thread_ready, &val, sizeof(uint64_t));
	if (got == -1) THROW_POSIX(finally, g_init_once_err, "read io_thread_ready");
	close(io_thread_ready);
	io_thread_ready = -1;
	g_io_thread_started = 1;
	// Wait for io_thread to start up >>>

finally:
	if (io_thread_ready > 0) {
		close(io_thread_ready);
		io_thread_ready = -1;
	}
}

//>>>
ulp_err ulp_init() //<<<
{
	call_once(&autoinit, ulp_init_once);
	return g_init_once_err;
}

//>>>
ulp_err ulp_shutdown() //<<<
{
	ulp_err		err = {NULL, ULP_OK};

	g_io_thread_running = 0;
	const int wrote = write(io_thread_wakeup_fd, &(uint64_t){1}, 8);
	if (wrote == -1)
		THROW_POSIX(finally, err, "write signalling io_thread_wakeup");

	if (g_io_thread_started) {
		int	res;
		if (thrd_success != thrd_join(io_thread_id, &res))
			THROW_ERR(finally, err, "Could not join io_thread");

		g_io_thread_started = 0;
	}

	ulp_obstack_pool_shutdown();

finally:
	return err;
}

//>>>

// vim: foldmethod=marker foldmarker=<<<,>>>
