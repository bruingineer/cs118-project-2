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
int rcv_data; // fd receive file
char* filebuf;
int* fragment_track;
int fragbegin;

struct Packet {
    unsigned short seq_num;
	unsigned short ack_num;
	unsigned short length;
    unsigned char flags;  // ACK, FIN, FRAG, SYN
    char payload[MAX_PAYLOAD_LENGTH];
};

int global_timeout;
struct WindowFrame {
	struct Packet packet;
	int sent;
	int ack;
	struct timeval timesent_tv;
};
//Client only has at most 1 packet awaiting to be ACKed
struct WindowFrame window1;
struct WindowFrame window2;

int get_packet(struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &serv_addr, &addrlen);
	if(recvlen > 0){
		printf("Receiving packet %d\n", rcv_packet->seq_num);
		//if(rcv_packet->flags & FIN) printf(" FIN\n");
		//else printf("\n");
		return recvlen;
	}
	return 0;
}

//Add pointer to AwaitACK field if an ACK is expected. Else, input NULL.
unsigned short send_packet(struct WindowFrame* frame, char* input, unsigned short datalen, unsigned short seq, unsigned short acknum, 
				 unsigned char ackflag, unsigned char finflag, unsigned char fragflag, unsigned char synflag){

	struct Packet tr_packet = {
		.seq_num = seq,
		.ack_num = acknum,
		.length = datalen,
		.flags = ACK*ackflag | FIN*finflag | FRAG*fragflag | SYN*synflag
	};
	if(input != NULL) memcpy(tr_packet.payload, input, datalen);
	else memset(tr_packet.payload, 0, MAX_PAYLOAD_LENGTH);

	if(synflag) printf("Sending packet SYN");
	else {
		printf("Sending packet %d", acknum);
		if(finflag) printf(" FIN");
		if ( (fragment_track[((seq - fragbegin)/MAX_PACKET_LENGTH)]) == 1) {
			printf(" Retransmission");
		}
	}
	printf("\n");
	
	if(sendto(sockfd, &tr_packet, datalen+sizeof(tr_packet), 0, (struct sockaddr *)&serv_addr, addrlen) < 0)
		error("ERROR in sendto");
	if(frame != NULL){
		frame->packet = tr_packet;
		frame->sent = 1;
		frame->ack = 0;
		gettimeofday(&frame->timesent_tv,NULL);
		global_timeout = RTO;
	}
	return datalen+sizeof(tr_packet);
}

void retransmit(struct WindowFrame* frame){
	/*char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	memcpy(buf,(void*) &frame->packet, MAX_PACKET_LENGTH);*/
	
	if(frame->packet.flags & SYN) printf("Sending packet SYN");
	else {
		printf("Sending packet %d", frame->packet.ack_num);
		if(FIN & frame->packet.flags) printf(" FIN");
	}
	printf(" Retransmission\n");
	
	if(sendto(sockfd, &(frame->packet), frame->packet.length + sizeof(frame->packet), 0, (struct sockaddr *)&serv_addr,addrlen) < 0)
		error("ERROR in sendto");
	gettimeofday(&(frame->timesent_tv),NULL);
}

void empty_window(){
	global_timeout = 0;
	window1.sent = 0;
	window1.ack = 0;
}

int timeout_remaining(struct timeval timesent_tv){
	struct timeval current_time_tv;
	gettimeofday(&current_time_tv,NULL);
	int elapsed_msec = (current_time_tv.tv_sec - timesent_tv.tv_sec)*1000
				+ (current_time_tv.tv_usec - timesent_tv.tv_usec)/1000;
	return (RTO-elapsed_msec);
}

int stateflag; //1 = We have already written to file, FIN. 3 = TIME_WAIT
//Handles resending
void check_timeout(){
	if(window1.sent == 1 && window1.ack == 0){//For all awaiting a return
		if (timeout_remaining(window1.timesent_tv) <= 0) {
			if(stateflag == 3) {
				// global_timeout = RTO*2;
				close(sockfd);
				exit(1);
			}
			else global_timeout = RTO;
			retransmit(&window1);

		}
	}
}

//Primary event loop

int fragments;
// int* fragment_track; moved to top
// int fragbegin; moved to top
long int filesize;
struct Packet rcv_packet;
void respond(){
	//char payload[MAX_PACKET_LENGTH] = {0};
	int recvlen;
	if((recvlen = get_packet(&rcv_packet)) == 0) return;
	if (rcv_packet.flags & SYN && rcv_packet.flags & ACK) {
		// only send syn ack then break
		if (rcv_packet.flags & FIN) {
			printf("404 file not found\n");
			send_packet(&window1, NULL, 0, 0, rcv_packet.seq_num + recvlen, 1,1,0,0);
			global_timeout = 2*RTO;
			stateflag = 3;
			return;
		}
		
		//Initialize input array and trackers
		memset((void*) &filesize, 0, sizeof(filesize));
		memcpy((void*) &filesize, rcv_packet.payload, sizeof(filesize));

		fragments = (filesize / MAX_PAYLOAD_LENGTH) + 1;//Number of packets needed
		filebuf = (char*) malloc(fragments * MAX_PAYLOAD_LENGTH * sizeof(char));//The buffer itself. Larger than necessary to prevent overflow
		fragment_track = (int*) malloc(fragments * sizeof(int));//Keeps track where things should go
		int i;
		for(i = 0; i < fragments; i++) fragment_track[i] = 0;
		fragbegin = rcv_packet.seq_num  + recvlen;
		//Respond
		send_packet(&window1, NULL, 0, 0, fragbegin, 1,0,0,0);
		
	} else {
		//Received FIN - reply FINACK
		// if (rcv_packet.flags & FIN && rcv_packet.flags & ACK) {
		if (rcv_packet.flags & FIN) {	
			if(window2.sent) retransmit(&window2);
			else send_packet(&window2, NULL, 0, 0, rcv_packet.seq_num  + recvlen, 1,1,0,0);
			global_timeout = RTO*2;
			stateflag = 3;
		}
		//Receive file fragment
		if (rcv_packet.flags & FRAG) {
			empty_window();
			int index = (rcv_packet.seq_num - fragbegin)/MAX_PACKET_LENGTH;
			if(fragment_track[index] == 0){
				memcpy(filebuf+index*MAX_PAYLOAD_LENGTH,rcv_packet.payload,rcv_packet.length);
				// fragment_track[index] = 1;
				fragments--;
			}

			/*
				int i = 0;
				for(; i < (filesize / MAX_PAYLOAD_LENGTH) + 1; i++){
					printf("%d",fragment_track[i]);
				}
				printf("\n");
			*/
			if(fragments <= 0){
				if(stateflag == 0){
					write(rcv_data,filebuf,filesize);
					close(rcv_data);
					stateflag = 1;
				}
				// send_packet(&window1, NULL, 0, 0, rcv_packet.seq_num + recvlen, 0,1,0,0);
				send_packet(NULL, NULL, 0, rcv_packet.seq_num, rcv_packet.seq_num + recvlen, 1,0,0,0);
			} else {
				empty_window();//Only relevant for first frag - since before that it awaits first frag
				send_packet(NULL, NULL, 0, rcv_packet.seq_num, rcv_packet.seq_num + MAX_PACKET_LENGTH, 1,0,0,0);
			}
			fragment_track[index] = 1;
		}
	}
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
	
	server = gethostbyname(hostname);
	if(server == NULL) error("No such host");
	
    // fill in address info for server
    memset((char*) &serv_addr, 0, sizeof(serv_addr));   // reset memory
    serv_addr.sin_family = AF_INET;
	memcpy((void *) &serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(portno);
	addrlen = sizeof(serv_addr);
	
	struct pollfd fds[] = {
		{sockfd, POLLIN}
    };
	
	send_packet(&window1, argv[3], strlen(argv[3]), 0, 0, 0, 0, 0, 1);
	fragments = -1;
	filebuf = NULL;
	fragment_track = NULL;
	stateflag = 0;
	int r;
	while(1){
    	// use poll to detect when data is ready to be read from socket
    	r = poll(fds,1,global_timeout);
		if (r < 0) {
			// poll error
			error("error in poll\n");
		}
		else if (fds[0].revents & POLLIN) {
			respond();
		} else if (global_timeout != 0) {//Something timed out. Handle resending
			if(stateflag == 3){//TIME-WAIT in FINACK elapsed. Close server
				if(fragment_track != NULL) free(fragment_track);
				if(filebuf != NULL) free(filebuf);
				close(sockfd);
				exit(0);
			}
			check_timeout();
		}
	}
    return 0;
}
