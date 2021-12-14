#include "int.h"

thrd_t	io_thread_id;
int io_thread_wakeup_fd = -1;
int	io_thread_epollfd = -1;


int init_msg_queue(struct msg_queue* q) //<<<
{
	dlist_init(&q->msgs);
	if (mtx_init(&q->mutex, mtx_plain) != thrd_success) {
		fprintf(stderr, "Could not initialize msg_queue mutex\n");
		return 1;
	}
	if (cnd_init(&q->msg_avail) != thrd_success) {
		fprintf(stderr, "Could not initialize msg_queue condition\n");
		return 1;
	};
}

//>>>
void deinit_msg_queue(struct msg_queue* q) //<<<
{
	cnd_destroy(&q->msg_avail);
	mtx_destroy(&q->mutex);
	// TODO: iterate of msgs and destroy them somehow?
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

	for (;;) {
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
	thrd_exit(EXIT_SUCCESS);
}

//>>>
void io_thread_register_con(struct con* c) //<<<
{
	struct epoll_event	ev;
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	ev.data.ptr = &c->io_ready_data;
	epoll_ctl(io_thread_epollfd, EPOLL_CTL_ADD, c->fd, &ev);
	c->epollfd = io_thread_epollfd;
	fprintf(stderr, "Registered con fd %d with io_thread_epollfd\n", c->fd);
}

//>>>
void close_con(struct con* c) //<<<
{
	if (c->epollfd != -1) {
		epoll_ctl(c->epollfd, EPOLL_CTL_DEL, c->fd, NULL);
		c->epollfd = -1;
	}

	if (-1 == close(c->fd))
		perror("close con fd");

	c->fd = -1;

	call_hooks(&c->close_hooks, c);
}

//>>>
int read_con(struct con* c) //<<<
{
	int		closed = 0;

	for (;;) {
		struct input *const	in = &c->in;
		size_t				shift = in->tok - in->buf;
		size_t				free = in->buf_size - (in->lim - in->tok);

		if (free < 1) {
			/* Input too long for receive buffer */
			fprintf(stderr, "Input too long for receive buffer\n");
			close_con(c);
			closed = 1;
			goto finally;
		}

		if (shift) {
			memmove(in->buf, in->tok, in->buf_size - shift);
			in->lim -= shift;
			in->cur -= shift;
			in->mar -= shift;
			in->tok -= shift;
			if (in->shift_tags)
				in->shift_tags(in, shift);
		}

		const size_t	avail = in->buf_size - (in->lim - in->buf);
		const size_t	want = in->remain > 0 && in->remain < avail ? in->remain : avail;
		const ssize_t	got = recv(c->fd, in->lim, want, MSG_DONTWAIT);

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
					close_con(c);
					closed = 1;
					goto finally;
			}
		} else if (got == 0) {
			printf("Connection socket closed\n");
			close_con(c);
			closed = 1;
			goto finally;
		} else if (in->remain > 0) {
			fprintf(stderr, "Read %ld bytes: \"%.*s\"\n", got, (int)got, in->lim);
			in->remain -= got;
		}

		in->lim += got;
		in->lim[0] = 0;	// Append sentinel

parse:
		enum parser_status	status = c->parser(in, c->q);

		switch (status) {
			case PARSER_STATUS_WAITING:
				break;

			case PARSER_STATUS_OVERFLOW:
			case PARSER_STATUS_ERROR:
				fprintf(stderr, "Parse error\n");
				close_con(c);
				closed = 1;
				goto finally;

			case PARSER_STATUS_CLOSE:
				close_con(c);
				closed = 1;
				goto finally;

			default:
				break;
		}
	}

finally:
	return closed;
}

//>>>
int write_con(struct con* c) //<<<
{
	struct output*	job = NULL;
	int				closed = 0;

	while ((job = dlist_head(&c->out))) {
		switch (job->source) {
			case IO_SOURCE_BUF:
				{
					const size_t	remain = job->buf.lim - job->buf.cur;
					const ssize_t	wrote = send(c->fd, job->buf.cur, remain, MSG_DONTWAIT | (job->dl.next ? MSG_MORE : 0));

					if (wrote == -1) {
						switch (errno) {
#if EAGAIN != EWOULDBLOCK
							case EWOULDBLOCK:
#endif
							case EAGAIN:
								if (c->epollfd == -1)
									io_thread_register_con(c);
								goto finally;

							default:
								perror("Error writing to con fd");
								close_con(c);
								closed = 1;
								goto finally;
						}
					}

					job->buf.cur += wrote;
					if (job->buf.lim <= job->buf.cur) {
						job = dlist_pop_head(&c->out);
						continue;
					}
				}
				break;

			case IO_SOURCE_STREAM:
				// TODO
				break;

			default:
				fprintf(stderr, "Invalid source type: %d\n", job->source);
				job = dlist_pop_head(&c->out);
		}
	}

finally:
	return closed;
}

//>>>
void io_ready(void* con_, uint32_t events) //<<<
{
	struct con*	c = con_;

	if (events & EPOLLIN)
		if (read_con(c))
			return;			// Socket was closed, skip write step

	if (events & EPOLLOUT)
		write_con(c);
}

//>>>
void free_con(struct rc_thing* aux, void* data) //<<<
{
	struct con*	c = aux->data;

	if (c) {
		if (c->ob) {
			obstack_pool_release(c->ob);
			c->ob = NULL;
		}

		deregister_hook_cbs(&c->close_hooks);

		free(c);
		c = NULL;
	}
}

//>>>
int accept_thread(void* li_) //<<<
{
	struct listen_info*			li = li_;
	struct sockaddr_storage		con_addr;
	socklen_t					addrlen = sizeof con_addr;

	for (;;) {
		const int con_fd = accept4(li->listen_fd, (struct sockaddr*)&con_addr, &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (con_fd == -1) {
			switch (errno) {
				case EINTR:
					continue;
				case EINVAL:
					fprintf(stderr, "Listening socket was closed\n");
					goto finally;
				default:
					perror("Error from accept");
					goto finally;
			}
		}

		struct con*	c = malloc(sizeof *c);
		*c = (struct con){
			.fd		= con_fd,
			.parser	= li->parser,
			.q		= li->q,
			.ob		= obstack_pool_get(OBSTACK_POOL_MEDIUM)
		};
		int rcvbuf;
		unsigned int rcvbuflen = sizeof rcvbuf;
		getsockopt(c->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &rcvbuflen);
		printf("new socket receivebuf: %d\n", rcvbuf);
		const size_t buf_alloc = rcvbuf*2 < 8192 ? 8192 : rcvbuf+8192;
		c->in.buf_size = buf_alloc-1;		// -1: reserve space for trailing sentinel
		c->in.buf = obstack_alloc(c->ob, buf_alloc);
		c->in.cur = c->in.tok = c->in.mar = c->in.lim = c->in.buf;
		c->in.remain = -1;
		c->io_ready_data = (struct io_ready_data){
			.cb	= &io_ready,
			.cx = c
		};
		c->epollfd = -1;
		struct hook_cb*	close_hook = obstack_alloc(c->ob, sizeof *close_hook);
		*close_hook = (struct hook_cb){
			.hook_cb = free_con,
			.aux.refcount = 1,
			.aux.data = c
		};
		register_hook_cb(&c->close_hooks, close_hook);

		read_con(c);
	}

finally:
	thrd_exit(EXIT_SUCCESS);
}

//>>>
int start_listen(const char* node, const char* service, parser* parser, struct msg_queue* q) //<<<
{
	int		rc;
	struct addrinfo*	addrs;
	struct addrinfo		hints = {
		.ai_flags		= AI_PASSIVE,
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= IPPROTO_TCP
	};

	rc = getaddrinfo(node, service, &hints, &addrs);
	if (rc != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		exit(EXIT_FAILURE);
	}

	for (struct addrinfo* addr=addrs; addr; addr=addr->ai_next) {
		const int	s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		thrd_t		accept_thread_id;
		const int	enabled = 1;

		if (s == -1) {
			perror("socket");
			exit(EXIT_FAILURE);
		}

		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(int)) == -1) {
			perror("setsockopt");
			exit(EXIT_FAILURE);
		}

		if (bind(s, addr->ai_addr, addr->ai_addrlen) != 0) {
			perror("bind");
			exit(EXIT_FAILURE);
		}

		if (listen(s, 4096) == -1) {
			perror("listen");
			exit(EXIT_FAILURE);
		}

		struct listen_info*	li = malloc(sizeof *li);
		*li = (struct listen_info){
			.listen_fd	= s,
			.parser		= parser,
			.q			= q
		};
		if (thrd_success != thrd_create(&accept_thread_id, &accept_thread, li)) {
			fprintf(stderr, "Could not create accept thread\n");
			return 1;
		}
		if (thrd_success != thrd_detach(accept_thread_id)) {
			fprintf(stderr, "Could not detach accept thread\n");
			return 1;
		}
	}
}

//>>>
int ulp_init() //<<
{
	int		io_thread_ready = -1;

	io_thread_ready = eventfd(0, EFD_CLOEXEC);

	if (thrd_success != thrd_create(&io_thread_id, io_thread, &io_thread_ready)) {
		fprintf(stderr, "Could not create io_thread\n");
		goto finally;
	}
	if (thrd_success != thrd_detach(io_thread_id)) {
		fprintf(stderr, "Could not detach io_thread\n");
		goto finally;
	}

	// Wait for io_thread to start up <<<
	uint64_t val;
	const int got = read(io_thread_ready, &val, sizeof(uint64_t));
	if (got == -1) {
		perror("read io_thread_ready");
		goto finally;
	}
	close(io_thread_ready);
	io_thread_ready = -1;
	// Wait for io_thread to start up >>>

finally:
	if (io_thread_ready > 0) {
		close(io_thread_ready);
		io_thread_ready = -1;
	}
	return 0;
}

//>>>

// vim: foldmethod=marker foldmarker=<<<,>>>
