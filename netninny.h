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
#include <iterator>
#include <algorithm>

#define BLOCK_SIZE 512

using namespace std;

static const char* error1_redirect =
    "HTTP/1.1 301 Moved Permanently\r\n"
    "Location: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error1.html\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static const char* error2_redirect =
    "HTTP/1.1 301 Moved Permanently\r\n"
    "Location: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error2.html\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

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
  client_socket(sockfd), serv_socket(-1), BUFFER_SIZE(0) {};

  int run();

 private:
  int client_socket;                      // Web browser socket
  int serv_socket;	                  // Web host socket

	
  int read_request(int);                  // Read HTTP headers
  int read_response(int);                 // Read HTTP response
  int stream_data();			  // Stream data from server to client

  int sock_connect();		          // Connect a socket to server
  int sock_send(int, const char *,size_t);	  // Send a msg to socket

  int build_request();                    // Build a request to the web server

  bool forbidden_content_request() const; // Search request for forbidden URL
  bool is_filterable() const;             // Search response for forbidden words
  bool filter_content(const char*&) const;

  char * get_data_space(size_t&);
  size_t get_block_size(size_t) const;
  
  //the host server
  string HOST;
  string NEW_REQUEST;
  
  vector<char*> BUFFER;
  int BUFFER_SIZE;
  
  vector<string> forbidden_url {
        "www.aftonbladet.se",
	"www.svd.se",
	"www.liu.se",
	"www.qz.com",
	"www.bbc.com" };
  
  vector<string> forbidden_words {
        "SpongeBob",
	"BritneySpears",
	"Paris Hilton",
	"Norrköping"};

};

bool incase_find(const string&, const string&);
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

size_t NinnyServer::get_block_size(size_t i ) const
{
  if ( i > BUFFER.size() )
    return 1;

  if (i == BUFFER.size() -1 )
    return BUFFER_SIZE - BLOCK_SIZE *( BUFFER.size()-1);
  else
    return BLOCK_SIZE;
}

char * NinnyServer::get_data_space(size_t &size)
{
  if (BUFFER_SIZE < BUFFER.size() * BLOCK_SIZE)
    {
      size = BUFFER.size()*BLOCK_SIZE - BUFFER_SIZE;
      return BUFFER.back() + (BLOCK_SIZE - size);
    }
  else
    {
      char *block = new char[BLOCK_SIZE];
      memset(block,0,BLOCK_SIZE);
      BUFFER.push_back(block);
      size = BLOCK_SIZE;
      return block;
    }
}

/*
 * Reads RESPONSE header
 */
int NinnyServer::read_response(int sockfd)
{
  
  while (true)
    {
      size_t size;
      char *buffer = get_data_space(size);
      ssize_t ret = recv(sockfd, buffer, size, 0);

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

      BUFFER_SIZE += ret;
      
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

  while (true)
    {
      
      size_t size;
      char * buffer = get_data_space(size);
      alarm(15);
      ssize_t ret = recv(sockfd,buffer,size,0);
      alarm(0);

      if ( ret == -1 )
	{
	  //due to external interrupt
	  if (errno == EINTR)
	      cerr << "No data in 15 seconds\n";
	  else    
	    perror("recv:");
	  return 1;
	}
      else if ( ret == 0 )
	{
	  cerr << "Client closed connection before request was recieved\n";
	  return 2;
	}

      BUFFER_SIZE += size;
      // find end of http OBS! this is not optimal and doesnt always work
      if ( ret >= 4 )
	{
	  if (buffer[ret-4] == '\r' && buffer[ret-3] == '\n' &&
	      buffer[ret-2] == '\r' && buffer[ret-1] == '\n')
	    return 0;
	}

    }
}

bool NinnyServer::filter_content(const char* & buffer) const
{
  string temp = string(buffer);

  for(auto words : forbidden_words)
    {
      if (incase_find(temp,words) )
	return true;
      continue;
    }
  
  return false;
}

bool NinnyServer::is_filterable() const
{
  for(auto str : BUFFER)
    {
      stringstream ss{str};
      string line;
      while(getline(ss, line))
	{
	  if(incase_find(line,"content-type:"))
	    ; // check if words following == text / html / xml
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
	      
	  if (incase_find(line,"Accept-Encoding"))
	    continue;
	  	    
	  if(incase_find(line,"Connection:"))
	      line = "Connection: Close\r";
	    
	  if (incase_find(line,"host:") )
	    {
	      stringstream sss {line};
	      sss >> HOST >> HOST;
	    }

	  line += '\n';
	  NEW_REQUEST += line;
	}
    }

  NEW_REQUEST += "\r\n";
  int pos = NEW_REQUEST.find("http://");
  
  if (pos != string::npos)
    NEW_REQUEST.erase(pos,HOST.size()+7);
  
  BUFFER.clear();
  BUFFER_SIZE = 0;
  return 0;
}

bool NinnyServer::forbidden_content_request() const
{
  for ( auto msg : BUFFER )
    {
      string request = string(msg);
      stringstream request_stream{request};
      string line;

      // cerr << request;

      getline(request_stream, line);

      for(auto str : forbidden_url)
	{
	  if(incase_find(line,str))
	    return true;
	}
      return false;    
    }
}

int NinnyServer::stream_data()
{
  size_t ret;
  char buffer[BLOCK_SIZE];

  //send the RESPONSE header
  for ( size_t i = 0; i < BUFFER.size(); ++i )
    {
      size_t len = get_block_size(i);
      sock_send(client_socket, BUFFER.at(i), len);
    }
  BUFFER.clear();
  BUFFER_SIZE = 0;
  
  //continue to stream data
  while (true)
    {
 
      ret = recv(serv_socket,buffer,BLOCK_SIZE,0);
      
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
	sock_send(client_socket,buffer,ret);
    }
}

int NinnyServer::sock_send(int sockfd, const char *msg, size_t size)
{
  size_t ret = 0;
  size_t len = size;
  while(len > 0)
    {
      
      ret = send(sockfd, msg, len, 0);
      msg += ret;
      len -= ret;
      
      if(ret == -1)
	{
	  perror("send:");
	  return 1;
	}
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

  // Search header for forbidden URLS
  if( forbidden_content_request() )
    {
      // Redirect if forbidden content is found
      sock_send(client_socket,error1_redirect,strlen(error1_redirect));
      close(client_socket);
      client_socket = -1;
      cerr << "Inappropiate content detected: redirecting\n";
      return 0;
    }
  else
    {
      // Otherwise, build a new request
      build_request();
    }

  if ( sock_connect() != 0 )
    {
      cerr << "Failed to connect to server\n";
      return 1;
    }
  
  //send the request
  sock_send(serv_socket,NEW_REQUEST.c_str(),NEW_REQUEST.size());
	
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

bool incase_find(const string &haystack, const string &needle)
{

  auto it = search(
		   haystack.begin(), haystack.end(),
		   needle.begin(),   needle.end(),
		   [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); });

  if(it != haystack.end())
    return true;

  return false;
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
	  
	  exit(0);
	}
      close(new_fd);
    }
}
    
    
#endif
