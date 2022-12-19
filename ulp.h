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
#include "ulp_dlist.h"
#include "ulp_obstack_pool.h"
#include "ulp_refcounted.h"
#include "ulp_cb.h"

struct ulp_con;
struct ulp_msg_queue;
struct ulp_input;
struct ulp_output;
struct ulp_listen_handle;

enum ulp_parser_status {
	ULP_PARSER_STATUS_UNDEF=0,
	ULP_PARSER_STATUS_WAITING,
	ULP_PARSER_STATUS_OVERFLOW,
	ULP_PARSER_STATUS_CLOSE,
	ULP_PARSER_STATUS_ERROR
};

typedef enum ulp_parser_status (ulp_parser)(struct ulp_con* con, struct ulp_input* in, void* cdata);

struct ulp_msg_queue {
	mtx_t				mutex;
	cnd_t				msg_avail;
	struct ulp_dlist	msgs;		// List of struct msg
};

struct ulp_msg {
	struct ulp_dlist_elem	dl;		// Must be first
	struct ulp_con*			con;
	void*					data;	// Message-type specific data
};

typedef void (ulp_shift_tags)(struct ulp_input* c, size_t shift);

struct ulp_mtagpool {
	struct obstack*		ob;
	void*				start;
};

struct ulp_mtag {
	struct ulp_mtag*	prev;
	ssize_t				dist;
};

static inline void ulp_mtag(struct ulp_mtag** pmt, const unsigned char* b, const unsigned char* t, struct ulp_mtagpool* mtp) //<<<
{
	struct ulp_mtag*    mt = obstack_alloc(mtp->ob, sizeof(struct ulp_mtag));
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
	ssize_t				remain;
	int					cond;
	int					state;
	struct ulp_mtagpool	mtp;
	void*				tags;		// Message-specific container for stags and mtags
	void*				msg;		// Message-specific container for the message being parsed
	ulp_shift_tags*		shift_tags;	// Optional cb to shift the message-specific tags
	size_t				buf_size;
	unsigned char*		buf;
};

// Output handlers <<<
enum ulp_output_source {
	ULP_IO_SOURCE_UNDEF=0,
	ULP_IO_SOURCE_BUF,
	ULP_IO_SOURCE_STREAM
};

typedef void (ulp_free_source)(void* source);	// source is a pointer to the source type struct, like source_buf

struct ulp_source_buf {
	ulp_free_source*	free;
	void*				data;		// Opaque pointer for source-specific info
	unsigned char*		buf;
	unsigned char*		cur;
	unsigned char*		lim;
};

struct ulp_source_stream;
typedef enum ulp_stream_status (ulp_stream_chunk)(struct ulp_source_stream* stream);

struct ulp_source_stream {
	ulp_free_source*	free;
	void*				data;		// Opaque pointer for source-specific info
	ulp_stream_chunk*	next_chunk;
};

struct ulp_output {
	struct ulp_dlist_elem	dl;		// Must be first
	enum ulp_output_source	source;
	union {
		struct ulp_source_buf		buf;
		struct ulp_source_stream	stream;
	};
};
// Output handlers >>>


int ulp_init();
struct ulp_listen_handle* ulp_start_listen(const char* node, const char* service, ulp_parser* parser, void* cdata);
int ulp_stop_listen(struct ulp_listen_handle* lh);
void ulp_close_con(struct ulp_con* c);
int ulp_init_msg_queue(struct ulp_msg_queue* q);

#endif
