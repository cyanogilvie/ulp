#ifndef INT_H
#define INT_H

#define _GNU_SOURCE
#define obstack_chunk_alloc	malloc
#define obstack_chunk_free	free

#include "ulp.h"

struct listen_info;
struct ulp_input;
struct ulp_output;

typedef void (io_ready_cb)(void* cdata, uint32_t events);

struct listen_info {
	int					listen_fd;
	ulp_parser*			parser;
	void*				cdata;			// Opaque pointer registered at start_listen, passed to parser
};

struct io_ready_data {
	io_ready_cb*	cb;
	void*			cx;
};

struct ulp_con {
	int						fd;
	ulp_parser*				parser;
	void*					cdata;
	struct obstack*			ob;
	struct ulp_input		in;
	struct ulp_dlist		out;			// Linked list of struct output
	int						epollfd;		// -1 if not registered with epoll
	struct io_ready_data	io_ready_data;
	struct ulp_dlist		close_hooks;
};

#include <x86intrin.h>
#define cycles()	__rdtsc()

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
#endif
