CC ?= cc
CFLAGS ?= -O1 -g -Wall
CFLAGS += -I../../libretro-common/include $(CFLAGS_EXTRA)
LDLIBS = -lEGL -lGL -ldl $(LDFLAGS_EXTRA)

glhost: glhost.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f glhost
