CC=g++
CPPFLAGS=-lsocket -lnsl -g -Wall
USERID=004454718
CLASSES=
FILES=server.c Makefile README

all: server

default: server
server: $(CLASSES)
	$(CC) -o $@ $^ $(CPPFLAGS) $@.cpp



clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server  *.tar.gz

dist: tarball

tarball: clean
	tar -cvzf $(USERID).tar.gz $(FILES)
