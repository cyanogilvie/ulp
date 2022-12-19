DESTDIR =
PREFIX = /usr/local

CC = gcc
RE2C = /home/cyan/git/evhttp/local/bin/re2c
AR = ar
RANLIB = ranlib
VALGRIND = valgrind

CFLAGS = -std=gnu17 -Wno-missing-braces -I. -Werror
CFLAGS_DEBUG = -ggdb3 -Og -DPURIFY
CFLAGS_OPTIMIZE = -ggdb3 -Ofast -march=native -mtune=native -flto
LDFLAGS =
PGO =
RE2COPTS = --case-ranges -W -Wno-nondeterministic-tags
VALGRINDARGS_EXTRA = 
VALGRINDARGS	= --tool=memcheck --num-callers=8 --leak-resolution=high \
		  --leak-check=yes -v --suppressions=suppressions --keep-debuginfo=yes \
		  --trace-children=yes $(VALGRINDARGS_EXTRA)

RE2C_OBJS = http.o
C_OBJS = accept.o msg_handlers.o obstack_pool.o cb.o
OBJS = $(C_OBJS)

all: libulp.a libulp_dbg.a blocking_accept dbg_blocking_accept

generated/http.c: http.re Makefile
	mkdir -p generated
	$(RE2C) $(RE2COPTS) --storable-state --conditions --type-header $(subst .c,.h,$@) $< -o $@
	chmod a-w generated/http.c generated/http.h

$(C_OBJS): %.o: %.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_OPTIMIZE) $(PGO) -o $@ $<

$(RE2C_OBJS): %.o: generated/%.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_OPTIMIZE) $(PGO) -o $@ $<

libulp.a: $(OBJS)
	echo "create $@\n $(foreach mod,$(OBJS),addmod $(mod)\n) save\n end\n" | $(AR) -M
	$(RANLIB) $@

blocking_accept: test.c libulp.a http.o
	$(CC) -o $@ $(CFLAGS) $(CFLAGS_OPTIMIZE) test.c $(LDFLAGS) $(PGO) http.o -L. -lulp

$(addprefix dbg_,$(C_OBJS)): dbg_%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_DEBUG) $(PGO) -o $@ $<

$(addprefix dbg_,$(RE2C_OBJS)): dbg_%.o: generated/%.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_DEBUG) $(PGO) -o $@ $<

libulp_dbg.a: $(addprefix dbg_,$(OBJS))
	echo "create $@\n $(foreach mod,$(addprefix dbg_,$(OBJS)),addmod $(mod)\n) save\n end\n" | $(AR) -M
	$(RANLIB) $@

dbg_blocking_accept: test.c libulp_dbg.a dbg_http.o
	$(CC) -o $@ $(CFLAGS) $(CFLAGS_DEBUG) test.c $(LDFLAGS) $(PGO) dbg_http.o -L. -lulp_dbg

vim-gdb: dbg_blocking_accept tags
	vim -c "set number" -c "set mouse=a" -c "set foldlevel=100" -c "Termdebug -ex set\ print\ pretty\ on --args ./dbg_blocking_accept" -c "2windo set nonumber" -c "1windo set nonumber" test.c

valgrind: dbg_testserv
	$(VALGRIND) $(VALGRINDARGS) ./$<

tags: *.re *.h *.c Makefile
	-ctags-exuberant --recurse=yes --langmap=c:+.re $(addprefix --exclude=,$(subst .o,.h,$(RE2C_OBJS))) $(addprefix --exclude=,$(subst .o,.c,$(RE2C_OBJS))) *.c *.h *.re

clean:
	-rm core blocking_accept dbg_blocking_accept $(OBJS) $(addprefix dbg_,$(OBJS)) tags generated

install: libulp.a
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include
	cp libulp.a $(DESTDIR)$(PREFIX)/lib/
	cp ulp.h ulp_dlist.h ulp_obstack_pool.h ulp_refcounted.h ulp_cb.h $(DESTDIR)$(PREFIX)/include/

.PHONY: all clean vim-gdb valgrind install
