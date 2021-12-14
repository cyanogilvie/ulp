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
#include "dlist.h"
#include "obstack_pool.h"
#include "refcounted.h"
#include "cb.h"

struct msg_queue;
struct input;
struct output;

enum parser_status {
	PARSER_STATUS_UNDEF=0,
	PARSER_STATUS_WAITING,
	PARSER_STATUS_OVERFLOW,
	PARSER_STATUS_CLOSE,
	PARSER_STATUS_ERROR
};

typedef enum parser_status (parser)(struct input* in, struct msg_queue* q);

struct msg_queue {
	mtx_t			mutex;
	cnd_t			msg_avail;
	struct dlist	msgs;		// List of struct msg
};

struct msg {
	struct dlist_elem	dl;		// Must be first
	void*				data;	// Message-type specific data
};

typedef void (shift_tags)(struct input* c, size_t shift);

struct mtagpool {
	struct obstack*		ob;
	void*				start;
};

struct mtag {
	struct mtag*		prev;
	ssize_t				dist;
};

struct input {
	unsigned char*	cur;
	unsigned char*	mar;
	unsigned char*	tok;
	unsigned char*	lim;
	ssize_t			remain;
	int				cond;
	int				state;
	struct mtagpool	mtp;
	void*			tags;		// Message-specific container for stags and mtags
	void*			msg;		// Message-specific container for the message being parsed
	shift_tags*		shift_tags;	// Optional cb to shift the message-specific tags
	size_t			buf_size;
	unsigned char*	buf;
};

// Output handlers <<<
enum output_source {
	IO_SOURCE_UNDEF=0,
	IO_SOURCE_BUF,
	IO_SOURCE_STREAM
};

typedef void (free_source)(void* source);		// source is a pointer to the source type struct, like source_buf

struct source_buf {
	free_source*	free;
	void*			data;		// Opaque pointer for source-specific info
	unsigned char*	buf;
	unsigned char*	cur;
	unsigned char*	lim;
};

struct source_stream;
typedef enum stream_status (stream_chunk)(struct source_stream* stream);

struct source_stream {
	free_source*	free;
	void*			data;		// Opaque pointer for source-specific info
	stream_chunk*	next_chunk;
};

struct output {
	struct dlist_elem	dl;		// Must be first
	enum output_source	source;
	union {
		struct source_buf		buf;
		struct source_stream	stream;
	};
};
// Output handlers >>>


int init_msg_queue(struct msg_queue* q);
int start_listen(const char* node, const char* service, parser* parser, struct msg_queue* q);
int ulp_init();

#endif
