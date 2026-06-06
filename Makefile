APP=vaisselle-factures
CC?=gcc
PREFIX?=/usr/local
CFLAGS?=-O2 -Wall -Wextra -std=c11
PKG_CFLAGS=$(shell pkg-config --cflags gtk+-3.0 sqlite3)
PKG_LIBS=$(shell pkg-config --libs gtk+-3.0 sqlite3)
SRC=src/main.c src/db.c src/invoice.c
OBJ=$(SRC:.c=.o)

all: $(APP)

$(APP): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(PKG_LIBS)

%.o: %.c src/app.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c -o $@ $<

install: $(APP)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(APP) $(DESTDIR)$(PREFIX)/bin/$(APP)

clean:
	rm -f $(APP) $(OBJ)

.PHONY: all install clean
