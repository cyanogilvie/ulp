#include "int.h"

struct ulp_msg_handler_pool {
	mtx_t					mutex;	// Must hold this lock to access this struct (after init)

	struct ulp_msg_queue*	q;		// Handlers process messages from this queue

	unsigned				avail;	// Count of idle handler threads
	unsigned				busy;	// Count of handler threads currently busy
	unsigned				min;	// Min number of handler threads to keep even if idle
	unsigned				max;	// Max number of handler threads to start

	struct ulp_dlist		idle;	// Linked list of idle handlers (LRU stack) (struct msg_handler)
	struct ulp_dlist		active;	// Linked list of handlers currently processing a message (struct msg_handler)

	ulp_init_handler*		init_handler;
	ulp_free_handler*		free_handler;
	ulp_process_msg*		process_msg;
};

struct ulp_msg_handler {
	struct ulp_dlist				dl;				// Must be first
	struct ulp_msg_handler_pool*	pool;
	struct ulp_msg_queue*			q;
	int								last_returned;	// Seconds since posix epoch this handler last became idle
	void*							handler_data;	// Opaque pointer containing the state of this handler
	thrd_t							handler_thread;
	ulp_process_msg*				process_msg;
};

int handler_thread(void* arg) //<<<
{
	struct ulp_msg_handler*	handler = arg;
	int						running = 1;

	handler->handler_data = handler->pool->init_handler();
	if (handler->handler_data == NULL) {
		fprintf(stderr, "Could not init handler thread\n");
		goto finally;
	}

	while (running) {
		struct ulp_msg*	m = NULL;

		if (thrd_success != mtx_lock(&handler->q->mutex)) {
			goto finally;
		}
		while (handler->q->msgs.head == NULL) {
			if (thrd_success != cnd_wait(&handler->q->msg_avail, &handler->q->mutex)) {
				fprintf(stderr, "msg_handler cnd_wait failed\n");
				running = 0;
				goto unlock;		// continue rather?
			}
		}

		m = ulp_dlist_pop_head(&handler->q->msgs);

unlock:
		if (thrd_success != mtx_unlock(&handler->q->mutex)) {
			goto finally;
		}

		// Process message
		if (m && handler->process_msg(handler->handler_data, m)) {
			goto finally;
		}

		// TODO: Check if we are surplus to requirements (pool->avail > max(pool->min, last_minute_max)), and terminate if so

	}

finally:
	return 0;		// These threads are detached
}

//>>>
ulp_err start_handler(struct ulp_msg_handler_pool* pool) // must have pool->mutex held <<<
{
	ulp_err					err = {NULL, ULP_OK};
	struct ulp_msg_handler*	new_handler = malloc(sizeof *new_handler);

	*new_handler = (struct ulp_msg_handler){
		.dl				= {0},
		.pool			= pool,
		.q				= pool->q,
		.process_msg	= pool->process_msg
	};

	if (thrd_success != thrd_create(&new_handler->handler_thread, &handler_thread, new_handler))
		THROW_ERR(finally, err, "Could not create new handler thread");

	if (thrd_success != thrd_detach(new_handler->handler_thread))
		THROW_ERR(finally, err, "Could not detach handler thread");

finally:
	if (err.msg) {
		if (new_handler) {
			free(new_handler);
			new_handler = NULL;
		}
	}
	return err;
}

//>>>
ulp_err ulp_init_msg_handler_pool_(struct ulp_init_msg_handler_pool_args args) //<<<
{
	ulp_err		err = {NULL, ULP_OK};
	int			locked = 0;

	*args.pool = malloc(sizeof(struct ulp_msg_handler_pool));

	**args.pool = (struct ulp_msg_handler_pool){
		.q				= args.q,
		.min			= args.min,
		.max			= args.max,
		.init_handler	= args.init_handler,
		.free_handler	= args.free_handler,
		.process_msg	= args.process_msg
	};
	struct ulp_msg_handler_pool* pool = *args.pool;

	if (thrd_success != mtx_init(&pool->mutex, mtx_plain))
		THROW_ERR(finally, err, "Could not initialize msg_handler_pool mutex");

	if (thrd_success != mtx_lock(&pool->mutex))
		THROW_ERR(finally, err, "Could not lock msg_handler_pool mutex");
	locked = 1;

	while (pool->avail < pool->min)
		ULP_CHECK(finally, err, start_handler(pool))

finally:
	if (locked)
		if (thrd_success != mtx_unlock(&pool->mutex))
			THROW_ERR(finally, err, "Could not unlock msg_handler_pool mutex");

	return err;
}

//>>>

// vim: ts=4 shiftwidth=4 foldmethod=marker foldmarker=<<<,>>>
