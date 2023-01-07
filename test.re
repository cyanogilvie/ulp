#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <time.h>
#include <signal.h>
#include "ulp.h"

#include "generated/testproto.h"
#include "generated/testproto_client.h"

int mainloop_wakeup_fd = -1;
int g_mainloop_running = 1;

void got_packet(struct ulp_con* c, const char* packet, size_t len) //<<<
{
	//printf("Got packet: (%.*s)\n", (int)len, packet);
	ulp_send(c, "Got: (", sizeof("Got: (")-1, .flags=ULP_MORE);
	char* copy = malloc(len);
	memcpy(copy, packet, len);	// Have to make a copy for ulp_send because we're about to shift the token out of the input buffer and it may not have been picked up yet
	ulp_send(c, packet, len, .flags=ULP_MORE, .release=free, .release_cdata=copy);
	ulp_send(c, ")\n", 2);
}

//>>>
void mainloop() //<<<
{
	const int			max_events = 100;
	const int			epollfd = epoll_create1(0);
	struct epoll_event	events[max_events];
	struct epoll_event	ev;

	if (epollfd == -1) {
		perror("epoll_create1");
		goto finally;
	}

	mainloop_wakeup_fd = eventfd(0, EFD_CLOEXEC);

	fprintf(stderr, "Entering mainloop, mainloop_wakeup_fd: %d\n", mainloop_wakeup_fd);
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = NULL;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, mainloop_wakeup_fd, &ev);

	while (g_mainloop_running) {
		const int nfds = epoll_wait(epollfd, events, max_events, -1);
		if (-1 == nfds) {
			if (errno == EINTR) continue;
			perror("epoll_wait");
			goto finally;
		}

		for (int i=0; i<nfds; i++) {
			// TODO: something
		}
	}

finally:
	if (epollfd > 0)
		close(epollfd);

	return;
}

//>>>
int fast_atoi(const char* s, const char* e) // This is only safe if the caller has ensured that the input is valid <<<
{
	int					acc = 0;
	const char*restrict	c = s;

	while (c < e) {
		acc = acc*10 + *c++-'0';
	}

	return acc;
}

//>>>
ulp_err test_connection(struct obstack* ob, const char* node, int node_len, const char* service, int service_len, int it, int timeout) //<<<
{
	ulp_err					err = {NULL, ULP_OK};
	struct ulp_msg_queue	q;
	struct timespec			tp;
	struct ulp_con*			c = NULL;
	char*					node_str	= obstack_copy0(ob, node,    node_len);
	char*					service_str	= service_len ? obstack_copy0(ob, service, service_len) : NULL;

	fprintf(stderr, "test_connection, it: %d\n", it);

	ULP_CHECK(finally, err, ulp_init_msg_queue(&q));

	ULP_CHECK(finally, err, ulp_connect(
		.node					= node_str,
		.service				= service_str,
		.c						= &c,
		.parser					= parse_testproto_client,
		.parser_private			= init_testproto_client_parser(),
		.parser_private_release	= free_testproto_client_parser,
		.shift_tags				= shift_testproto_client_tags,
		.cdata					= &q
	));

	if (-1 == clock_gettime(CLOCK_MONOTONIC_RAW, &tp)) ULP_THROW_POSIX(finally, err, "clock_gettime");
	const uint64_t		start = tp.tv_sec * 1000000000ULL + tp.tv_nsec;

	for (int i=0; i<it; i++) {
		struct ulp_msg*	msg = NULL;

		ULP_CHECK(finally, err, ulp_send(c, .data="hello, ulp\n", .len=sizeof("hello, ulp\n")-1));
		ULP_CHECK(finally, err, ulp_msg_queue_get(&q, .c=c, .msg=&msg, .timeout=timeout*1000));
		//ULP_CHECK(finally, err, ulp_msg_queue_get(&q, .c=c, .msg=&msg, .timeout=1000000000));
		//printf("response: (%s)\n", (char*)msg->data);
		ulp_msg_free(msg);
	}

	if (-1 == clock_gettime(CLOCK_MONOTONIC_RAW, &tp)) ULP_THROW_POSIX(finally, err, "clock_gettime");
	const uint64_t		end = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
	printf("%d round trips, %.3f microseconds latency\n", it, (end-start)/(it*1e3));

finally:
	ulp_deinit_msg_queue(&q);
	if (c) {
		ulp_close_con(c);
		c = NULL;
	}
	obstack_free(ob, node_str);

	return err;
}

//>>>

void sighandler(int sig) //<<<
{
	fprintf(stderr, "In sighandler: %d\n", sig);
	if (g_mainloop_running && mainloop_wakeup_fd > 0) {
		g_mainloop_running = 0;
		if (-1 == write(mainloop_wakeup_fd, &(uint64_t){1}, 8))
			perror("write on mainloop_wakeup eventfd");
	} else {
		fprintf(stderr, "Mainloop not running, exiting directly\n");
		ulp_shutdown();
		exit(EXIT_SUCCESS);
	}
}

//>>>
int main(int argc, char** argv) //<<<
{
	struct ulp_listen_handle*	lh = NULL;
	ulp_err						err = {NULL, ULP_OK};
	struct obstack*				ob = ulp_obstack_pool_get(ULP_OBSTACK_POOL_MEDIUM);

	signal(SIGINT,  sighandler);
	signal(SIGTERM, sighandler);

	ULP_CHECK(finally, err, ulp_init());

	for (int i=1; i<argc; i++) {
		char	*YYCURSOR=argv[i], *YYLIMIT=argv[i]+strlen(argv[i])+1, *YYMARKER, *ns, *ne, *ss, *se, *its, *ite, *ts, *te;
		char	yych;
		int		state;
		/*!stags:re2c format = "\t\tchar*\t@@{tag} = NULL;\n"; */

		// Interpret each arg as a DSL statement
		/*!re2c
			re2c:api:style         = free-form;
			re2c:define:YYGETSTATE = "state";
			re2c:define:YYSETSTATE = "state = @@;";
			re2c:define:YYFILL     = "";

			end			= "\x00";
			digit		= [0-9];
			ws			= [ \t\n]*;
			node		= ws @ns [^\x00,]+ @ne ws;
			service		= ws @ss [^\x00]+ @se ws;
			abs_path	= ws @ns "/" [^\x00]* @ne ws;
			it			= ws @its digit+ @ite ws;
			timeout		= ws @ts digit+ @te ws;

			"listen(" abs_path ")" end	{
				char*	sun_path = obstack_copy0(ob, ns, ne-ns);
				printf("Listening on UDS path %s\n", sun_path);
				ULP_CHECK(finally, err,
					ulp_start_listen(sun_path,
						.accept		= accept_handler,
						.parser		= parse_testproto,
						.cdata		= got_packet,
						.lh			= &lh,
						.link_lh	= lh
					)
				);
				obstack_free(ob, sun_path);
				continue;
			}

			"listen(" node "," service ")" end	{
				char*	node	= obstack_copy0(ob, ns, ne-ns);
				char*	service	= obstack_copy0(ob, ss, se-ss);
				printf("Listening on INET socket %s:%s\n", node, service);
				ULP_CHECK(finally, err,
					ulp_start_listen(node,
						.service	= service,
						.accept		= accept_handler,
						.parser		= parse_testproto,
						.cdata		= got_packet,
						.lh			= &lh,
						.link_lh	= lh
					)
				);
				obstack_free(ob, node);
				continue;
			}

			"testconnect(" abs_path "," it "," timeout ")" end	{
				ULP_CHECK(finally, err, test_connection(ob, ns, ne-ns, NULL, 0, fast_atoi(its, ite), fast_atoi(ts, te)));
				continue;
			}

			"testconnect(" node "," service "," it "," timeout ")" end	{
				ULP_CHECK(finally, err, test_connection(ob, ns, ne-ns, ss, se-ss, fast_atoi(its, ite), fast_atoi(ts, te)));
				continue;
			}

			"quit" end {
				printf("Stopping\n");
				g_mainloop_running = 0;
				if (mainloop_wakeup_fd > 0 && -1 == write(mainloop_wakeup_fd, &(uint64_t){1}, 8))
					perror("write on mainloop_wakeup eventfd");
				goto finally;
			}

			* {
				fprintf(stderr, "Can't parse command: (%s), ofs: %ld\n", argv[i], YYCURSOR-argv[i]-1);
				err = (ulp_err){"Invalid command"};
				goto finally;
			}
		*/
	}

	if (lh)
		mainloop();

finally:
	if (ob) {
		ulp_obstack_pool_release(ob);
		ob = NULL;
	}

	ulp_stop_listen(lh);
	ulp_shutdown();
	if (mainloop_wakeup_fd > 0)
		if (-1 == close(mainloop_wakeup_fd))
			perror("close mainloop_wakeup_fd");

	if (err.msg) {
		fprintf(stderr, "Unhandled error: %s\n", err.msg);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

//>>>

// vim: ft=c foldmethod=marker foldmarker=<<<,>>>
