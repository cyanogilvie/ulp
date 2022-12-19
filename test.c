#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <threads.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <obstack.h>
#include "ulp.h"

#include "generated/http.h"

int mainloop_wakeup_fd = -1;

void http_req(struct ulp_con* c, struct http_msg* msg) //<<<
{
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

	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = NULL;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, mainloop_wakeup_fd, &ev);

	for (;;) {
		const int nfds = epoll_wait(epollfd, events, max_events, -1);
		fprintf(stderr, "mainloop epoll_wait returned\n");
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
	return;
}

//>>>

int main(int argc, char** argv) //<<<
{
	if (ulp_init()) {
		fprintf(stderr, "up_init error\n");
		goto finally;
	}

	//ulp_init_msg_queue(&http_reqs);

	if (ulp_start_listen("0.0.0.0", "1234", &parse_http, http_req)) {
		fprintf(stderr, "Could not listen on 0.0.0.0:1234\n");
		return EXIT_FAILURE;
	}

	mainloop();

finally:
	return EXIT_SUCCESS;
}

//>>>

// vim: foldmethod=marker foldmarker=<<<,>>>
