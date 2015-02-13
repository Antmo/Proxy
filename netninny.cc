/* Linköping universitet, spring 2015
 *
 * Authors: Anton Sundkvist (antsu913)
 *          Jonathan Möller (jonmo578)
 *
 *  A proxy implemented in C++
 */

#include "netninny.h"

using namespace std;

int main(int argc, char* argv[])
{

  if(argc != 2)
    {
      cerr << "usage: ./netninny PORTNUMBER";
      return 1;
    }
  else
    {
      NinnyClient ninny(argv[1]);
      return ninny.run();
    }
  return 0;
}
