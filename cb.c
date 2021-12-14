#include "int.h"

void register_hook_cb(struct dlist* cbs, struct hook_cb* cb) //<<<
{
	dlist_prepend(cbs, cb);
}

//>>>
void deregister_hook_cb(struct dlist* cbs, struct hook_cb* cb) //<<<
{
	dlist_remove(cbs, cb);
}

//>>>
void call_hooks(struct dlist* cbs, void* arg) //<<<
{
	struct hook_cb*		cb = dlist_head(cbs);

	while (cb) {
		cb->hook_cb(&cb->aux, arg);
		cb = cb->dl.next;
	}
}

//>>>
void deregister_hook_cbs(struct dlist* cbs) //<<<
{
	struct hook_cb*		cb = dlist_head(cbs);

	while (cb) {
		if (cb->aux.data) {
			rc_decref(&cb->aux);
			cb->aux.data = NULL;
		}
		cb = cb->dl.next;
	}
}

//>>>

// vim: ts=4 shiftwidth=4 foldmethod=marker foldmarker=<<<,>>>
