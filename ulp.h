#ifndef ULP_H
#define ULP_H

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

struct ulp_con;
struct ulp_msg_queue;
struct ulp_input;
struct ulp_listen_handle;

enum ulp_err_code {
	ULP_ERR_NONE=0,
	ULP_OK,
	ULP_BREAK,
	ULP_CONTINUE,
	ULP_ERR_BADARGS,
	ULP_ERR_GETADDRINFO,
	ULP_ERR_ERRNO,
	ULP_ERR_CONNECT,
	ULP_ERR_TIMEOUT,
	ULP_ERR_QUEUE_EMPTY,
	ULP_ERR_UNSPEC,
	ULP_ERR_TCL,			// Error details left in a Tcl_Interp in .detail

	ULP_ERR_END
};

typedef struct ulp_err {
	const char*			msg;
	enum ulp_err_code	code;
	const char*			detail;
	int					posix;
} ulp_err;

#define ULP_CHECK(label, var, call)		do { var = call; if (var.msg) goto label; } while(0)
#define ULP_THROW(label, var, m, ...)									\
	do {																\
		(var) = (ulp_err){.code=ULP_ERR_UNSPEC, .msg=(m), __VA_ARGS__};	\
		goto label;														\
	} while(0)
#define ULP_THROW_POSIX(label, var, m, ...)		\
	do {										\
		(var) = (ulp_err){.code=ULP_ERR_ERRNO, .posix=errno, .msg=(m), __VA_ARGS__};	\
		goto label;								\
	} while(0)

#include "ulp_dlist.h"
#include "ulp_obstack_pool.h"
#include "ulp_refcounted.h"
#include "ulp_cb.h"
#include "ulp_msg_handlers.h"

enum ulp_parser_status {
	ULP_PARSER_STATUS_UNDEF=0,
	ULP_PARSER_STATUS_WAITING,
	ULP_PARSER_STATUS_OVERFLOW,
	ULP_PARSER_STATUS_CLOSE,
	ULP_PARSER_STATUS_ERROR,
	ULP_PARSER_STATUS_DONE
};

typedef ulp_err (ulp_foreach_cb)(void* item, void* cdata);
typedef enum ulp_parser_status (ulp_parser)(struct ulp_con* con, struct ulp_input*const in, void* cdata);
typedef int (ulp_accept)(struct ulp_con* con, struct ulp_input*const in, void* cdata);

struct ulp_msg_queue {
	mtx_t				mutex;
	cnd_t				msg_avail;
	struct ulp_dlist	msgs;		// List of struct msg
};

typedef void (ulp_shift_tags)(struct ulp_input* c, ssize_t shift);

struct ulp_mtag {
	struct ulp_mtag*	prev;
	ssize_t				dist;
};

static inline void ulp_mtag(struct ulp_mtag** pmt, const unsigned char* b, const unsigned char* t, struct obstack* ob) //<<<
{
	struct ulp_mtag*    mt = obstack_alloc(ob, sizeof(struct ulp_mtag));
	mt->prev = *pmt;
	mt->dist = t - b;
	*pmt = mt;
}

//>>>

struct ulp_input {
	unsigned char*		cur;
	unsigned char*		mar;
	unsigned char*		tok;
	unsigned char*		lim;
	ulp_shift_tags*		shift_tags;	// Optional cb to shift the pending token tags when input is refilled
	void*				parser_private;
	ulp_rc_releaser*	parser_private_release;
	size_t				buf_size;
	unsigned char*		buf;
	int					eof;		// Read side has closed
};

ulp_err ulp_init();
ulp_err ulp_shutdown();	// Only really for testing

struct ulp_start_listen_args {
	const char*					node;
	const char*					service;
	ulp_accept*					accept;
	ulp_parser*					parser;
	void*						cdata;
	ulp_rc_releaser*			cdata_release;
	struct ulp_listen_handle**	lh;
	struct ulp_listen_handle*	link_lh;
};
ulp_err ulp_start_listen_(struct ulp_start_listen_args);
#define ulp_start_listen(n_, ...) ulp_start_listen_((struct ulp_start_listen_args){.node=(n_), __VA_ARGS__})

ulp_err ulp_stop_listen(struct ulp_listen_handle* lh);

void* ulp_listen_cdata(struct ulp_listen_handle* lh);
int ulp_listen_family(struct ulp_listen_handle* lh);

struct ulp_listen_info_item {
	struct sockaddr_storage*	addr;
	socklen_t					addrlen;
	struct ulp_listen_handle*	lh;
};
struct ulp_listen_info_args {
	struct ulp_listen_handle*	lh;
	ulp_foreach_cb*				cb;		// item is struct ulp_listen_info_item
	void*						cdata;
};
ulp_err ulp_listen_info_(struct ulp_listen_info_args);
#define ulp_listen_info(...) ulp_listen_info_((struct ulp_listen_info_args){__VA_ARGS__})

ulp_err ulp_close_con(struct ulp_con* c);
ulp_err ulp_init_msg_queue(struct ulp_msg_queue* q);
ulp_err ulp_deinit_msg_queue(struct ulp_msg_queue* q);
ulp_err ulp_getpeername(struct ulp_con* c, struct sockaddr*restrict addr, socklen_t*restrict addrlen);
int ulp_eof(struct ulp_con* c);


// ulp_send flags:
#define ULP_MORE		1<<0		// Signal that there is more data about to be queued up, defer sending
struct ulp_send_args {
	struct ulp_con*		c;
	const void*			data;
	size_t				len;
	int					flags;
	ulp_rc_releaser*	release;
	void*				release_cdata;
};
ulp_err ulp_send_(struct ulp_send_args);
#define ulp_send(con, ...) ulp_send_((struct ulp_send_args){.c=(con), __VA_ARGS__})

ulp_err ulp_con_base(struct ulp_con* c, size_t* base);
ulp_err ulp_con_discard_pending(struct ulp_con* c, size_t rewind_to_base);

struct ulp_connect_args {
	struct ulp_con**	c;
	const char*			node;
	const char*			service;
	ulp_parser*			parser;
	void*				cdata;
	int					async;		// Non-blocking connect.  Wait for writable
	ulp_shift_tags*		shift_tags;
	void*				parser_private;
	ulp_rc_releaser*	parser_private_release;
};
ulp_err ulp_connect_(struct ulp_connect_args);
#define ulp_connect(...) ulp_connect_((struct ulp_connect_args){__VA_ARGS__});

void ulp_con_set_cdata(struct ulp_con* c, void* cdata);
void* ulp_con_get_cdata(struct ulp_con* c);

#endif
