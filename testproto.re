#include "ulp.h"
#include "generated/testproto.h"

extern int mainloop_wakeup_fd;
extern int g_mainloop_running;

/*!header:re2c:on */
typedef void (testproto_got_packet)(struct ulp_con* c, const char* packet, size_t len);

struct testproto_tags {
	/*!stags:re2c format = "\tunsigned char*\t\t@@{tag};\n"; */
	/*!mtags:re2c format = "\tstruct mtag*\t\t@@{tag};\n"; */
};

struct testproto_private {
	struct obstack*			ob;
	struct testproto_tags	tags;
	int						state;

	/*
	int					cond;
	struct ulp_mtagpool	mtp;
	void*				msg;		// Message-specific container for the message being parsed
	*/
};

enum ulp_parser_status parse_testproto(struct ulp_con* con, struct ulp_input* in, void* cdata);
/*!header:re2c:off */

void shift_testproto_tags(struct ulp_input* in, size_t shift) //
{
	struct testproto_private*	p = in->parser_private;
	/*!stags:re2c format = "\tif (p->tags.@@) p->tags.@@ -= shift;\n"; */
}

//>>>
struct testproto_private* init_testproto_parser() //<<<
{
	struct obstack*				p_ob = ulp_obstack_pool_get(ULP_OBSTACK_POOL_SMALL);
	struct testproto_private*	p = obstack_alloc(p_ob, sizeof(struct testproto_private));

	*p = (struct testproto_private){
		.ob		= p_ob
	};

	return p;
}

//>>>
void free_testproto_parser(void* parser_private) //<<<
{
	struct testproto_private*	p = parser_private;

	if (p->ob)
		ulp_obstack_pool_release(p->ob);
}

//>>>
enum ulp_parser_status parse_testproto(struct ulp_con* c, struct ulp_input*const in, void* cdata) //<<<
{
	testproto_got_packet*	cb = cdata;
	unsigned char			yych;
	unsigned char			*e;

	if (in->parser_private == NULL) {
		in->parser_private = init_testproto_parser();
		in->parser_private_release = free_testproto_parser;
		in->shift_tags = shift_testproto_tags;
	}
	struct testproto_private*const	p = in->parser_private;
	struct testproto_tags*const		tags = &p->tags;

loop:
	/*!re2c
		re2c:api:style = free-form;
		re2c:tags:expression   = "tags->@@";
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

		"quit" eol {
			ulp_send(c, "bye\n", 4);
			return ULP_PARSER_STATUS_CLOSE;
		}

		"stop" eol {
			ulp_send(c, "stopping\n", sizeof("stopping\n"), 0, NULL, NULL);
			g_mainloop_running = 0;
			if (-1 == write(mainloop_wakeup_fd, &(uint64_t){1}, 8))
				perror("write on mainloop_wakeup eventfd");
			return ULP_PARSER_STATUS_CLOSE;
		}

        packet {
			(cb)(c, (const char*)in->tok, e - in->tok);
			in->tok = in->cur;
			p->state = 0;
			goto loop;
		}
	*/

	return ULP_PARSER_STATUS_ERROR;
}

//>>>

// vim: ft=c foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
