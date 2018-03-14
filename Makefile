CC=gcc
CFLAGS=-g -Wall
USERID=004454718
CLASSES=
FILES=server.c Makefile README


all: clean server client

server: $(CLASSES)
	$(CC) -o $@ $^ $(CFLAGS) $@.c
client: $(CLASSES)
	$(CC) -o $@ $^ $(CFLAGS) $@.c

clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server  *.tar.gz ./server ./client

dist: tarball

tarball: clean
	tar -cvzf $(USERID).tar.gz $(FILES)