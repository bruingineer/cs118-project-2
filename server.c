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
#include <signal.h>  /* signal name macros, and the kill() prototype */

#define MAX_PACKET_LENGTH 1024; // 1024 bytes, inluding all headers
#define MAX_SEQ_NUM 30720; // 30720 bytes, reset to 1 after it reaches 30Kbytes
int window_size = 5120; // bytes, default value
int RTO = 500; // retransmission timeout value
#define ACK 0x08;
#define FIN 0x04;
#define FRAG 0x02;
#define SYN 0x01;

struct PacketHeader {
    unsigned short seq_num;
	unsigned short ack_num;
	unsigned short length;
    unsigned char flags;  // ACK, FIN, FRAG, SYN
};


int sockfd, portno;
struct sockaddr_in serv_addr, cli_addr;
socklen_t addrlen = sizeof(cli_addr);



//Simple error handling function. Prints a message and exits when called.
void error(char *msg)
{
    perror(msg);
    exit(1);
}

char in_buf[1024]; //Buffer for HTTP GET input
char hdr_buf[8]; //Buffer for HTTP response header
int recvlen;
//Handles server input/output
void respond(){
	struct PacketHeader header;
	printf("%d\n",sizeof(header));
	
	memset(in_buf, 0, 1024);  // reset memory
	
	//printf("waiting on port %d\n", portno);
	recvlen = recvfrom(sockfd, in_buf, 1024, 0, (struct sockaddr*) &cli_addr, &addrlen);
	if(recvlen > 0){
		printf("received %d bytes\n", recvlen);
		in_buf[recvlen] = 0;
		printf("received message: %s\n", in_buf);
		struct PacketHeader header;
		header.seq_num = 2000;
		header.ack_num = 2001;
		header.length = 222;
		header.flags = 3;
		char* out_buf[1024];
		memset(out_buf,0,1024);
		memcpy(out_buf,(void*) &header,sizeof(header));
		sendto(sockfd, out_buf, 1024, 0, (struct sockaddr*) &cli_addr, addrlen);
		//sendto(sockfd, in_buf, 1024, 0, (struct sockaddr*) &cli_addr, addrlen);
	}
	
	/*
    int n;
    char in_buffer[2048]; //Buffer for HTTP GET input
    char header[2048]; //Buffer for HTTP response header

    memset(in_buffer, 0, 2048);  // reset memory
    memset(header,0,2048); //reset header memory

    //read client's message
    n = read(newsockfd, in_buffer, 2047);
    if (n < 0) error("ERROR reading from socket");
    printf("%s\n", in_buffer);
    
    //Extract file name, get its file descriptor, and modify content_type, content_length and content_response_code
    int fd = parse_file_req(in_buffer);
   
    //Construct TCP header incrementally
    strcat(header,"HTTP/1.1 ");
    strcat(header,content_response_code);
    strcat(header,"\r\nContent-Type: ");
    strcat(header,content_type);
    strcat(header,"\r\nContent-Length: ");
    sprintf(header + strlen(header),"%d", (int) content_length);//sprintf here needed for integer
    strcat(header,"\r\nConnection: Keep-Alive\r\n\r\n");
   
    write(newsockfd, header, strlen(header));//Write header

    //Write file contents
    char* wrbuf = (char*) malloc(sizeof(char)*content_length); //Create buffer for HTTP response payload with exact size for the file
    read(fd, wrbuf, content_length); //Read file into the buffer
    write(newsockfd, wrbuf, content_length); //Write the buffer into the socket
    close(fd); //Close the file*/
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

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    while(1){
		respond();
    }

						  
    //close(sockfd);

    return 0;
}
