#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <errno.h>
#include <string.h>

#ifdef __cplusplus

class MDFN_Error
{
 public:

 MDFN_Error();

 MDFN_Error(int errno_code_new, const char *format, ...);

 ~MDFN_Error();

 MDFN_Error(const MDFN_Error &ze_error);
 MDFN_Error & operator=(const MDFN_Error &ze_error);

 virtual const char *what(void);
 int GetErrno(void);

 private:

 int errno_code;
 char *error_message;
};

#endif

#endif
