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

#include "generated/testproto.h"

int mainloop_wakeup_fd = -1;
int g_mainloop_running = 1;

void got_packet(struct ulp_con* c, const char* packet, size_t len) //<<<
{
	printf("Got packet: (%.*s)\n", (int)len, packet);
	ulp_send(c, "Got: (", sizeof("Got: ("), .flags=ULP_MORE);
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

int main(int argc, char** argv) //<<<
{
	ulp_err		err = {NULL, ULP_OK};
	struct ulp_listen_handle*	lh = NULL;
	struct ulp_listen_handle*	lh_uds = NULL;

	ULP_CHECK(finally, err, ulp_init());

	ULP_CHECK(finally, err,
			ulp_start_listen("0.0.0.0",
				.service	= "1234",
				.parser		= parse_testproto,
				.cdata		= got_packet,
				.lh			= &lh
	));

	ULP_CHECK(finally, err,
			ulp_start_listen("/tmp/ulp.sock",
				.parser		= parse_testproto,
				.cdata		= got_packet,
				.lh			= &lh_uds
	));

	mainloop();

	ulp_stop_listen(lh);
	ulp_stop_listen(lh_uds);
	ulp_shutdown();

finally:
	if (err.msg) {
		fprintf(stderr, "Unhandled error: %s\n", err.msg);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

//>>>

// vim: foldmethod=marker foldmarker=<<<,>>>
