#include "int.h"

typedef void*	(init_handler)(void);
typedef void	(free_handler)(void* handler_data);
typedef int		(process_msg)(void* handler_data, struct msg* msg);	// Return 0 if ok, else handler thread is terminated

struct msg_handler_pool {
	mtx_t				mutex;	// Must hold this lock to access this struct (after init)

	struct msg_queue*	q;		// Handlers process messages from this queue

	unsigned			avail;	// Count of idle handler threads
	unsigned			busy;	// Count of handler threads currently busy
	unsigned			min;	// Min number of handler threads to keep even if idle
	unsigned			max;	// Max number of handler threads to start

	struct dlist		idle;	// Linked list of idle handlers (LRU stack) (struct msg_handler)
	struct dlist		active;	// Linked list of handlers currently processing a message (struct msg_handler)

	init_handler*		init_handler;
	free_handler*		free_handler;
	process_msg*		process_msg;
};

struct msg_handler {
	struct dlist				dl;				// Must be first
	struct msg_handler_pool*	pool;
	struct msg_queue*			q;
	int							last_returned;	// Seconds since posix epoch this handler last became idle
	void*						handler_data;	// Opaque pointer containing the state of this handler
	thrd_t						handler_thread;
	process_msg*				process_msg;
};


int handler_thread(void* arg) //<<<
{
	struct msg_handler*	handler = arg;
	int					running = 1;

	handler->handler_data = handler->pool->init_handler();
	if (handler->handler_data == NULL) {
		fprintf(stderr, "Could not init handler thread\n");
		goto finally;
	}

	while (running) {
		if (thrd_success != mtx_lock(&handler->q->mutex)) {
			goto finally;
		}
		while (handler->q->msgs.head == NULL) {
			if (thrd_success != cnd_wait(&handler->q->msg_avail, &handler->q->mutex)) {
				fprintf(stderr, "msg_handler cnd_wait failed\n");
				running = 0;
				goto unlock;
			}
		}

		struct msg* m = dlist_pop_head(&handler->q->msgs);

unlock:
		if (thrd_success != mtx_unlock(&handler->q->mutex)) {
			goto finally;
		}

		// Process message
		if (handler->process_msg(handler->handler_data, m)) {
			goto finally;
		}

		// TODO: Check if we are surplus to requirements (pool->avail > max(pool->min, last_minute_max)), and terminate if so

	}

finally:
	return 0;		// These threads are detached
}

//>>>
int start_handler(struct msg_handler_pool* pool) // must have pool->mutex held <<<
{
	int					rc = 0;
	struct msg_handler*	new_handler = malloc(sizeof *new_handler);

	*new_handler = (struct msg_handler){
		.dl				= {0},
		.pool			= pool,
		.q				= pool->q,
		.process_msg	= pool->process_msg
	};

	if (thrd_success != thrd_create(&new_handler->handler_thread, &handler_thread, new_handler)) {
		rc = 1;
		goto finally;
	}
	if (thrd_success != thrd_detach(new_handler->handler_thread)) {
		rc = 1;
		goto finally;
	}

finally:
	if (rc) {
		if (new_handler) {
			free(new_handler);
			new_handler = NULL;
		}
	}
	return rc;
}

//>>>
int init_msg_handler_pool(struct msg_handler_pool* pool, struct msg_queue* q, unsigned min, unsigned max, init_handler* init_handler, free_handler* free_handler, process_msg* process_msg) //<<<
{
	int		rc = 0;

	*pool = (struct msg_handler_pool){
		.q				= q,
		.min			= min,
		.max			= max,
		.init_handler	= init_handler,
		.free_handler	= free_handler,
		.process_msg	= process_msg
	};

	if (thrd_success != mtx_init(&pool->mutex, mtx_plain)) {
		fprintf(stderr, "Could not initialize msg_handler_pool mutex\n");
		rc = 1;
		goto finally;
	}

	if (thrd_success != mtx_lock(&pool->mutex)) {
		fprintf(stderr, "Could not lock msg_handler_pool mutex\n");
		rc = 1;
		goto finally;
	}

	while (pool->avail < pool->min) {
		if (start_handler(pool)) {
			rc = 1;
			break;
		}
	}

	if (thrd_success != mtx_unlock(&pool->mutex)) {
		fprintf(stderr, "Could not unlock msg_handler_pool mutex\n");
		rc = 1;
		goto finally;
	}

finally:
	return rc;
}

//>>>

// vim: ts=4 shiftwidth=4 foldmethod=marker foldmarker=<<<,>>>
