#ifndef INT_H
#define INT_H

#define _GNU_SOURCE
#define obstack_chunk_alloc	malloc
#define obstack_chunk_free	free

#include "ulp.h"

#include <sys/un.h>

#define ERR(...)	(ulp_err){__VA_ARGS__}

#define THROW_ERR(label, var, ...)				\
	do {										\
		(var) = (ulp_err){__VA_ARGS__};	\
		goto label;								\
	} while(0);

#define THROW_POSIX(label, var, m, ...)			\
	do {										\
		(var) = (ulp_err){.code=ULP_ERR_ERRNO, .posix=errno, .msg=(m), __VA_ARGS__};	\
		goto label;								\
	} while(0);

#if DEBUG
#	define DBG(msg, ...) fprintf(stderr, (msg), __VA_ARGS__)
#else
#	define DBG(msg, ...)
#endif

typedef void (io_ready_cb)(void* cdata, uint32_t events);

struct ulp_listen_handle {
	struct ulp_listen_handle*	next;
	int							listen_fd;
	ulp_parser*					parser;
	void*						cdata;			// Opaque pointer registered at start_listen, passed to parser
	ulp_rc_releaser*			cdata_release;
	thrd_t						accept_thread_id;
	int							type;			// socket address family, ie. AF_INET, AF_INET6, AF_UNIX
	ulp_accept*					accept_handler;
	struct ulp_dlist			close_hooks;
	struct obstack*				ob;
};

struct io_ready_data {
	io_ready_cb*	cb;
	void*			cx;
};

// Output handlers <<<
struct release_iov_segment {
	ulp_rc_releaser*	release;
	void*				cdata;
};

#define ULP_OUTPUT_IOV_STATIC_SIZE	64
struct output {
	struct ulp_dlist_elem		dl;		// Must be first
	mtx_t						mutex;
	size_t						iov_base;	// iov[0] is the nth segment we've sent since opening
	struct iovec*				iov;
	struct iovec				iov_static[ULP_OUTPUT_IOV_STATIC_SIZE];
	int							iovcnt;
	int							iovavail;
	struct release_iov_segment*	iov_release;
	struct release_iov_segment	iov_release_static[ULP_OUTPUT_IOV_STATIC_SIZE];
	int							waiting;	// true if we're waiting for a writable callback
};
// Output handlers >>>


struct ulp_con {
	struct ulp_rc_thing		rc;				// Must be first
	int						fd;
	mtx_t					mutex;
	ulp_parser*				parser;
	void*					cdata;
	struct obstack*			ob;
	struct ulp_input		in;
	struct output			out;
	int						epollfd;		// -1 if not registered with epoll
	struct io_ready_data	io_ready_data;
	struct ulp_dlist		close_hooks;
	int						closing;
	int						eof;
};

/*
#include <x86intrin.h>
#define cycles()	__rdtsc()
*/

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
#endif
