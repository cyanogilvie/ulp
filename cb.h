#ifndef CB_H
#define CB_H

typedef void (hook_cb)(struct rc_thing* aux, void* arg);

struct hook_cb {
	struct dlist_elem	dl;		// Must be first
	hook_cb*			hook_cb;
	struct rc_thing		aux;
};

void register_hook_cb(struct dlist* cbs, struct hook_cb* cb);
void deregister_hook_cb(struct dlist* cbs, struct hook_cb* cb);
void deregister_hook_cbs(struct dlist* cbs);
void call_hooks(struct dlist* cbs, void* arg);

#endif
