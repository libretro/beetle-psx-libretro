#include <stdint.h>
#include <string.h>

#include <string>

#include "misc.h"

void MDFN_strtoupper(char *str)
{
   size_t x;
   for(x = 0; str[x]; x++)
   {
      if(str[x] >= 'a' && str[x] <= 'z')
         str[x] = str[x] - 'a' + 'A';
   }
}

void MDFN_strtoupper(std::string &str)
{
   size_t x;
   const size_t len = str.length();

   for(x = 0; x < len; x++)
   {
      if(str[x] >= 'a' && str[x] <= 'z')
         str[x] = str[x] - 'a' + 'A';
   }
}
