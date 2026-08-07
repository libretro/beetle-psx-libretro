CC ?= cc
CFLAGS ?= -O1 -g -Wall
CPPFLAGS = -I../../libretro-common/include
all: vkhost
vkhost: vkhost.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ vkhost.c -ldl -lvulkan
clean:
	rm -f vkhost
