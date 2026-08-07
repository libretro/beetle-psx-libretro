ROOT := ../..
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fwrapv
CPPFLAGS := -I$(ROOT) -I$(ROOT)/pgxp -I$(ROOT)/libretro-common/include

# Links the real PGXP sources, so the code under test is the shipping code
# rather than a copy of it.
PGXP_SRC := $(ROOT)/pgxp/pgxp_gpu.c $(ROOT)/pgxp/pgxp_gte.c \
            $(ROOT)/pgxp/pgxp_cpu.c $(ROOT)/pgxp/pgxp_mem.c \
            $(ROOT)/pgxp/pgxp_main.c $(ROOT)/pgxp/pgxp_value.c

all: oracle transport range

oracle: oracle.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ oracle.c $(PGXP_SRC) stub_log.c -lm

range: range.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ range.c $(PGXP_SRC) stub_log.c -lm

transport: transport.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ transport.c $(PGXP_SRC) stub_log.c -lm

check: oracle transport range
	./oracle
	@echo
	./transport
	@echo
	./range

clean:
	rm -f oracle transport range

.PHONY: all check clean
