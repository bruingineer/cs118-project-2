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
drop:
	tc qdisc add dev lo root netem 
dropdelay:
	tc qdisc change dev lo root netem delay 100ms
checkdrop:
	tc qdisc show dev lo
nodrop:
	tc qdist del dev lo root
lossdrop:
	tc qdisc change dev lo root netem loss 20% delay 100ms
reorderdrop:
	tc qdisc change dev lo root netem gap 5 delay 100ms
dropall:
	tc qdisc change dev lo root netem loss 20% gap 5 delay 100ms

clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server  *.tar.gz ./server ./client

dist: tarball

tarball: clean
	tar -cvzf $(USERID).tar.gz $(FILES)