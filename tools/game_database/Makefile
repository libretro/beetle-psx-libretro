ROOT := ../..
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fwrapv
CPPFLAGS := -I$(ROOT) -I$(ROOT)/libretro-common/include

SRC := compatibility.c $(ROOT)/libretro_game_database.c \
       $(ROOT)/input_compatibility.c

all: compatibility

compatibility: $(SRC) $(ROOT)/database/psx_games.inc
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC)

check: compatibility
	./compatibility

clean:
	rm -f compatibility

.PHONY: all check clean
