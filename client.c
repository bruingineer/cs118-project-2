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
	int timeout;
	struct timeval timesent_tv;
};
//Client only has at most 1 packet awaiting to be ACKed
struct WindowFrame window1;

int get_packet(struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &serv_addr, &addrlen);
	if(recvlen > 0){
		printf("Receiving packet %d\n", rcv_packet->seq_num);
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
	}
	printf("\n");
	
	if(sendto(sockfd, &tr_packet, datalen+sizeof(tr_packet), 0, (struct sockaddr *)&serv_addr, addrlen) < 0)
		error("ERROR in sendto");
	if(frame != NULL){
		frame->packet = tr_packet;
		frame->sent = 1;
		frame->ack = 0;
		frame->timeout = 0;
		gettimeofday(&frame->timesent_tv,NULL);
		//frame->timesent_tv;
	}
	return datalen+sizeof(tr_packet);
}

void retransmit(struct WindowFrame* frame){
	/*char buf[MAX_PACKET_LENGTH];
	memset(buf, 0, MAX_PACKET_LENGTH);
	memcpy(buf,(void*) &frame->packet, MAX_PACKET_LENGTH);*/
	
	printf("Sending packet %d %d", frame->packet.ack_num, WINDOW_SIZE_BYTES);
	if(SYN & frame->packet.flags) printf(" SYN");
	else if(FIN & frame->packet.flags) printf(" FIN");
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

// TIMEOUT logic
// go thru the window calculate the shortest timeout 
void refresh_timeout(){
	global_timeout = 0;
	int shortest_TO_msec = RTO + 1;
	int timeleft;
	if(window1.sent == 1 && window1.ack == 0){//For all awaiting a return
		timeleft = timeout_remaining(window1.timesent_tv);
		if(timeleft == 0){
			retransmit(&window1);
			timeleft = RTO;
		}
		if (timeleft < shortest_TO_msec)
			shortest_TO_msec = timeleft;
	}
	if(shortest_TO_msec < RTO + 1) global_timeout = shortest_TO_msec;
	// printf("seq: %d, elapsed_msec: %d\n", window[to_iter].packet.seq_num, elapsed_msec);
}

//Handles resending
void check_timeout(){
	if(window1.sent == 1 && window1.ack == 0){//For all awaiting a return
		if (timeout_remaining(window1.timesent_tv) <= 0) retransmit(&window1);
	}
	refresh_timeout();
}

//Primary event loop
int fragments;
int* fragment_track;
int fragbegin;
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
			send_packet(NULL, "", 0, 0, rcv_packet.seq_num + recvlen, 1,1,0,0);
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
		fragbegin = rcv_packet.seq_num  + recvlen;
		//Respond
		send_packet(&window1, NULL, 0, 0, fragbegin, 1,0,0,0);
		
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
				memcpy(filebuf+index*MAX_PAYLOAD_LENGTH,rcv_packet.payload,rcv_packet.length);
				fragment_track[index] = 1;
				fragments--;
			}
			if(fragments == 0){
				write(rcv_data,filebuf,filesize);
				send_packet(&window1, NULL, 0, 0, rcv_packet.seq_num + recvlen, 0,1,0,0);
			} else {
				empty_window();//Only relevant for first frag - since before that it awaits first frag
				send_packet(NULL, NULL, 0, 0, rcv_packet.seq_num + MAX_PACKET_LENGTH, 1,0,0,0);
			}
		}
	}
	
	refresh_timeout();
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
	
	send_packet(&window1, argv[3], strlen(argv[3]), 0, 0, 0, 0, 0, 1);
	refresh_timeout();
	fragments = -1;

	while(1){
		respond();
		if (global_timeout < 0) {//Something timed out. Handle resending
			check_timeout();
		}
	}
    return 0;
}
