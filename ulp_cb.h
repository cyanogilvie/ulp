#ifndef ULP_CB_H
#define ULP_CB_H

typedef void (ulp_hook_cb)(struct ulp_rc_thing* aux, void* arg);

struct ulp_hook_cb {
	struct ulp_dlist_elem	dl;		// Must be first
	ulp_hook_cb*			hook_cb;
	struct ulp_rc_thing		aux;
};

void ulp_register_hook_cb(struct ulp_dlist* cbs, struct ulp_hook_cb* cb);
void ulp_deregister_hook_cb(struct ulp_dlist* cbs, struct ulp_hook_cb* cb);
void ulp_deregister_hook_cbs(struct ulp_dlist* cbs);
void ulp_call_hooks(struct ulp_dlist* cbs, void* arg);

#endif
