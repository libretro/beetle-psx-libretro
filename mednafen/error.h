#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <errno.h>
#include <string.h>

#ifdef __cplusplus

class ErrnoHolder;
class MDFN_Error : public std::exception
{
 public:

 MDFN_Error();

 MDFN_Error(int errno_code_new, const char *format, ...);
 MDFN_Error(const ErrnoHolder &enh);

 ~MDFN_Error();

 MDFN_Error(const MDFN_Error &ze_error);
 MDFN_Error & operator=(const MDFN_Error &ze_error);

 virtual const char *what(void);
 int GetErrno(void);

 private:

 int errno_code;
 char *error_message;
};

class ErrnoHolder
{
 public:

 ErrnoHolder()
 {
  local_errno = 0;
  local_strerror[0] = 0;
 }

 ErrnoHolder(int the_errno)
 {
  SetErrno(the_errno);
 }

 inline int Errno(void) const
 {
  return(local_errno);
 }

 const char *StrError(void) const
 {
  return(local_strerror);
 }

 void operator=(int the_errno)
 {
  SetErrno(the_errno);
 }

 private:

 void SetErrno(int the_errno);

 int local_errno;
 char local_strerror[256];
};

#endif

#endif
