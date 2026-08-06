ROOT := ../..
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fwrapv
CPPFLAGS := -I$(ROOT) -I$(ROOT)/pgxp

all: oracle

# Links the real pgxp_gpu.c, so the accept rule under test is the shipping
# one rather than a copy of it.
oracle: oracle.c $(ROOT)/pgxp/pgxp_gpu.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ oracle.c $(ROOT)/pgxp/pgxp_gpu.c -lm

check: oracle
	./oracle

clean:
	rm -f oracle

.PHONY: all check clean
