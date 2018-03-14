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

#define MAX_PACKET_LENGTH 1024; // 1024 bytes, inluding all headers
#define MAX_SEQ_NUM 30720; // 30720 bytes, reset to 1 after it reaches 30Kbytes
int window_size = 5120; // bytes, default value
int RTO = 500; // retransmission timeout value
#define ACK 0x08;
#define FIN 0x04;
#define FRAG 0x02;
#define SYN 0x01;

/*class packet_header {
public:
    unsigned short seq_num, ACK_num, data_length;
    inline void set(int var) {
                flags |= var;
                return;
    };
    inline bool ack() {return flags & 0x08;}
    inline bool fin() {return flags & 0x04;}
    inline bool frag() {return flags & 0x02;}
    inline bool syn() {return flags & 0x01;}

private:
    unsigned char flags = 0x00;  // ACK, FIN, FRAG, SYN
};*/

int sockfd, portno;
char* hostname;
struct sockaddr_in serv_addr, cli_addr;
struct hostent *server;

//Simple error handling function. Prints a message and exits when called.
void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{//Socket connection as provided by sample code


    if (argc < 3) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
	
	hostname = argv[1];
	portno = atoi(argv[2]);

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
	
	//Set up client as server as well. Server will discover this
    memset((char*) &cli_addr, 0, sizeof(cli_addr));   // reset memory
    cli_addr.sin_family = AF_INET;
	cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    cli_addr.sin_port = htons(0);
	if (bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0)
        error("ERROR on binding");
	/*
    char buf[1024];
	printf("Enter msg");
	fgets(buf, 1024, stdin);*/
	char* buf = "test sending";
	
	int serverlen = sizeof(serv_addr);
	if(sendto(sockfd,buf,strlen(buf),0, (struct sockaddr *)&serv_addr,serverlen) < 0)
		error("ERROR in sendto");
	printf("send \"%s\" to %d\n",buf,portno);
	
	char in_buf[1024]; //Buffer for HTTP GET input
	char header[12]; //Buffer for HTTP response header
	int recvlen;
	while(1){
	memset(in_buf, 0, 1024);  // reset memory
	
	//printf("waiting on port %d\n", portno);
	recvlen = recvfrom(sockfd, in_buf, 1024, 0, (struct sockaddr*) &serv_addr, &serverlen);
		if(recvlen > 0){
			printf("received %d bytes\n", recvlen);
			in_buf[recvlen] = 0;
			printf("received message: %s\n", in_buf);
		}
    //close(sockfd);
	}
    return 0;
}
