#include "int.h"

void ulp_register_hook_cb(struct ulp_dlist* cbs, struct ulp_hook_cb* cb) //<<<
{
	ulp_dlist_prepend(cbs, cb);
}

//>>>
void ulp_deregister_hook_cb(struct ulp_dlist* cbs, struct ulp_hook_cb* cb) //<<<
{
	ulp_dlist_remove(cbs, cb);
}

//>>>
void ulp_call_hooks(struct ulp_dlist* cbs, void* arg) //<<<
{
	struct ulp_hook_cb*		cb = ulp_dlist_head(cbs);

	while (cb) {
		cb->hook_cb(&cb->aux, arg);
		cb = cb->dl.next;
	}
}

//>>>
void ulp_deregister_hook_cbs(struct ulp_dlist* cbs) //<<<
{
	struct ulp_hook_cb*		cb = ulp_dlist_head(cbs);

	while (cb) {
		if (cb->aux.data) {
			ulp_rc_decref(&cb->aux);
			cb->aux.data = NULL;
		}
		cb = cb->dl.next;
	}
}

//>>>

// vim: ts=4 shiftwidth=4 foldmethod=marker foldmarker=<<<,>>>
