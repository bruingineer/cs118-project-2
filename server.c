/* By Kuan Xiang Wen and Josh Camarena, Feb 2018
   CS118 Project 1
   
   Running this program starts a webserver. It is non-persistent – the server only handles a single HTTP request and then returns. 
   We have also provided “index.html” and “404.html” to handle the default and File DNE cases.
   The server awaits a HTTP GET request, and returns a single HTTP response. 
   To request a file from the server, type “(IP ADDRESS):(PORT NO.)/(FILENAME)” into an up-to-date Firefox browser. 
   It can return the following filetypes: htm/html, jpg/jpeg file, gif file. 
   If the filename is nonempty but has does not have any of the above filetypes, it returns a binary file that prompts a download. 
   If the field is empty, it looks to download “index.html”. 
   If the file is not found, it returns “404.html with a 404 error”.
   After it serves a request, the server closes all sockets and returns/shuts down. 
   To retrieve another file, the server binary should be run again.
*/
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/time.h>
#include <signal.h>  /* signal name macros, and the kill() prototype */

#define MAX_PACKET_LENGTH 1024 // 1024 bytes, inluding all headers
#define MAX_PAYLOAD_LENGTH 1016 // 1024 - 8
#define HEADER_LENGTH 8
#define MAX_SEQ_NUM 30720 // 30720 bytes, reset to 1 after it reaches 30Kbytes
#define WINDOW_SIZE_BYTES 5120 // bytes, default value
#define WINDOW_SIZE_PACKETS 5
#define RTO 500 // retransmission timeout value
#define ACK 0x08
#define FIN 0x04
#define FRAG 0x02
#define SYN 0x01

//Simple error handling function. Prints a message and exits when called.
void error(char *msg)
{
    perror(msg);
    exit(1);
}

int sockfd, portno;
struct sockaddr_in serv_addr, cli_addr;
socklen_t addrlen = sizeof(serv_addr);
socklen_t cli_addrlen = sizeof(cli_addr);
int server_seq = 512;

struct Packet {
    unsigned short seq_num;
	unsigned short ack_num;
	unsigned short length;
    unsigned char flags;  // ACK, FIN, FRAG, SYN
    char payload[MAX_PAYLOAD_LENGTH];
};

struct WindowFrame {
	struct Packet packet;
	int sent;
	int ack;
	int timeout;
	struct timeval timesent_tv;
};


struct AwaitACK {
	char buf[MAX_PACKET_LENGTH];
	//struct PacketHeader header;
	int timeout;
};


int get_packet(char* in_buf, struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &cli_addr, &cli_addrlen);
	if(recvlen > 0){
		// memcpy((void*) header,in_buf,HEADER_LENGTH);
		// memcpy((void*) data, in_buf + HEADER_LENGTH, header->length);
		// memcpy((void*) rcv_packet, )
		printf("Receiving packet %d\n", rcv_packet->seq_num);
		//printf("payload: %s\n", rcv_packet->payload);
		
		return 1;
	}
	return 0;
}

//Add pointer to AwaitACK field if an ACK is expected. Else, input NULL.
void send_packet(struct AwaitACK* await_packet, char* input, unsigned short seq, unsigned short acknum, 
				 unsigned char ackflag, unsigned char finflag, unsigned char fragflag, unsigned char synflag){
	
	char buf[MAX_PACKET_LENGTH] = {0};
	// memset(buf, 0, MAX_PACKET_LENGTH);
	unsigned short datalen = strlen(input);
	if(datalen > (MAX_PAYLOAD_LENGTH)) error("Packet too large");
	struct Packet tr_packet = {
		.seq_num = seq,
		.ack_num = acknum,
		.length = datalen,
		.flags = ACK*ackflag | FIN*finflag | FRAG*fragflag | SYN*synflag
	};
	memcpy(tr_packet.payload, input, datalen);
	// memcpy(buf,(void*) &header, HEADER_LENGTH);
	// memcpy(buf + HEADER_LENGTH, input, datalen);
	
	printf("Sending packet %d %d", seq, WINDOW_SIZE_BYTES);
	if(synflag) printf(" SYN");
	else if(finflag) printf(" FIN");
	printf("\n");
	
	if(sendto(sockfd, &tr_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
		error("ERROR in sendto");
	server_seq = server_seq+MAX_PACKET_LENGTH;

/*
	if(await_packet != NULL){
		memset(await_packet->buf, 0, MAX_PACKET_LENGTH);
		memcpy(await_packet->buf, buf, HEADER_LENGTH + datalen);

		memset((void*) &await_packet->header, 0, HEADER_LENGTH);
		memcpy((void*) &await_packet->header, (void*) &header, HEADER_LENGTH);
		await_packet->timeout = 1;
	}
*/

}

/********
void retransmit(struct AwaitACK* await_packet){
	char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	memcpy(buf,(void*) &await_packet->header, HEADER_LENGTH);
	memcpy(buf + HEADER_LENGTH, await_packet->buf, await_packet->header.length);
	
	printf("Sending packet %d %d", await_packet->header.seq_num, WINDOW_SIZE_BYTES);
	if(SYN & await_packet->header.flags) printf(" SYN");
	else if(FIN & await_packet->header.flags) printf(" FIN");
	printf(" Retransmission\n");
	
	if(sendto(sockfd,buf, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
	error("ERROR in sendto");
}
*******/

int fd;
void send_file(){
	char wrbuf[MAX_PAYLOAD_LENGTH];
	read(fd, wrbuf, MAX_PAYLOAD_LENGTH);
}

//Primary event loop
char in_buf[MAX_PACKET_LENGTH]; //Buffer 
struct Packet rcv_packet;
void respond(){
	memset(in_buf, 0, MAX_PACKET_LENGTH);  // reset memory
	//char payload[MAX_PACKET_LENGTH] = {0};
	get_packet(in_buf, &rcv_packet);

	if (rcv_packet.flags & SYN) {
		char* synbuf = "syn ack";
		// only send syn ack then break
		send_packet(NULL, synbuf, server_seq, rcv_packet.seq_num, 1,0,0,1);
	} else {
		if (rcv_packet.flags & FIN) {

		}
		if (rcv_packet.flags & FRAG) {

		}
		if (rcv_packet.flags & ACK) {
		
		}
	}

	// printf("%d %d %d\n", header.ack_num, header.length, header.flags);
	// printf("received message: %d \n%s\n", strlen(payload), payload);
	// char* buf = "Server Acknowledged";
	// send_packet(NULL, buf, 2002, 366, 0, 1, 0, 1);

}

//Sets up socket connection and starts server (based entirely off sample code). 
//Calls response() and closes socket and returns right after.
int main(int argc, char *argv[])
{//Socket connection as provided by sample code

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // create socket
    if (sockfd < 0)
        error("ERROR opening socket");
    
	
    // fill in address info
	memset((char *) &serv_addr, 0, sizeof(serv_addr));   // reset memory
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(portno);
	
	fd = open("test.jpg", O_RDONLY);
	
    if (bind(sockfd, (struct sockaddr *) &serv_addr, addrlen) < 0)
        error("ERROR on binding");

    struct pollfd fds[] = {
		{sockfd, POLLIN}
    };
    int r = 0;
    while(1){
    	// use poll to detect when data is ready to be read from socket
    	r = poll(fds,1,0);
		if (r < 0) {
			// poll error
			error("error in poll\n");
		}
		else if (fds[0].revents & POLLIN) {
			respond();
		}
    }

    return 0;
}
