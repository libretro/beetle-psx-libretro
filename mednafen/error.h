#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <string.h>

#ifdef __cplusplus

class MDFN_Error
{
   public:
      MDFN_Error();
      MDFN_Error(int errno_code_new, const char *format, ...);
      ~MDFN_Error();
   private:
      char *error_message;
};

#endif

#endif
