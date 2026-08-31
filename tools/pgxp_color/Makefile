ROOT := ../..
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fwrapv
# PGXP_DIAG=1 so the counters the range harness reads are actually compiled
# in; they are absent from a default (shipping) build by design.
CPPFLAGS := -DPGXP_DIAG=1 -I$(ROOT) -I$(ROOT)/pgxp -I$(ROOT)/libretro-common/include

# Links the real PGXP sources, so the code under test is the shipping code
# rather than a copy of it.
PGXP_SRC := $(ROOT)/pgxp/pgxp_gpu.c $(ROOT)/pgxp/pgxp_gte.c \
            $(ROOT)/pgxp/pgxp_cpu.c $(ROOT)/pgxp/pgxp_mem.c \
            $(ROOT)/pgxp/pgxp_main.c $(ROOT)/pgxp/pgxp_value.c

all: oracle transport range compatibility

oracle: oracle.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ oracle.c $(PGXP_SRC) stub_log.c -lm

range: range.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ range.c $(PGXP_SRC) stub_log.c -lm

transport: transport.c $(PGXP_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ transport.c $(PGXP_SRC) stub_log.c -lm

compatibility: compatibility.c $(ROOT)/libretro_game_database.c \
      $(ROOT)/input_compatibility.c \
		$(ROOT)/database/psx_games.inc
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ compatibility.c \
		$(ROOT)/libretro_game_database.c $(ROOT)/input_compatibility.c

check: oracle transport range compatibility
	./oracle
	@echo
	./transport
	@echo
	./range
	@echo
	./compatibility

clean:
	rm -f oracle transport range compatibility

.PHONY: all check clean
