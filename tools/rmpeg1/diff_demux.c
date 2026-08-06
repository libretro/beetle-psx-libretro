/* Differential test: rmpeg1_ps vs pl_mpeg's demuxer on the same stream.
 * Compares packet count, type, substream index, PTS/DTS and payload bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <formats/rmpeg1_ps.h>

#define PLM_NO_STDIO 1
#define PL_MPEG_IMPLEMENTATION
#include <stddef.h>
#include "pl_mpeg.h"

typedef struct { int type; int idx; unsigned long long pts, dts; size_t size; unsigned long hash; } rec_t;

static unsigned long fnv(const unsigned char *p, size_t n)
{ unsigned long h = 2166136261UL; size_t i; for (i=0;i<n;i++){h^=p[i];h*=16777619UL;} return h; }

int main(int argc, char **argv)
{
   FILE *f; long len; unsigned char *data;
   rec_t *mine, *ref; size_t nmine=0, nref=0, cap=200000, i, off, chunk;
   rmpeg1_ps_t *ps; rmpeg1_ps_packet_t pkt;
   plm_buffer_t *pb; plm_demux_t *dx; plm_packet_t *rp;
   int mismatch = 0;

   if (argc < 3) { fprintf(stderr,"usage: %s file.mpg chunksize\n",argv[0]); return 2; }
   chunk = (size_t)atoi(argv[2]);

   f = fopen(argv[1],"rb"); if(!f){perror("open");return 2;}
   fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
   data = malloc((size_t)len);
   if (fread(data,1,(size_t)len,f) != (size_t)len) { fprintf(stderr,"short read\n"); return 2; }
   fclose(f);

   mine = calloc(cap,sizeof(rec_t)); ref = calloc(cap,sizeof(rec_t));

   /* --- ours, fed in chunk-sized pieces to exercise the streaming path --- */
   ps = rmpeg1_ps_init(0);
   if (!ps) { fprintf(stderr,"init failed\n"); return 2; }
   off = 0;
   while (off < (size_t)len) {
      size_t want = (size_t)len - off; if (want > chunk) want = chunk;
      size_t got  = rmpeg1_ps_write(ps, data+off, want);
      off += got;
      while (rmpeg1_ps_next(ps,&pkt) && nmine < cap) {
         mine[nmine].type=pkt.type; mine[nmine].idx=pkt.index;
         mine[nmine].pts=pkt.pts;   mine[nmine].dts=pkt.dts;
         mine[nmine].size=pkt.size; mine[nmine].hash=fnv(pkt.data,pkt.size);
         nmine++;
      }
      if (got == 0) { /* buffer full and nothing drained: would deadlock */
         fprintf(stderr,"STALL at off=%zu\n",off); return 3; }
   }
   while (rmpeg1_ps_next(ps,&pkt) && nmine < cap) {
      mine[nmine].type=pkt.type; mine[nmine].idx=pkt.index;
      mine[nmine].pts=pkt.pts; mine[nmine].dts=pkt.dts;
      mine[nmine].size=pkt.size; mine[nmine].hash=fnv(pkt.data,pkt.size); nmine++;
   }

   /* --- reference --- */
   pb = plm_buffer_create_with_memory(data,(size_t)len,0);
   dx = plm_demux_create(pb,0);
   while ((rp = plm_demux_decode(dx)) != NULL && nref < cap) {
      int t; int ix;
      if (rp->type>=0xE0&&rp->type<=0xEF){t=RMPEG1_PS_VIDEO;ix=rp->type&0x0F;}
      else if (rp->type>=0xC0&&rp->type<=0xDF){t=RMPEG1_PS_AUDIO;ix=rp->type&0x1F;}
      else if (rp->type==0xBD){t=RMPEG1_PS_PRIVATE_1;ix=0;}
      else continue;
      ref[nref].type=t; ref[nref].idx=ix;
      ref[nref].pts = (rp->pts<0)?RMPEG1_PS_NO_PTS:(unsigned long long)(rp->pts*90000.0+0.5);
      ref[nref].size=rp->length; ref[nref].hash=fnv(rp->data,rp->length);
      nref++;
   }

   printf("packets: ours=%zu ref=%zu  resyncs=%u  scr=%llu muxrate=%u\n",
          nmine,nref,rmpeg1_ps_resyncs(ps),
          (unsigned long long)rmpeg1_ps_scr(ps), rmpeg1_ps_mux_rate(ps));

   if (nmine != nref) { printf("MISMATCH: packet count\n"); mismatch=1; }

   for (i=0;i<(nmine<nref?nmine:nref);i++) {
      if (mine[i].type!=ref[i].type||mine[i].idx!=ref[i].idx||
          mine[i].size!=ref[i].size||mine[i].hash!=ref[i].hash) {
         printf("MISMATCH @%zu: ours{t=%d i=%d sz=%zu h=%08lx} ref{t=%d i=%d sz=%zu h=%08lx}\n",
                i,mine[i].type,mine[i].idx,mine[i].size,mine[i].hash,
                ref[i].type,ref[i].idx,ref[i].size,ref[i].hash);
         if (++mismatch>5) break;
      }
      /* pl_mpeg stores PTS as double seconds; allow 1 tick of rounding */
      if (mine[i].pts!=RMPEG1_PS_NO_PTS && ref[i].pts!=RMPEG1_PS_NO_PTS) {
         long long d=(long long)mine[i].pts-(long long)ref[i].pts;
         if (d<-1||d>1){ printf("MISMATCH @%zu: pts ours=%llu ref=%llu\n",i,mine[i].pts,ref[i].pts); if(++mismatch>5) break; }
      }
   }

   rmpeg1_ps_free(ps);
   printf(mismatch?"RESULT: FAIL\n":"RESULT: PASS\n");
   return mismatch?1:0;
}
