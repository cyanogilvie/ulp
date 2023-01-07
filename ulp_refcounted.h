#ifndef ULP_REFCOUNTED_H
#define ULP_REFCOUNTED_H

typedef void (ulp_rc_releaser)(void* data);

struct ulp_rc_thing {
	int64_t				refcount;
	void*				data;
	ulp_rc_releaser*	free;
};

static inline void ulp_rc_incref(void* _thing) //<<<
{
	struct ulp_rc_thing*	thing = _thing;
	thing->refcount++;
}

//>>>
static inline void ulp_rc_decref(void* _thing) //<<<
{
	struct ulp_rc_thing*	thing = _thing;
	if (--thing->refcount <= 0 && thing->free)
		thing->free(thing->data);
}

//>>>

#endif
