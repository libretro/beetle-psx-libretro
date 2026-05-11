#!/usr/bin/env sh

# Convert to C array.
xxd -i -n openbios openbios.bin > openbios.bin.h

# Make safer to include in multiple places.
sed -i 's/unsigned char openbios/static const unsigned char openbios/g' openbios.bin.h

# Discard the useless size specifier.
sed -i 's/unsigned int openbios_len.*$//g' openbios.bin.h
