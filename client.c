/* By Kuan Xiang Wen and Josh Camarena, Feb 2018
   CS118 Project 2
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
char* hostname;
struct sockaddr_in serv_addr, cli_addr;
struct hostent *server;
socklen_t addrlen;
// sequence # doesn't matter in client
int global_seq = 10000;
int rcv_data; // fd receive file
char* filebuf;

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

int get_packet(char* in_buf, struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &serv_addr, &addrlen);
	if(recvlen > 0){
		printf("Receiving packet %d\n", rcv_packet->seq_num);
		return 1;
	}
	return recvlen;
}

//Add pointer to AwaitACK field if an ACK is expected. Else, input NULL.
void send_packet(struct WindowFrame* frame, char* input, unsigned short datalen, unsigned short seq, unsigned short acknum, 
				 unsigned char ackflag, unsigned char finflag, unsigned char fragflag, unsigned char synflag){

	struct Packet tr_packet = {
		.seq_num = seq,
		.ack_num = acknum,
		.length = datalen,
		.flags = ACK*ackflag | FIN*finflag | FRAG*fragflag | SYN*synflag
	};
	if(input != NULL) memcpy(tr_packet.payload, input, datalen);
	else memset(tr_packet.payload, 0, MAX_PAYLOAD_LENGTH);

	//printf("Sending packet %d %d", seq, WINDOW_SIZE_BYTES);
	// client prints
	if(synflag) printf("Sending packet SYN");
	else {
		printf("Sending packet %d", acknum);
		if(finflag) printf(" FIN");
	}
	printf("\n");
	
	if(sendto(sockfd, &tr_packet, datalen+sizeof(tr_packet), 0, (struct sockaddr *)&serv_addr, addrlen) < 0)
		error("ERROR in sendto");
	if(frame != NULL){
		frame->packet = tr_packet;
		frame->sent = 0;
		frame->ack = 0;
		frame->timeout = 0;
		gettimeofday(&frame->timesent_tv,NULL);
		//frame->timesent_tv;
	}
}

void retransmit(struct WindowFrame* frame){
	char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	memcpy(buf,(void*) &frame->packet, MAX_PACKET_LENGTH);
	
	printf("Sending packet %d %d", frame->packet.ack_num, WINDOW_SIZE_BYTES);
	if(SYN & frame->packet.flags) printf(" SYN");
	else if(FIN & frame->packet.flags) printf(" FIN");
	printf(" Retransmission\n");
	
	if(sendto(sockfd, buf, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&serv_addr,addrlen) < 0)
	error("ERROR in sendto");
}

//Primary event loop
int fragments;
int* fragment_track;
int fragbegin;
long int filesize;
char in_buf[MAX_PACKET_LENGTH]; //Buffer for HTTP GET input
struct Packet rcv_packet;
void respond(){
	memset(in_buf, 0, MAX_PACKET_LENGTH);  // reset memory
	//char payload[MAX_PACKET_LENGTH] = {0};
	int recvlen;
	if((recvlen = get_packet(in_buf, &rcv_packet)) == 0) return;

	if (rcv_packet.flags & SYN && rcv_packet.flags & ACK) {
		// only send syn ack then break
		if (rcv_packet.flags & FIN) {
			printf("404 file not found\n");
			send_packet(NULL, "", 0, 0, rcv_packet.seq_num, 1,1,0,0);
			//global_seq = global_seq+MAX_PACKET_LENGTH;
			close(sockfd);
			exit(1);
		}
		
		//Initialize input array and trackers
		memset((void*) &filesize, 0, sizeof(filesize));
		memcpy((void*) &filesize, rcv_packet.payload, sizeof(filesize));
		filebuf = (char*) malloc(filesize * sizeof(char));//The buffer itself
		fragments = (filesize / MAX_PAYLOAD_LENGTH) + 1;//Number of packets needed
		fragment_track = (int*) malloc(fragments * sizeof(int));//Keeps track where things should go
		int i;
		for(i = 0; i < fragments; i++) fragment_track[i] = 0;
		fragbegin = rcv_packet.seq_num + MAX_PACKET_LENGTH;
		//Respond
		send_packet(NULL, NULL, 0, 0, rcv_packet.seq_num + MAX_PACKET_LENGTH, 1,0,0,0);
		
	} else {
		//Received FIN - close connection
		if (rcv_packet.flags & FIN) {
			/*if(fragments > -1){
				free(fragment_track);
				free(filebuf);
			}*/
			close(sockfd);
			exit(0);
		}
		//Receive file fragment
		if (rcv_packet.flags & FRAG) {
			int index = (rcv_packet.seq_num - fragbegin)/MAX_PACKET_LENGTH;
			if(fragment_track[index] == 0){
				printf("Index: %d Size: %d\n",index, rcv_packet.length);
				memcpy(filebuf+index*MAX_PAYLOAD_LENGTH,rcv_packet.payload,rcv_packet.length);
				fragment_track[index] = 1;
				fragments--;
			}
			if(fragments == 0){
				write(rcv_data,filebuf,filesize);
				send_packet(NULL, NULL, 0, 0, rcv_packet.seq_num + MAX_PACKET_LENGTH, 0,1,0,0);
			} else {
				send_packet(NULL, NULL, 0, 0, rcv_packet.seq_num + MAX_PACKET_LENGTH, 1,0,0,0);
			}
			//TODO: Write to file buffer filebuf
		}
	}

	// printf("%d %d %d\n", header.ack_num, header.length, header.flags);
	// printf("received message: %d \n%s\n", strlen(payload), payload);
	// char* buf = "Server Acknowledged";
	// send_packet(NULL, buf, 2002, 366, 0, 1, 0, 1);

}

int main(int argc, char *argv[])
{//Socket connection as provided by sample code
    if (argc < 4) {
        fprintf(stderr,"ERROR, not enough arguments\n");
        exit(1);
    }
	
	hostname = argv[1];
	portno = atoi(argv[2]);

	if ((rcv_data = open("receive.data", O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
		error("Failed to open file");
	}
	
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
	
	send_packet(NULL, argv[3], strlen(argv[3]), global_seq, 0, 0, 0, 0, 1);
	global_seq = global_seq+MAX_PACKET_LENGTH;
	fragments = -1;

	while(1){
		respond();
	}
    return 0;
}
