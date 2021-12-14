#ifndef REFCOUNTED_H
#define REFCOUNTED_H

typedef void (releaser)(void* data);

struct rc_thing {
	int64_t		refcount;
	void*		data;
	releaser*	free;
};

static inline void rc_incref(struct rc_thing* thing) //<<<
{
	thing->refcount++;
}

//>>>
static inline void rc_decref(struct rc_thing* thing) //<<<
{
	if (--thing->refcount <= 0 && thing->free)
		thing->free(thing->data);
}

//>>>

#endif
