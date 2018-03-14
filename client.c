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

int sockfd, portno;
char* hostname;

//Simple error handling function. Prints a message and exits when called.
void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{//Socket connection as provided by sample code
    struct sockaddr_in serv_addr;
	struct hostent *server;

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
    

    // fill in address info
    memset((char*) &serv_addr, 0, sizeof(serv_addr));   // reset memory
    serv_addr.sin_family = AF_INET;
	memcpy((void *) &serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(portno);

	/*
    char buf[1024];
	printf("Enter msg");
	fgets(buf, 1024, stdin);*/
	char* buf = "test sending";
	
	int serverlen = sizeof(serv_addr);
	if(sendto(sockfd,buf,strlen(buf),0, (struct sockaddr *)&serv_addr,serverlen) < 0)
		error("ERROR in sendto");
	printf("send \"%s\" to %d\n",buf,portno);
	
    close(sockfd);

    return 0;
}
