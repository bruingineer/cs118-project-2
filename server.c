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

#define DEBUG 0
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
int num_frames = 0;
int fileread = 0;
int stateflag;
int fd = -1;
char filename[256] = {0}; // 255 chars max filename size

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

struct WindowFrame window[5] = {0};

int get_packet(struct Packet* rcv_packet) {
	int recvlen = recvfrom(sockfd, rcv_packet, MAX_PACKET_LENGTH, 0, (struct sockaddr*) &cli_addr, &cli_addrlen);
	if(recvlen > 0){
		// if(rcv_packet->ack_num > 0) printf("Receiving packet %d\n", rcv_packet->ack_num);
		if(rcv_packet->ack_num > 0) {
			printf("Receiving packet %d\n", rcv_packet->ack_num);
			//if(rcv_packet->flags & FIN) printf(" FIN\n");
			//else printf("\n");
		}
		return 1;
	}
	return 0;
}

//Add pointer to frame field if an ACK is expected. Else, input NULL.
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
	
	printf("Sending packet %d %d", seq, WINDOW_SIZE_BYTES);

	if(synflag) printf(" SYN");
	else if(finflag) printf(" FIN");
	printf("\n");

	if(sendto(sockfd, &tr_packet, datalen+sizeof(tr_packet), 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
		error("ERROR in sendto");
	
	if(frame != NULL){
		frame->packet = tr_packet;
		frame->sent = 1;
		frame->ack = 0;
		gettimeofday(&(frame->timesent_tv),NULL);
	}
	return datalen+sizeof(tr_packet);
}

void retransmit(struct WindowFrame* frame){
	printf("Sending packet %d %d", frame->packet.seq_num, WINDOW_SIZE_BYTES);
	if(SYN & frame->packet.flags) printf(" SYN");
	else if(FIN & frame->packet.flags) printf(" FIN");
	printf(" Retransmission\n");
	
	if(sendto(sockfd, &(frame->packet), frame->packet.length + sizeof(frame->packet), 0, (struct sockaddr *)&cli_addr,cli_addrlen) < 0)
		error("ERROR in sendto");
	gettimeofday(&(frame->timesent_tv),NULL);
}

/*void init_file_transfer(){
	int i;
	char buf[MAX_PAYLOAD_LENGTH];
	for(i=0; i < 5; i=i+1) {
		// reads next part of file and puts it in window
		int r = read(fd,buf,MAX_PAYLOAD_LENGTH);
		if (r == 0) {
			fileread = 1;
			break; //File is completely transmitted!
		}
		else{ 
			send_packet(&window[i], buf, sizeof(buf), global_seq, 0, 0, 0, 1, 0);
			global_seq = global_seq + MAX_PACKET_LENGTH;
			// num_frames = num_frames + 1;
		}
		// once window is full, break
	}
}*/

// returns 1 if all frames in window are acked, 0 otherwise
int check_final_acks() {
	int i;
	for(i = 0; i < 5; i++){
		if(window[i].ack == 0){
			return 0;
		}
	}
	return 1;
}

void file_transfer(unsigned short acknum){
	// process window here
	// check if any packets can be saved in order and move the window up
	// then fill window with next payloads from next_file_window_frame
	if (stateflag == 1) {
		int i;
		char buf[MAX_PAYLOAD_LENGTH];
		for(i=0; i < 5; i=i+1) {
			// reads next part of file and puts it in window
			int r = read(fd,buf,MAX_PAYLOAD_LENGTH);
			if (r == 0) {
				fileread = 1;
				for (; i<5;i++)
					window[i].ack = 1;
				break; //File is completely buffered!
			}
			else{ 
				send_packet(&window[i], buf, sizeof(buf), global_seq, 0, 0, 0, 1, 0);
				global_seq = global_seq + MAX_PACKET_LENGTH;
				// num_frames = num_frames + 1;
			}
			// once window is full, break
		}
		return;
	}

	int i;
	for(i = 0; i < 5; i++){
		if(acknum == window[i].packet.seq_num + MAX_PACKET_LENGTH){
			window[i].ack = 1;
			break;
		}
	}
	
	if (DEBUG) {
		for(i = 0; i < 5; i++){
			printf("acknum: %d, ",acknum);
			printf("i.seq+PacketLen: %d =?= ", window[i].packet.seq_num + MAX_PACKET_LENGTH);
			printf("i.ack: %d\n", window[i].ack);
		}
	}
	if(i == 5) return;//Probably repeated ACK
	if(fileread) return; // only need to store ACKs if whole file has been read

	char buf[MAX_PAYLOAD_LENGTH];
	i = 0; //Frame we are checking
	int j = -1; //This iterator searches for the next un-ACKed frame
	if (window[i].ack) {
		while(i < 4) {
			// 'remove' consecutive acked packets from the beginning of the window array 
			// shift other frames in array
			// yes i know this would be 'better' implemented as a queue with pointers
			// where elements don't have to be shifted in an array
			if (window[i].ack) {
				j = i + 1;//Start this iterator
				while(j < 5){
					if(!window[j].ack){
						window[i] = window[j];
						i++;
						j++;
					}
					else j++;
				}
				break;
			} else i++;
		}
	}
	if(j != -1){//j is set => repopulate window, starting from i
		while(i < 5){
			int r = read(fd,buf,MAX_PAYLOAD_LENGTH);
			if (r == 0) {//File is completely transmitted!
				// acks the non used window frames if any are left
				for(; i < 5; i++){
					window[i].ack = 1;
				}
				fileread = 1;
				break; 
			}
			else{ 
				send_packet(&window[i], buf, sizeof(buf), global_seq, 0, 0, 0, 1, 0);
				global_seq = global_seq + MAX_PACKET_LENGTH;
			}
			i++;
		}
	}
}

void empty_window(){
	global_timeout = 0;
	int i;
	for(i = 0; i < 5; i++){
		window[i].sent = 0;
		window[i].ack = 0;
	}
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
	int i;
	int shortest_TO_msec = RTO*2 + 1;
	int timeleft;
	for(i = 0; i < 5; i++){
		if(window[i].sent == 1 && window[i].ack == 0){//For all awaiting a return
			timeleft = timeout_remaining(window[i].timesent_tv);
			if(timeleft <= 0){
				retransmit(&window[i]);
				timeleft = RTO;
			}
			if(timeleft < shortest_TO_msec)
				shortest_TO_msec = timeleft;
		}
	}
	if(shortest_TO_msec < RTO*2 + 1) global_timeout = shortest_TO_msec;
	// printf("seq: %d, elapsed_msec: %d\n", window[to_iter].packet.seq_num, elapsed_msec);
}

//Handles resending
void check_timeout(){
	int i;
	for(i = 0; i < 5; i++){
		if(window[i].sent == 1 && window[i].ack == 0){//For all awaiting a return
			//printf("Frame %d: %d %d\n",i,window[i].packet.seq_num,timeout_remaining(window[i].timesent_tv));
			if (timeout_remaining(window[i].timesent_tv) <= 0) retransmit(&window[i]);
		}
	}
	refresh_timeout();
}

//Primary event loop
// moved stateflag to top
struct Packet rcv_packet;
void respond(){
	if(DEBUG) printf("enter respond with state %d\n", stateflag);
	get_packet(&rcv_packet);
		
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
				// may need to add Timeout for this
				send_packet(&window[0], syn_buf, sizeof(syn_buf), global_seq, rcv_packet.seq_num, 1,finish,0,1);
				global_seq = global_seq+MAX_PACKET_LENGTH;
				if(finish == 0) stateflag = 1;
			}
			if (rcv_packet.flags & FIN && rcv_packet.flags & ACK) {
				close(fd);
				// empty_window();
				// send_packet(&window[0], NULL, 0, global_seq, rcv_packet.seq_num, 1,1,0,0);
				// stateflag = 3;
				close(sockfd);
				exit(0);
			}
			break;
		
			/*
		case 1://Awaiting client handshake ACK - ensures data is allocated
			if (rcv_packet.flags & ACK) {
				// init_file_transfer();
				//stateflag = 2;
				printf("%d, %d", fileread, check_final_acks());

				if (fileread && check_final_acks()) {
					empty_window();
					send_packet(&window[0], NULL, 0, global_seq, rcv_packet.seq_num, 0,1,0,0);
					stateflag = 3;
				} else stateflag = 2;
			}
			break;

			*/
		case 1:
		case 2://File transfer
			if (rcv_packet.flags & ACK) {
				file_transfer(rcv_packet.ack_num);
				if (stateflag == 1) stateflag = 2;
				if (DEBUG) printf("all acked: %d\n", check_final_acks());
				if (fileread && check_final_acks()) {
					empty_window();
					send_packet(&window[0], NULL, 0, global_seq, rcv_packet.seq_num, 0,1,0,0);
					stateflag = 3;
				}
			} 
			break;
		case 3: // transfer over, everything acked, Await FIN ACK then close
			if (rcv_packet.flags & FIN && rcv_packet.flags & ACK) {
				close(sockfd);
				exit(0);
			}
			break;
		default:
			break;
	}
	refresh_timeout();
	if (DEBUG) printf("exit respond\n");
}

//Sets up socket connection and starts server (based entirely off sample code). 
//Calls response() and closes socket and returns right after.
int main(int argc, char *argv[])
{//Socket connection as provided by sample code

    if (argc < 2) {
        error("ERROR, no port provided");
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

	stateflag = 0;
    if (bind(sockfd, (struct sockaddr *) &serv_addr, addrlen) < 0)
        error("ERROR on binding");

    struct pollfd fds[] = {
		{sockfd, POLLIN}
    };
	
    int r = 0;
	global_timeout = 0;
	empty_window();
	
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
			check_timeout();
		}
    }

    return 0;
}
