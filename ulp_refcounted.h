#ifndef ULP_REFCOUNTED_H
#define ULP_REFCOUNTED_H

typedef void (ulp_rc_releaser)(void* data);

struct ulp_rc_thing {
	int64_t				refcount;
	void*				data;
	ulp_rc_releaser*	free;
};

static inline void* ulp_rc_incref(void* _thing) //<<<
{
	struct ulp_rc_thing*	thing = _thing;
	thing->refcount++;
	return thing;
}

//>>>
static inline void ulp_rc_decref(void* _thing) //<<<
{
	struct ulp_rc_thing*	thing = _thing;
	if (--thing->refcount <= 0 && thing->free)
		thing->free(thing->data ? thing->data : thing);
}

//>>>
static inline void ulp_rc_replace(void* _target, void* _replacement) //<<<
{
	struct ulp_rc_thing**	target		= _target;
	struct ulp_rc_thing*	replacement	= _replacement;
	struct ulp_rc_thing*	old = *target;

	*target = replacement;
	if (*target) ulp_rc_incref(*target);
	if (old) ulp_rc_decref(old);
}

//>>>

#endif
