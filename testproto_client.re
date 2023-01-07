#include "ulp.h"
#include "generated/testproto_client.h"

/*!header:re2c:on */ //<<<
struct testproto_client_tags {
	/*!stags:re2c format = "\tunsigned char*\t\t@@{tag};\n"; */
	/*!mtags:re2c format = "\tstruct mtag*\t\t@@{tag};\n"; */
};

struct testproto_client_private {
	struct obstack*					ob;
	struct testproto_client_tags	tags;
	int								state;
	unsigned char					yych;
	unsigned int					yyaccept;

	/*
	int					cond;
	struct ulp_mtagpool	mtp;
	void*				msg;		// Message-specific container for the message being parsed
	*/
};

enum ulp_parser_status parse_testproto_client(struct ulp_con* con, struct ulp_input* in, void* cdata);
void shift_testproto_client_tags(struct ulp_input* in, size_t shift);
void free_testproto_client_parser(void* parser_private);
struct testproto_client_private* init_testproto_client_parser();
/*!header:re2c:off */ //>>>

void shift_testproto_client_tags(struct ulp_input* in, size_t shift) //<<<
{
	struct testproto_client_private*	p = in->parser_private;
	/*!stags:re2c format = "\tif (p->tags.@@) p->tags.@@ -= shift;\n"; */
}

//>>>
struct testproto_client_private* init_testproto_client_parser() //<<<
{
	struct obstack*						p_ob = ulp_obstack_pool_get(ULP_OBSTACK_POOL_SMALL);
	struct testproto_client_private*	p = obstack_alloc(p_ob, sizeof *p);

	*p = (struct testproto_client_private){
		.ob		= p_ob
	};

	return p;
}

//>>>
void free_testproto_client_parser(void* parser_private) //<<<
{
	struct testproto_client_private*	p = parser_private;
	printf("In free_testproto_client_parser\n");

	if (p->ob)
		ulp_obstack_pool_release(p->ob);
}

//>>>
enum ulp_parser_status parse_testproto_client(struct ulp_con* c, struct ulp_input*const in, void* cdata) //<<<
{
	struct testproto_client_private*const	p = in->parser_private;
	struct ulp_msg_queue*					q = cdata;
	struct testproto_client_tags*const		tags = &p->tags;
	unsigned char							*e;

loop:
	/*!re2c
		re2c:api:style = free-form;
		re2c:tags:expression   = "tags->@@";
		re2c:variable:yych     = "p->yych";
		re2c:variable:yyaccept = "p->yyaccept";
        re2c:define:YYCTYPE    = "unsigned char";
        re2c:define:YYCURSOR   = "in->cur";
        re2c:define:YYMARKER   = "in->mar";
        re2c:define:YYLIMIT    = "in->lim";
        re2c:define:YYGETSTATE = "p->state";
        re2c:define:YYSETSTATE = "p->state = @@;";
        re2c:define:YYFILL     = "return ULP_PARSER_STATUS_WAITING;";
        re2c:eof = 0;

		eol		= "\r"? "\n";
        packet	= [^\x0a\x0d]* @e eol;

        *      { return ULP_PARSER_STATUS_ERROR; }
        $      { return ULP_PARSER_STATUS_WAITING; }

        packet {
			//fprintf(stderr, "parse_testproto_client got packet, posting to queue: <%.*s>\n", (int)(e-in->tok), in->tok);
			ulp_msg_queue_post(q, .c=c, .data=strndup((char*)in->tok, e - in->tok), .free=free);

			in->tok = in->cur;
			p->state = 0;
			goto loop;
		}
	*/

	return ULP_PARSER_STATUS_ERROR;
}

//>>>

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
