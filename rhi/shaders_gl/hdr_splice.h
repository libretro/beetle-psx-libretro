#ifndef GL_HDR_SPLICE_H
#define GL_HDR_SPLICE_H

#include <stdlib.h>
#include <string.h>

/* output_fragment with the Display Transform (hdr_transform.glsl.h) spliced
 * in before its HDR encode, which calls into it. Shared by the GL renderer
 * and tools/hdr_transform's shader compile check, so the check compiles
 * exactly the text the renderer does. Caller frees; NULL on failure. */
static char *gl_hdr_splice_output_fragment(const char *fragment, const char *dt)
{
   const char *mark = strstr(fragment, "vec3 encode_hdr_output(");
   size_t      head, tlen, rest;
   char       *src;
   if (!mark)
      return NULL;
   head = (size_t)(mark - fragment);
   tlen = strlen(dt);
   rest = strlen(mark);
   src  = (char*)malloc(head + tlen + rest + 3);
   if (!src)
      return NULL;
   memcpy(src, fragment, head);
   src[head] = '\n';
   memcpy(src + head + 1, dt, tlen);
   src[head + 1 + tlen] = '\n';
   memcpy(src + head + 2 + tlen, mark, rest + 1);
   return src;
}

#endif
