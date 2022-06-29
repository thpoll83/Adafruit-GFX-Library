all: fontconvert

CC     = gcc
CFLAGS = -Wall -I/usr/local/include/freetype2 -I/usr/include/freetype2 -I/usr/include
LIBS   = -lfreetype

fontconvert: fontconvert.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	strip $@

clean:
	rm -f fontconvert

ifeq ($(bindir),)
    bindir := /usr/local/bin
endif

install: fontconvert
	install -m 777 fontconvert $(bindir)/
