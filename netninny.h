#ifndef NETNINNY_H_
#define NETNINNY_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <string>
#include <iostream>
#include <sstream>


#define BLOCK_SIZE 512

using namespace std;

class NinnyClient 
{
 public:
  NinnyClient(const char* port) { mPort.assign(port); }
  int run();
 private:
  string mPort;
  const int BACKLOG = 10;
};

class NinnyServer 
{
 public:
  NinnyServer(int sockfd) :
	  client_socket(sockfd), serv_socket(-1) {};

  int run();

 private:
  int client_socket; //web browser socket
  int serv_socket;	//web host socket
};

int NinnyServer::run()
{
	char buffer[512];
	ssize_t ret;
	
	//intercept the HTTP request from the browser to the web server
	//read it
	ret = recv(client_socket, buffer, sizeof buffer, 0);
	//extract the host address
	//open a socket to the host address
	
	string buff{buffer, ret};
	size_t found = buff.find("\r\n");

	string REQUEST_LINE;
	string METHOD;
	string ABS_URL;
	string VERSION;

	if(found != string::npos) {
	  string tmp{buff, 0, found};
	  REQUEST_LINE = tmp;
	}

	stringstream ss{REQUEST_LINE};
	while(ss >> METHOD >> ABS_URL >> VERSION)

	  cout << METHOD << "\n"
	       << ABS_URL << "\n"
	       << VERSION << "\n";

	//get the request line
	//	char * REQUEST_LINE = strtok(buffer,"\r\n");
	//buffer is fucked up now I dont know how this function operates

	//get the fields from the REQUEST_LINE
	/*
	char * METHOD	= strtok(REQUEST_LINE," ");
	char * ABS_URI	= strtok(NULL," ");
	char * VERSION	= strtok(NULL," ");
	*/

	//	printf("%s\n%s\n%s\n",METHOD,ABS_URI,VERSION);

	return 0;
}

static void
sigchld_handler(int s)
{
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

static void*
get_in_addr(struct sockaddr *sa)
{
  if(sa->sa_family == AF_INET) 
    {
      return &(((struct sockaddr_in*)sa)->sin_addr);
    }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int 
NinnyClient::run() 
{
  int sockfd, new_fd;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  struct sigaction sa;
  int yes{1};
  char s[INET6_ADDRSTRLEN];
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if((rv = getaddrinfo(NULL, mPort.c_str(), &hints, &servinfo) != 0))
    {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
      {
	if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
	  {
	    perror("server: socket");
	    continue;
	  }
	if((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
			 sizeof(int)) == -1))
	  {
	    perror("setsockopt");
	    exit(1);
	  }
	if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
	  {
	    close(sockfd);
	    perror("server: bind");
	    continue;
	  }

	break;

      }

    if(p == NULL)
      {
	fprintf(stderr, "server: failed to bind\n");
	return 2;
      }

    freeaddrinfo(servinfo);

    if(listen(sockfd, BACKLOG) == -1)
      {
	perror("listen");
	exit(1);
      }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if(sigaction(SIGCHLD, &sa, NULL) == -1)
      {
	perror("sigaction");
	exit(1);
      }

    printf("server: waiting for connections...\n");

    while(1)
      {
	sin_size = sizeof their_addr;
	new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size);

	if(new_fd == -1)
	  {
	    perror("accept");
	    continue;
	  }

	inet_ntop(their_addr.ss_family, 
		  get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
	printf("server: got connection from %s\n", s);

	if(!fork())
	  {
	    close(sockfd);

	    // Initiate NinnyServer class as a proxy with new_fd as param
	    NinnyServer proxy(new_fd);
	    proxy.run();

	    exit(1);
	  }
	close(new_fd);
      }
}
    
    
#endif
