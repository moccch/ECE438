/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <string>
#include <iostream>

using namespace std;

#define PORT "3490" // the port client will be connecting to 

#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define BUFSIZE 1024

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int http_request(int sockfd, const string &path){
	char buf[BUFSIZE];
	string requestmsg = "GET " + path + " HTTP/1.1\r\n\r\n";
	send(sockfd, requestmsg.c_str(), requestmsg.size(), 0);
	int numrecv = 0;
	FILE* fp = fopen("output", "w");
	if(fp==NULL) return -1;
	string body;
	if((numrecv=recv(sockfd, buf, BUFSIZE, 0))!=-1){
		body = string(buf, numrecv);
		cout<<"Status: "<<body.substr(0, body.find("\r\n\r\n"))<<endl;
		int pos = body.find("\r\n\r\n")+strlen("\r\n\r\n");
		if(pos==-1){
			return -1;
		}
		body = body.substr(pos, -1);
		fwrite(body.c_str(), 1, body.size(), fp);
		cout<<"Body:\n"<<body;
	}
	while((numrecv=recv(sockfd, buf, BUFSIZE, 0))!=-1){
		if(numrecv==0) break;
		fwrite(buf, 1, numrecv, fp);
		cout<<string(buf, numrecv);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	if (argc != 2) {
	    fprintf(stderr,"usage: client hostname\n");
	    exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// phrase arguments
	string arg(argv[1]);
	cout<<arg<<endl;
	// string arg("http://192.168.113.128:3490/partner.txt");
	int ippos = arg.find("//")+strlen("//");
	string ip = arg.substr(ippos, arg.find("/", ippos)-ippos);
	int portpos = ip.find(":");
	string port = "80";
	if(portpos!=-1){
		port = ip.substr(portpos+1, -1); 
		ip = ip.substr(0, portpos);
	}
	string path = arg.substr(arg.find("/", ippos), -1);

	if ((rv = getaddrinfo(ip.c_str(), port.c_str(), &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	// http client:
	http_request(sockfd, path);

	// if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
	//     perror("recv");
	//     exit(1);
	// }

	// buf[numbytes] = '\0';

	// printf("client: received '%s'\n",buf);

	close(sockfd);

	return 0;
}

