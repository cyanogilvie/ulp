#ifndef _ULP_OBSTACK_POOL_H
#define _ULP_OBSTACK_POOL_H

enum ulp_obstack_pool_estimate {
	ULP_OBSTACK_POOL_SMALL,			// A few pages
	ULP_OBSTACK_POOL_MEDIUM			// A MB or more
};

struct obstack* ulp_obstack_pool_get(enum ulp_obstack_pool_estimate est);
void ulp_obstack_pool_release(struct obstack* ob);
void ulp_obstack_pool_groom(uint64_t now);
void ulp_obstack_pool_shutdown();

#endif
