#include <stdio.h>

int trio_sscanf(const char *buffer, const char *format, ...)
{
   return sscanf(buffer, format);
}

int trio_snprintf(const char *buffer, size_t max, const char *format, ...)
{
   return snprintf(buffer, max, format);
}

int trio_fprintf(FILE *file, const char *format, ...)
{
   return fprintf(file, format);
}

int trio_vasprintf(char **result, const char *format, va_list args)
{
   return vasprintf(result, format, args);
}

int trio_vfprintf(FILE *file, const char *format, va_list args)
{
   return vfprintf(file, format, args);
}

int trio_vfscanf(FILE *file, const char *format, va_list args)
{
   return vfscanf(file, format, args);
}
