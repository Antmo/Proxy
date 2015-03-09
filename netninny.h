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
#include <vector>

#define BLOCK_SIZE 512

using namespace std;

static void * get_in_addr(struct sockaddr *);
static void sigchld_handler(int);

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
  int client_socket;                    // Web browser socket
  int serv_socket;	                // Web host socket

	
  int read_request(int);                // Read HTTP headers
  int read_response(int);               // Read HTTP response
  int stream_data();			// Stream data from server to client

  int sock_connect();		        // Connect a socket to server
  int sock_send(int, const char *);	// Send a msg to socket

  int build_request();                  // build a request to the web server
  
  //the host server
  string HOST;
  string NEW_REQUEST;
  
  vector<string> BUFFER;

};

/* ========================================================= */
/*                        NinnyServer                        */
/* ========================================================= */


/*
 * connects to a web server using HOST
 */
int NinnyServer::sock_connect()
{
  struct addrinfo hints, *servinfo, *p;
  int rv;

  char s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if( (rv = getaddrinfo(HOST.c_str(), "80", &hints, &servinfo)) != 0)
    {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
    }

  for (p = servinfo; p != NULL; p = p->ai_next)
    {
      if ( (serv_socket = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1 )
	{
	  perror("socket:");
	  continue;
	}
      if ( connect(serv_socket,p->ai_addr, p->ai_addrlen) == -1 )
	{
	  perror("socket connect:");
	  close(serv_socket);
	}

      break;
    }

  if (!p)
    return false;

  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
  printf("connected to %s\n",s);
	
  freeaddrinfo(servinfo);

  return 0;
}

/*
 * Reads RESPONSE header
 */
int NinnyServer::read_response(int sockfd)
{

  ssize_t ret;
  char buffer[BLOCK_SIZE];
  string TEMP;
	
  while (true)
    {

      //read upto BLOCK_SIZE bytes from socket
      memset(buffer,0,BLOCK_SIZE);
      ret = recv(sockfd, buffer, sizeof buffer, 0);

      if (ret == -1)
	{
	  perror("recv:");
	  return 1;
	}
      else if (ret == 0)
	{
	  close(sockfd);
	  serv_socket = -1;
	  return 0;
	}
		
      TEMP = string{buffer};
      BUFFER.push_back(TEMP);
      // find end of http OBS! this is not optimal and doesnt always work
      if ( ret >= 4 )
	{
	  for (int i = 0; i < ret; ++i)
	    {
	      if (buffer[i] == '\r' && buffer[i+1] == '\n' &&
		  buffer[i+2] == '\r' && buffer[i+3] == '\n')
		    return 0; //HTTP header end is in this block
	    }
	}
    }
  return 0;
}

/*
 * Read REQUEST header
 */
int NinnyServer::read_request(int sockfd)
{
  ssize_t ret;
  char buffer[BLOCK_SIZE];
  string TEMP;

  while (true)
    {
      
      memset(buffer,0,BLOCK_SIZE);

      alarm(15);
      ret = recv(sockfd,buffer,sizeof buffer,0);
      alarm(0);
      
      if ( ret == -1 )
	{
	  //due to external interrupt
	  if (errno == EINTR)
	      cout << "No data in 15 seconds\n";
	  else    
	    perror("recv:");
	  return 1;
	}

      //add the data to the buffer
      TEMP = string(buffer);
      BUFFER.push_back(TEMP);
      
      // find end of http OBS! this is not optimal and doesnt always work
      if ( ret >= 4 )
	{
	  if (buffer[ret-4] == '\r' && buffer[ret-3] == '\n' &&
	      buffer[ret-2] == '\r' && buffer[ret-1] == '\n')
	    return 0;
	}
    }
}

int NinnyServer::build_request()
{
  for ( auto str : BUFFER )
    {
      stringstream ss{str};
      string line;
      while ( getline(ss,line) )
	{
	  if (line == "\r\n" )
	    break;

	  if (line.find("Accept-Encoding") != string::npos)
	      continue;

	  if(line.find("Connection:") != string::npos)
	      line = "Connection: Close\r";
	    
	  if (line.find("Host:") != string::npos )
	    {
	      stringstream sss {line};
	      sss >> HOST >> HOST;
	    }

	  line += '\n';
	  NEW_REQUEST += line;
	}
    }
  NEW_REQUEST += "\r\n";
  
  BUFFER.clear();
  return 0;
}


int NinnyServer::stream_data()
{
  ssize_t ret;
  char buffer[BLOCK_SIZE];

  //send the RESPONSE header
  for ( auto msg : BUFFER )
    {
      sock_send(client_socket, msg.c_str() );
    }
  BUFFER.clear();
  
  //continue to stream data
  while (true)
    {
      memset(buffer,0,BLOCK_SIZE);
      ret = recv(serv_socket,buffer,sizeof buffer,0);

      if ( ret == -1 )
	{
	  perror("recv");
	  return 1;
	}
      else if (ret == 0)
	{
	  cout << "Closing socket.....\n";
	  close(serv_socket);
	  serv_socket = -1;
	  return 0;
	}
      else
	sock_send(client_socket,buffer);
    }
}

int NinnyServer::sock_send(int sockfd, const char * msg)
{
  size_t len = strlen(msg);

  if ( len != send(sockfd,msg,len,0) )
    { 
      cerr << "Error: send\n";
      return 1;
    }
  return 0;
}


int NinnyServer::run()
{
	
  if ( read_request(client_socket) != 0 )
    {
      cerr << "Failed to read request header\n";
      return 1;
    }
  
  build_request();
  
  if ( sock_connect() != 0 )
    {
      cerr << "Failed to connect to server\n";
      return 1;
    }
  
  //send the request
  sock_send(serv_socket,NEW_REQUEST.c_str());
	
  if ( read_response(serv_socket) != 0)
    {
      cerr << "Failed to read response header\n";
      return 1;
    }

  stream_data();

  return 0;
}

/* ========================================================= */
/*                    Helper functions                       */
/* ========================================================= */

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

/* ========================================================= */
/*                        NinnyClient                        */
/* ========================================================= */

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
