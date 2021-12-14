#ifndef INT_H
#define INT_H

#define _GNU_SOURCE
#define obstack_chunk_alloc	malloc
#define obstack_chunk_free	free

#include "ulp.h"

struct listen_info;
struct input;
struct output;

typedef void (io_ready_cb)(void* cdata, uint32_t events);

struct listen_info {
	int					listen_fd;
	parser*				parser;
	struct msg_queue*	q;
};

struct io_ready_data {
	io_ready_cb*	cb;
	void*			cx;
};

struct con {
	int						fd;
	parser*					parser;
	struct msg_queue*		q;
	struct obstack*			ob;
	struct input			in;
	struct dlist			out;			// Linked list of struct output
	int						epollfd;		// -1 if not registered with epoll
	struct io_ready_data	io_ready_data;
	struct dlist			close_hooks;
};

extern struct msg_queue http_reqs;

#include <x86intrin.h>
#define cycles()	__rdtsc()

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
#endif
