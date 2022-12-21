#ifndef ULP_MSG_HANDLERS_H
#define ULP_MSG_HANDLERS_H

struct ulp_msg {
	struct ulp_dlist_elem	dl;		// Must be first
	struct ulp_con*			con;
	void*					data;	// Message-type specific data
};

typedef void*	(ulp_init_handler)(void);
typedef void	(ulp_free_handler)(void* handler_data);
typedef int		(ulp_process_msg)(void* handler_data, struct ulp_msg* msg);	// Return 0 if ok, else handler thread is terminated

struct ulp_msg_handler_pool;

struct ulp_init_msg_handler_pool_args {
	struct ulp_msg_handler_pool**	pool;
	struct ulp_msg_queue*			q;
	unsigned						min;
	unsigned						max;
	ulp_init_handler*				init_handler;
	ulp_free_handler*				free_handler;
	ulp_process_msg*				process_msg;
};
ulp_err ulp_init_msg_handler_pool_(struct ulp_init_msg_handler_pool_args);
#define ulp_init_msg_handler_pool(p, ...) ulp_init_msg_handler_pool_((struct ulp_init_msg_handler_pool_args){.min=1, .max=1, .pool=(p), __VA_ARGS__})

#endif
