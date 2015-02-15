#include<stdio.h>
#include<netdb.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/wait.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>

#define BACKLOG 10 	// pending connection queue
#define PORT "8000"	// port to connect
#define MAX_COUNT 1024
#define MAX_LENGTH 1024


void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
	if(sa->sa_family == AF_INET)
		return &(((struct sockaddr_in *)sa)->sin_addr);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	int rv;
	int yes=1;
	int sockfd, new_fd;
	socklen_t sin_size;
	struct sigaction sa;
	char s[INET6_ADDRSTRLEN];
	struct sockaddr_storage their_addr;
	struct addrinfo hints, *servinfo, *p;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	for(p = servinfo; p != NULL; p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			perror("server: socket");
			continue;
		}

		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
			perror("setsockopt");
			exit(1);
		}

		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if(p == NULL){
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); //free the linkedlist

	if(listen(sockfd, BACKLOG) == -1){
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1){
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connection...\n");

	int file_fd;
	ssize_t bytes_read;
	char url[MAX_LENGTH];
	char buffer[MAX_LENGTH];
	char method[MAX_LENGTH];
	char filename[MAX_LENGTH];
	char protocol[MAX_LENGTH];

	while(1){
		sin_size = sizeof their_addr;
		if((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1){
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n",s);

		// read http request
		bytes_read = recv(new_fd, buffer, sizeof buffer, 0);
		buffer[bytes_read] = '\0';

		//extract the mathod, url, protocol
		sscanf(buffer, "%s %s %s", method, url, protocol);

		filename[0] = '\0';
		if(strcmp(url,"/") == 0) // if file not specified then send "File Not Specified" http response
			strcpy(filename, "file_not_specified.txt");
		else // copy url to filename skipping the first '/'
			strcpy(filename, &url[1]);

		// open file for reading
		if((file_fd = open(filename, O_RDONLY)) == -1){
			// file not found then send "File Not Found" http response
			close(file_fd);
			strcpy(filename, "not_found.txt");
			file_fd = open(filename, O_RDONLY);
		}

		if(!fork()){ // child
			int msgSend;

			close(sockfd);
			while((msgSend = sendfile(new_fd, file_fd, NULL, MAX_COUNT)) != 0)
				if(msgSend == -1){
					perror("send");
					exit(1);
				}
			close(file_fd);
			close(new_fd);
			exit(0);
		}
		// parent
		close(new_fd);
		close(file_fd);
	}
	return 0;
}
