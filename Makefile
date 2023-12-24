DESTDIR =
PREFIX = /usr/local

VER = 0.2

CC = gcc
RE2C = local/bin/re2c
AR = ar
RANLIB = ranlib
VALGRIND = valgrind

CFLAGS = -std=gnu17 -fPIC -Wall -Wextra -Werror -Wpointer-arith -Wno-missing-braces -Wno-missing-field-initializers -Wno-unused-parameter -I.
CFLAGS_DEBUG = -ggdb3 -Og -DPURIFY
CFLAGS_OPTIMIZE = -ggdb3 -Ofast -march=native -mtune=native -flto
CFLAGS_PROFILE = -pg
LDFLAGS =
PGO =
RE2COPTS = --case-ranges --storable-state --tags -W -Wno-nondeterministic-tags
VALGRINDARGS_EXTRA = 
VALGRINDARGS	= --tool=memcheck --num-callers=8 --leak-resolution=high \
		  --leak-check=yes -v --suppressions=suppressions --keep-debuginfo=yes \
		  --trace-children=yes $(VALGRINDARGS_EXTRA)
CACHEGRINDARGS	= --tool=cachegrind --cachegrind-out-file=cachegrind.out --cache-sim=yes --branch-sim=no

RE2C_OBJS = testproto.o testproto_client.o test.o
C_OBJS = accept.o msg_handlers.o obstack_pool.o cb.o
OBJS = $(C_OBJS)

all: libulp.a libulp_dbg.a blocking_accept dbg_blocking_accept

local/bin/re2c:
	mkdir -p local/bin
	git submodule update --init --recommend-shallow --checkout --depth 1 --single-branch tools/re2c
	(cd tools/re2c; ./autogen.sh && ./configure --disable-golang --disable-rust --disable-benchmarks --disable-benchmarks-regenerate && make -j 4)
	cp tools/re2c/re2c local/bin

generated/%.c: %.re Makefile $(RE2C)
	mkdir -p generated
	$(RE2C) $(RE2COPTS) --storable-state --conditions --type-header $(subst .c,.h,$@) $< -o $@
	chmod a-w $@ $(subst .c,.h,$@)

$(C_OBJS): %.o: %.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_OPTIMIZE) $(PGO) -o $@ $<

$(RE2C_OBJS): %.o: generated/%.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_OPTIMIZE) $(PGO) -o $@ $<

libulp.a: $(OBJS)
	echo "create $@\n $(foreach mod,$(OBJS),addmod $(mod)\n) save\n end\n" | $(AR) -M
	$(RANLIB) $@

libulp$(VER).so: $(OBJS)
	$(CC) -o $@ --shared -fPIC $(OBJS)

blocking_accept: $(RE2C_OBJS) libulp.a
	$(CC) -o $@ $(CFLAGS) $(CFLAGS_OPTIMIZE) test.o $(LDFLAGS) $(PGO) testproto.o testproto_client.o -L. -lulp


# Debug
$(addprefix dbg_,$(C_OBJS)): dbg_%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_DEBUG) $(PGO) -o $@ $<

$(addprefix dbg_,$(RE2C_OBJS)): dbg_%.o: generated/%.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_DEBUG) $(PGO) -o $@ $<

libulp_dbg.a: $(addprefix dbg_,$(OBJS))
	echo "create $@\n $(foreach mod,$(addprefix dbg_,$(OBJS)),addmod $(mod)\n) save\n end\n" | $(AR) -M
	$(RANLIB) $@

dbg_blocking_accept: $(addprefix dbg_,$(RE2C_OBJS)) libulp_dbg.a
	$(CC) -o $@ $(CFLAGS) $(CFLAGS_DEBUG) test.o $(LDFLAGS) $(PGO) dbg_testproto.o dbg_testproto_client.o -L. -lulp_dbg


# Profile
$(addprefix prof_,$(C_OBJS)): prof_%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_PROFILE) $(PGO) -o $@ $<

$(addprefix prof_,$(RE2C_OBJS)): prof_%.o: generated/%.c Makefile
	$(CC) -c $(CFLAGS) $(CFLAGS_PROFILE) $(PGO) -o $@ $<

libulp_prof.a: $(addprefix prof_,$(OBJS))
	echo "create $@\n $(foreach mod,$(addprefix prof_,$(OBJS)),addmod $(mod)\n) save\n end\n" | $(AR) -M
	$(RANLIB) $@

prof_blocking_accept: $(addprefix prof_,$(RE2C_OBJS)) libulp_prof.a
	$(CC) -o $@ $(CFLAGS) $(CFLAGS_PROFILE) test.o $(LDFLAGS) $(PGO) prof_testproto.o prof_testproto_client.o -L. -lulp_prof


vim-gdb: dbg_blocking_accept tags
	vim -c "set number" -c "set mouse=a" -c "set foldlevel=100" -c "Termdebug -ex set\ print\ pretty\ on --args ./dbg_blocking_accept listen(0.0.0.0,1234) listen(/tmp/uds.sock) testconnect(localhost,1234,10,1000) testconnect(/tmp/uds.sock,10,1000) quit" -c "2windo set nonumber" -c "1windo set nonumber" test.re
	#vim -c "set number" -c "set mouse=a" -c "set foldlevel=100" -c "Termdebug -ex set\ print\ pretty\ on --args ./dbg_blocking_accept listen(/tmp/uds.sock) testconnect(/tmp/uds.sock,10,1000) quit" -c "2windo set nonumber" -c "1windo set nonumber" test.re

test: blocking_accept
	./$< 'listen(0.0.0.0,1234)' 'testconnect(localhost,1234,10000,10000)' 'quit'
	./$< 'listen(/tmp/uds.sock)' 'testconnect(/tmp/uds.sock,10000,10000)' 'quit'

valgrind: dbg_blocking_accept
	#$(VALGRIND) $(VALGRINDARGS) ./$< 'listen(0.0.0.0,1234)' 'testconnect(localhost,1234,10,100000)' 'quit'
	$(VALGRIND) $(VALGRINDARGS) ./$< 'listen(0.0.0.0,1234)' 'listen(/tmp/uds.sock)' 'testconnect(localhost,1234,10,100000)' 'testconnect(/tmp/uds.sock,10,100000)' 'quit'
	#$(VALGRIND) $(VALGRINDARGS) ./$< 'listen(0.0.0.0,1234)'

cachegrind: blocking_accept
	-rm cachegrind.out
	#$(VALGRIND) $(CACHEGRINDARGS) ./$< 'listen(0.0.0.0,1234)' 'testconnect(localhost,1234,1000,100000)' 'quit'
	$(VALGRIND) $(CACHEGRINDARGS) ./$< 'listen(/tmp/uds.sock)' 'testconnect(/tmp/uds.sock,1000,100000)' 'quit'

profile: prof_blocking_accept
	./$< 'listen(0.0.0.0,1234)' 'testconnect(localhost,1234,100000,1000)' 'quit'
	./$< 'listen(/tmp/uds.sock)' 'testconnect(/tmp/uds.sock,100000,1000)' 'quit'

perf: blocking_accept
	sudo perf record ./$< 'listen(/tmp/uds.sock)' 'testconnect(/tmp/uds.sock,100000,1000)' 'quit'

perfstats: blocking_accept
	sudo perf stat ./$< 'listen(/tmp/uds.sock)' 'testconnect(/tmp/uds.sock,100000,1000)' 'quit'

tags: *.re *.h *.c Makefile
	-ctags-exuberant --recurse=yes --langmap=c:+.re $(addprefix --exclude=,$(subst .o,.h,$(RE2C_OBJS))) $(addprefix --exclude=,$(subst .o,.c,$(RE2C_OBJS))) *.c *.h *.re

clean:
	-rm -f core blocking_accept dbg_blocking_accept prof_blocking_accept libulp.a libulp*.so $(OBJS) $(addprefix dbg_,$(OBJS)) $(addprefix prof_,$(OBJS)) tags generated/* cachegrind.out gmon.out

install: libulp.a libulp$(VER).so
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include
	cp libulp.a $(DESTDIR)$(PREFIX)/lib/
	cp libulp$(VER).so $(DESTDIR)$(PREFIX)/lib/
	cp ulp.h ulp_dlist.h ulp_msg_handlers.h ulp_obstack_pool.h ulp_refcounted.h ulp_cb.h $(DESTDIR)$(PREFIX)/include/

uninstall:
	-rm -f $(DESTDIR)$(PREFIX)/lib/libulp.a
	-rm -f $(DESTDIR)$(PREFIX)/lib/libulp*.so
	-rm -f $(DESTDIR)$(PREFIX)/include/ulp*.h

.PHONY: all clean vim-gdb valgrind cachegrind install uninstall test profile perf perfstats
