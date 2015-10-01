#ifndef __MDFN_FILEWRAPPER_H
#define __MDFN_FILEWRAPPER_H

#define MODE_READ       0
#define MODE_WRITE      1
#define MODE_WRITE_SAFE 2

class FileWrapper
{
   public:
      FileWrapper(const char *path, const int mode, const char *purpose = NULL);
      ~FileWrapper();

      uint64_t read(void *data, uint64_t count, bool error_on_eof = true);

      void write(const void *data, uint64_t count);

      char *get_line(char *s, int size);	// Same semantics as fgets(), for now

      void seek(int64_t offset, int whence);

      int64_t tell(void);

      int64_t size(void);

      void close(void);	// Flushes and closes the underlying OS/C lib file.  Calling any other method of this class after a call to
      // this method is illegal(except for the implicit call to the destructor).
      //
      // This is necessary since there can be errors when closing a file, and we can't safely throw an
      // exception from the destructor.
      //
      // Manually calling this method isn't strictly necessary, it'll be called from the destructor
      // automatically, but calling is strongly recommended when the file is opened for writing.
   private:

      FileWrapper & operator=(const FileWrapper &);    // Assignment operator
      FileWrapper(const FileWrapper &);		// Copy constructor

      FILE *fp;
      const int OpenedMode;
};

#endif
