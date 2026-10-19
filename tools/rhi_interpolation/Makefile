ROOT := ../..
CFLAGS ?= -O2 -Wall -Wextra -std=gnu89 -fwrapv
CPPFLAGS := -I$(ROOT) -I$(ROOT)/libretro-common/include
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
endif

all: interpolation$(EXEEXT)

interpolation$(EXEEXT): interpolation.c $(ROOT)/rhi/rhi_interpolation.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ interpolation.c

check-shaders:
	sh ./check_shaders.sh "$(ROOT)"

check: check-shaders interpolation$(EXEEXT)
	./interpolation$(EXEEXT)

clean:
	rm -f interpolation interpolation.exe

.PHONY: all check check-shaders clean
