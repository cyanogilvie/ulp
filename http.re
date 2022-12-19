#include "ulp.h"
#include "generated/http.h"

/*!header:re2c:on */
struct http_msg {
	struct obstack*		ob;
};

struct http_tags {
	/*!stags:re2c format = "\tunsigned char*\t\t@@{tag};\n"; */
	/*!mtags:re2c format = "\tstruct mtag*\t\t@@{tag};\n"; */
};

enum ulp_parser_status parse_http(struct ulp_con* con, struct ulp_input* in, void* cdata);
/*!header:re2c:off */

void shift_http_tags(struct ulp_input* in, size_t shift) //
{
	/*!stags:re2c format = "\tif (in->@@) in->@@ -= shift;\n"; */
}

//>>>
void init_http_parser(struct ulp_input* in) //<<<
{
	struct obstack*		msg_ob = ulp_obstack_pool_get(ULP_OBSTACK_POOL_SMALL);
	struct http_msg*	msg = in->msg = obstack_alloc(msg_ob, sizeof(struct http_msg));

	*msg = (struct http_msg){0};
	msg->ob = msg_ob;

	in->tags = obstack_alloc(msg->ob, sizeof(struct http_tags));
	in->shift_tags = &shift_http_tags;
}

//>>>
void free_http_parser(struct ulp_input* in) //<<<
{
	if (in->msg) {
		struct http_msg*	msg = in->msg;
		in->tags = NULL;
		ulp_obstack_pool_release(msg->ob);
		in->msg = NULL;
	}
}

//>>>
enum ulp_parser_status parse_http(struct ulp_con* c, struct ulp_input* in, void* cdata) //<<<
{
	if (in->msg == NULL) init_http_parser(in);

	return ULP_PARSER_STATUS_WAITING;
}

//>>>

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
