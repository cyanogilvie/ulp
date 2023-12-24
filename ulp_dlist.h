#ifndef ULP_DLIST_H
#define ULP_DLIST_H

struct ulp_dlist_elem {
	void*	next;
	void*	prev;
};

struct ulp_dlist {
	struct ulp_dlist_elem*	head;
	struct ulp_dlist_elem*	tail;
};

static inline void ulp_dlist_init(void* dlistPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	dlist->head = dlist->tail = NULL;
}

//>>>
static inline void ulp_dlist_prepend(void* dlistPtr, void* elemPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	struct ulp_dlist_elem*	elem = elemPtr;

	if (dlist->head) {
		dlist->head->prev = elem;
		elem->next = dlist->head;
		dlist->head = elem;
	} else {
		dlist->head = dlist->tail = elem;
		elem->next = NULL;
		elem->prev = NULL;
	}
}

//>>>
static inline void ulp_dlist_append(void* dlistPtr, void* elemPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	struct ulp_dlist_elem*	elem = elemPtr;

	if (dlist->tail) {
		dlist->tail->next = elem;
		elem->prev = dlist->tail;
		dlist->tail = elem;
	} else {
		dlist->head = dlist->tail = elem;
		elem->next = NULL;
		elem->prev = NULL;
	}
}

//>>>
static inline void ulp_dlist_remove(void* dlistPtr, void* elemPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	struct ulp_dlist_elem*	elem = elemPtr;

	if (elem->prev) {
		struct ulp_dlist_elem*	elem_prev = elem->prev;
		elem_prev->next = elem->next;
	} else {
		// elem was the head
		dlist->head = elem->next;
	}

	if (elem->next) {
		struct ulp_dlist_elem*	elem_next = elem->next;
		elem_next->prev = elem->prev;
	} else {
		// elem was the tail
		dlist->tail = elem->prev;
	}
}

//>>>
static inline void* ulp_dlist_pop_head(void* dlistPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	struct ulp_dlist_elem*	elem = NULL;

	if (dlist->head) {
		elem = dlist->head;

		dlist->head = elem->next;
		if (dlist->head)
			dlist->head->prev = NULL;
		elem->next = NULL;

		if (dlist->tail == elem) dlist->tail = NULL;
	}

	return elem;
}

//>>>
static inline void* ulp_dlist_pop_tail(void* dlistPtr) //<<<
{
	struct ulp_dlist*		dlist = dlistPtr;
	struct ulp_dlist_elem*	elem = NULL;

	if (dlist->tail) {
		elem = dlist->tail;

		dlist->tail = elem->prev;
		elem->prev = NULL;

		if (dlist->head == elem) dlist->head = NULL;
	}

	return elem;
}

//>>>
static inline void* ulp_dlist_head(void* dlistPtr) //<<<
{
	struct ulp_dlist*	dlist = dlistPtr;
	return dlist->head;
}

//>>>
static inline void* ulp_dlist_tail(void* dlistPtr) //<<<
{
	struct ulp_dlist*	dlist = dlistPtr;
	return dlist->tail;
}

//>>>

#endif
