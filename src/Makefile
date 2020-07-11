# SPDX-License-Identifier: CC0-1.0

CROSS_COMPILE ?=

CC	:= $(CROSS_COMPILE)gcc
CFLAGS	?= -O0 -g -W -Wall -Wno-unused-parameter -Iinclude
LDFLAGS	?=
LIBS	:= -lm
GEN-IMAGE := gen-image

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: $(GEN-IMAGE)

$(GEN-IMAGE): gen-image.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	-rm -f *.o
	-rm -f $(GEN-IMAGE)

install:
	cp $(GEN-IMAGE) $(INSTALL_DIR)/
