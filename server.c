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
struct sockaddr_in serv_addr, cli_addr;
socklen_t addrlen = sizeof(serv_addr);
socklen_t cli_addrlen = sizeof(cli_addr);
int global_seq = 20000;

int fd = -1;
char filename[256] = {0}; // 255 chars max filename size

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

struct WindowFrame window[5] = {0};

int get_packet(char* in_buf, struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &cli_addr, &cli_addrlen);
	if(recvlen > 0){
		if(rcv_packet->ack_num > 0) printf("Receiving packet %d\n", rcv_packet->ack_num);
		return 1;
	}
	return 0;
}

//Add pointer to frame field if an ACK is expected. Else, input NULL.
void send_packet(struct WindowFrame* frame, char* input, unsigned short seq, unsigned short acknum, 
				 unsigned char ackflag, unsigned char finflag, unsigned char fragflag, unsigned char synflag){
	
	unsigned short datalen = 0;
	if(input != NULL) datalen = strlen(input);
	else datalen = 0;

	struct Packet tr_packet = {
		.seq_num = seq,
		.ack_num = acknum,
		.length = datalen,
		.flags = ACK*ackflag | FIN*finflag | FRAG*fragflag | SYN*synflag
	};
	if(input != NULL) memcpy(tr_packet.payload, input, datalen);
	else memset(tr_packet.payload, 0, MAX_PAYLOAD_LENGTH);
	
	printf("Sending packet %d %d", seq, WINDOW_SIZE_BYTES);
	if(synflag) printf(" SYN");
	else if(finflag) printf(" FIN");
	printf("\n");
	
	if(sendto(sockfd, &tr_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
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
	
	printf("Sending packet %d %d", frame->packet.seq_num, WINDOW_SIZE_BYTES);
	if(SYN & frame->packet.flags) printf(" SYN");
	else if(FIN & frame->packet.flags) printf(" FIN");
	printf(" Retransmission\n");
	
	if(sendto(sockfd,buf, MAX_PACKET_LENGTH, 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
	error("ERROR in sendto");
}

// grabs next MAX PAYLOAD SIZE bytes from the file to be sent
// inits the frame pointed to by frame
// returns the data length 
/*
int next_file_window_frame(struct WindowFrame* frame) {
	
	frame->packet.length = r;
	frame->packet.seq_num = global_seq;
	global_seq = global_seq + 1024;
	frame->packet.flags = 0;
	frame->sent = 0;
	frame->ack = 0;
	frame->timeout = 0;
	send_packet(NULL, buf, 2002, 366, 0, 1, 0, 1);
	//frame->timesent_tv;
}*/

void init_file_transfer(){
	int i;
	char buf[MAX_PAYLOAD_LENGTH];
	for(i=0; i < 5; i=i+1) {
		// reads next part of file and puts it in window
		
		int r = read(fd,buf,MAX_PAYLOAD_LENGTH);
		if (r == 0) {
			send_packet(&window[i], buf, global_seq, 0, 0, 0, 0, 0);
			global_seq = global_seq + 1024;
			break; //File is completely transmitted!
		}
		else{ 
			send_packet(&window[i], buf, global_seq, 0, 0, 0, 1, 0);
			global_seq = global_seq + 1024;
		}
		// once window is full, break
	}
}

void file_transfer(unsigned short acknum){
	// process window here
	// check if any packets can be saved in order and move the window up
	// then fill window with next payloads from next_file_window_frame
	int i;
	for(i = 0; i < 4; i++){
		if(acknum == window[i].packet.seq_num - MAX_PACKET_LENGTH){
			window[i].ack = 1;
			break;
		}
	}
	if(i == 5) return;//Probably repeated ACK
	
	char buf[MAX_PAYLOAD_LENGTH];
	i = 0; //Frame we are checking
	int j = -1; //This iterator searches for the next un-ACKed frame
	while(i < 4) {
		// 'remove' consecutive acked packets from the beginning of the window array 
		// shift other frames in array
		// yes i know this would be 'better' implemented as a queue with pointers
		// where elements don't have to be shifted in an array
		if (window[i].ack) {
			j = i + 1;//Start this iterator
			while(j < 4){
				if(!window[j].ack){
					window[i] = window[j];
					i++;
					j++;
				}
				else j++;
			}
			if(j >= 4) break;
		} else i++;
	}
	if(j != -1){//j is set => repopulate window, starting from i
		while(i < 4){
			int r = read(fd,buf,MAX_PAYLOAD_LENGTH);
			if (r == 0) {
				send_packet(&window[i], buf, global_seq, 0, 0, 0, 0, 0);
				global_seq = global_seq + 1024;
				break; //File is completely transmitted!
			}
			else{ 
				send_packet(&window[i], buf, global_seq, 0, 0, 0, 1, 0);
				global_seq = global_seq + 1024;
			}
			i++;
		}
	}
}

//Primary event loop
char in_buf[MAX_PACKET_LENGTH]; //Buffer
int stateflag;
struct Packet rcv_packet;
void respond(){
	memset(in_buf, 0, MAX_PACKET_LENGTH);  // reset memory
	//char payload[MAX_PACKET_LENGTH] = {0};
	get_packet(in_buf, &rcv_packet);
	switch(stateflag){
		case 0://Awaiting SYN
			if (rcv_packet.flags & SYN) {
				char syn_buf[MAX_PAYLOAD_LENGTH];
				memset(syn_buf, 0, MAX_PAYLOAD_LENGTH);
				strncpy(filename, rcv_packet.payload, 256);
				
				int finish = 0;// if 404, set fin to 1
				if ((fd = open(filename, O_RDONLY)) < 0) finish = 1;
				else{
					struct stat s; //Declare struct
					if (fstat(fd,&s) < 0){ //Attempt to use fstat()
						 error("fstat() failed");
					}
					memcpy(syn_buf, (void*) &s.st_size, sizeof(s.st_size));
				}
				// send file size
				
				send_packet(NULL, syn_buf, global_seq, rcv_packet.seq_num, 1,finish,0,1);
				global_seq = global_seq+MAX_PACKET_LENGTH;
				stateflag = 1;
			}
			break;
		case 1://Awaiting client handshake ACK - ensures data is allocated
			if (rcv_packet.flags & ACK) {
				init_file_transfer();
				stateflag = 2;
			}
			break;
		case 2://File transfer
			if (rcv_packet.flags & ACK) {
				file_transfer(rcv_packet.ack_num);
			} else if (rcv_packet.flags & FIN) {
				char fin_buf[MAX_PAYLOAD_LENGTH];
				memset(fin_buf, 0, MAX_PAYLOAD_LENGTH);
				send_packet(NULL, fin_buf, global_seq, rcv_packet.seq_num, 0,1,0,0);
				close(sockfd);
				exit(0);
			}
			break;
		case 3://Await client timeout FINACK in case
			break;
		default:
			break;
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
	stateflag = 1;
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
