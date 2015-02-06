/* Linköping universitet, spring 2015
 *
 * Authors: Anton Sundkvist (antsu913)
 *          Jonathan Möller (jonmo578)
 *
 *  A proxy implemented in C++
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BACKLOG 10  // Pending requests
#define CLNT_PRT 80 // Port 80 = HTTP

using namespace std;

int main(int argc, char* argv[])
{

  if(argc != 2)
    {
      cerr << "usage: ./netninny PORTNUMBER";
      return 1;
    }

  return 0;
}
