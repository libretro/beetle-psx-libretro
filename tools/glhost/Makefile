CC ?= cc
CFLAGS ?= -O1 -g -Wall
CFLAGS += -I../../libretro-common/include $(CFLAGS_EXTRA)
# -rdynamic exports glhost's glTexSubImage2D counter to the core.
LDLIBS = -rdynamic -lEGL -lGL -ldl $(LDFLAGS_EXTRA)

glhost: glhost.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f glhost
