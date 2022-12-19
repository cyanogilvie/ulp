#include "int.h"

struct obstack_slot {
	struct obstack			ob;				// Must be first
	struct obstack_slot*	next;
	uint64_t				last_returned;
	void*					first_object;
};

struct obstack_pool {
	struct obstack_slot*	free;
	int						avail;
	uint64_t				oldest;
};

thread_local struct obstack_pool	t_obstack = {0};

struct obstack* ulp_obstack_pool_get(enum ulp_obstack_pool_estimate est) //<<<
{
	struct obstack_slot*	slot;

	if (t_obstack.free) {
		slot = t_obstack.free;
		t_obstack.free = slot->next;
		slot->next = NULL;
		t_obstack.avail--;
		return (struct obstack*)slot;
	}

	slot = (struct obstack_slot*)obstack_chunk_alloc(sizeof(struct obstack_slot));
	switch (est) {
		case ULP_OBSTACK_POOL_SMALL:
			obstack_begin(&slot->ob, 12288-32);
			break;
		case ULP_OBSTACK_POOL_MEDIUM:
			obstack_begin(&slot->ob, 1048576-32);
			break;
		default:
			obstack_begin(&slot->ob, 12288-32);
	}
	slot->first_object = obstack_alloc(&slot->ob, 1);	// Record the first obstack allocation so we can release it when the obstack is returned

	return (struct obstack*)slot;
}

//>>>
void ulp_obstack_pool_release(struct obstack* ob) //<<<
{
	struct obstack_slot*	slot = (struct obstack_slot*)ob;
	const uint64_t			now = cycles();

	obstack_free(&slot->ob, slot->first_object);
	slot->first_object = obstack_alloc(&slot->ob, 1);

	slot->next = t_obstack.free;
	slot->last_returned = now;

	if (t_obstack.free == NULL)
		t_obstack.oldest = now;

	t_obstack.free = slot;
	t_obstack.avail++;

	ulp_obstack_pool_groom(now);
}

//>>>
void ulp_obstack_pool_groom(uint64_t now) //<<<
{
	const int		min_pool = 10;		// Keep at least this many slots in the pool
	const uint64_t	horizon = now - 30*1000000000ULL;	// Free excess slots that are older than this

	if (
			t_obstack.avail > min_pool &&
			t_obstack.oldest < horizon
	) {
		int						i = 0;
		struct obstack_slot*	s = t_obstack.free;
		struct obstack_slot*	p = NULL;

		while (s) {
			if (++i > min_pool && s->last_returned > horizon) {
				// Free this slot and all trailing ones (which are guaranteed to be older)
				p->next = NULL;
				t_obstack.oldest = p->last_returned;
				t_obstack.avail = i-1;
				while (s) {
					obstack_free(&s->ob, NULL);
					s = s->next;
				}
				break;
			}
			p = s;
			s = s->next;
		}
	}
}

//>>>

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
