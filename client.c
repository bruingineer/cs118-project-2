/* By Kuan Xiang Wen and Josh Camarena, Feb 2018
   CS118 Project 1

*/
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>  /* signal name macros, and the kill() prototype */

#define MAX_PACKET_LENGTH 1024 // 1024 bytes, inluding all headers
#define MAX_PAYLOAD_LENGTH 1015 // 1024 - 8 - 1
#define HEADER_LENGTH 8
#define MAX_SEQ_NUM 30720 // 30720 bytes, reset to 1 after it reaches 30Kbytes
int window_size = 5120; // bytes, default value
int RTO = 500; // retransmission timeout value

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
char* hostname;
struct sockaddr_in serv_addr, cli_addr;
struct hostent *server;
socklen_t addrlen;

struct PacketHeader {
    unsigned short seq_num;
	unsigned short ack_num;
	unsigned short length;
    unsigned char flags;  // ACK, FIN, FRAG, SYN
};

struct AwaitACK {
	char buf[MAX_PACKET_LENGTH];
	struct PacketHeader header;
	int timeout;
};


int get_packet(char* in_buf, struct PacketHeader* header, char* data) {
	int recvlen = recvfrom(sockfd, in_buf, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &serv_addr, &addrlen);
	if(recvlen > 0){
		memcpy((void*) header,in_buf, HEADER_LENGTH);
		memcpy((void*) data, in_buf + HEADER_LENGTH, header->length);
		printf("Receiving packet %d\n", header->seq_num);
		return 1;
	}
	return 0;
}

//Add pointer to AwaitACK field if an ACK is expected. Else, input NULL.
void send_packet(struct AwaitACK* await_packet, char* input, unsigned short seq, unsigned short acknum, 
				 unsigned char ackflag, unsigned char finflag, unsigned char fragflag, unsigned char synflag){
	char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	unsigned short datalen = strlen(input);
	struct PacketHeader header;
	if(datalen > (MAX_PAYLOAD_LENGTH)) error("Packet too large");
	header.seq_num = seq;
	header.ack_num = acknum;
	header.length = datalen;
	header.flags = ACK*ackflag | FIN*finflag | FRAG*fragflag | SYN*synflag;
	memcpy(buf,(void*) &header, HEADER_LENGTH);
	memcpy(buf + HEADER_LENGTH, input, datalen);
	
	printf("Sending packet %d %d", seq, window_size);
	if(synflag) printf(" SYN");
	else if(finflag) printf(" FIN");
	printf("\n");
	
	if(sendto(sockfd,buf, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&serv_addr,addrlen) < 0)
		error("ERROR in sendto");
	
	if(await_packet != NULL){
		memset(await_packet->buf, 0, MAX_PACKET_LENGTH);
		memcpy(await_packet->buf, buf, HEADER_LENGTH + datalen);
		memset((void*) &await_packet->header, 0, HEADER_LENGTH);
		memcpy((void*) &await_packet->header, (void*) &header, HEADER_LENGTH);
		await_packet->timeout = 1;
	}
}

void retransmit(struct AwaitACK* await_packet){
	char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	memcpy(buf,(void*) &await_packet->header, HEADER_LENGTH);
	memcpy(buf + HEADER_LENGTH, await_packet->buf, await_packet->header.length);
	
	printf("Sending packet %d %d", await_packet->header.seq_num, window_size);
	if(SYN & await_packet->header.flags) printf(" SYN");
	else if(FIN & await_packet->header.flags) printf(" FIN");
	printf(" Retransmission\n");
	
	if(sendto(sockfd,buf, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&serv_addr,addrlen) < 0)
	error("ERROR in sendto");
}

//Primary event loop
char in_buf[1024]; //Buffer for HTTP GET input
int rcv_data;
struct PacketHeader header;
void respond(){
	memset(in_buf, 0, 1024);  // reset memory
	char payload[1024] = {0};
	if(get_packet(in_buf, &header, payload)){
		//printf("%d %d %d\n", header.ack_num, header.length, header.flags);
		//printf("received message: %d \n%s\n", strlen(payload), payload);
		exit(0);
	}
}

int main(int argc, char *argv[])
{//Socket connection as provided by sample code


    if (argc < 3) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
	
	hostname = argv[1];
	portno = atoi(argv[2]);
	rcv_data = creat("receive.data", O_WRONLY);
	
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // create socket
    if (sockfd < 0)
        error("ERROR opening socket");
	
	server = gethostbyname("127.0.0.1");
	if(server == NULL) error("No such host");
	
    // fill in address info for server
    memset((char*) &serv_addr, 0, sizeof(serv_addr));   // reset memory
    serv_addr.sin_family = AF_INET;
	memcpy((void *) &serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(portno);
	addrlen = sizeof(serv_addr);
	
	/*//Set up client as server as well. Server will discover this
    memset((char*) &cli_addr, 0, sizeof(cli_addr));   // reset memory
    cli_addr.sin_family = AF_INET;
	cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    cli_addr.sin_port = htons(0);
	if (bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0)
        error("ERROR on binding");
	*//*
    char buf[1024];
	printf("Enter msg");
	fgets(buf, 1024, stdin);*/
	char* buf = "test sending 1234567890-sdfghjkrefc";
	
	send_packet(NULL, buf, 365, 2001, 1, 0, 1, 0);
	printf("send \"%s\" to %d\n",buf,portno);
	


	while(1){
		respond();
	}
    return 0;
}