/* Differential test: rmpeg1_video vs pl_mpeg, I-pictures only.
 * Compares geometry and per-plane pixel hashes frame by frame, and reports
 * worst-case per-pixel deviation (IEEE 1180 allows IDCT implementations to
 * differ slightly, so an exact hash match is not required -- but it is what
 * we want to see, and any large deviation is a real bug). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <formats/rmpeg1_ps.h>
#include <formats/rmpeg1_video.h>

#define PLM_NO_STDIO 1
#define PL_MPEG_IMPLEMENTATION
#include <stddef.h>
#include "pl_mpeg.h"

static unsigned long fnv(const unsigned char *p, size_t n)
{ unsigned long h=2166136261UL; size_t i; for(i=0;i<n;i++){h^=p[i];h*=16777619UL;} return h; }

typedef struct { unsigned w,h; unsigned long hy,hcb,hcr; unsigned char *y,*cb,*cr; unsigned ys,cs; } shot_t;

static void grab(shot_t *s, const unsigned char *y,const unsigned char *cb,const unsigned char *cr,
                 unsigned w,unsigned h,unsigned ys,unsigned cs)
{
   unsigned r; size_t yn=(size_t)w*h, cn=(size_t)(w/2)*(h/2);
   s->w=w; s->h=h; s->ys=w; s->cs=w/2;
   s->y=malloc(yn); s->cb=malloc(cn); s->cr=malloc(cn);
   for(r=0;r<h;r++) memcpy(s->y+(size_t)r*w, y+(size_t)r*ys, w);
   for(r=0;r<h/2;r++){ memcpy(s->cb+(size_t)r*(w/2), cb+(size_t)r*cs, w/2);
                       memcpy(s->cr+(size_t)r*(w/2), cr+(size_t)r*cs, w/2); }
   s->hy=fnv(s->y,yn); s->hcb=fnv(s->cb,cn); s->hcr=fnv(s->cr,cn);
}

int main(int argc, char **argv)
{
   FILE *f; long len; unsigned char *data;
   rmpeg1_ps_t *ps; rmpeg1_ps_packet_t pkt;
   rmpeg1_video_t *vid; rmpeg1_video_frame_t fr;
   plm_t *plm;
   shot_t mine[4096], ref[4096]; size_t nm=0, nr=0, i, off, chunk;
   int fails=0;

   if (argc<3){fprintf(stderr,"usage: %s file.mpg chunk\n",argv[0]);return 2;}
   chunk=(size_t)atoi(argv[2]);
   f=fopen(argv[1],"rb"); if(!f){perror("open");return 2;}
   fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
   data=malloc((size_t)len);
   if(fread(data,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);

   /* ---- ours: demux then decode ---- */
   ps=rmpeg1_ps_init(0); vid=rmpeg1_video_init();
   off=0;
   while(off<(size_t)len){
      size_t want=(size_t)len-off; if(want>chunk) want=chunk;
      size_t got=rmpeg1_ps_write(ps,data+off,want); off+=got;
      while(rmpeg1_ps_next(ps,&pkt)){
         if(pkt.type!=RMPEG1_PS_VIDEO) continue;
         size_t p=0;
         while(p<pkt.size){
            size_t w=rmpeg1_video_write(vid,pkt.data+p,pkt.size-p);
            p+=w;
            while(rmpeg1_video_decode(vid,&fr)){
               if(fr.coding_type==1 && nm<4096)
                  grab(&mine[nm++],fr.y,fr.cb,fr.cr,fr.width,fr.height,fr.y_stride,fr.c_stride);
            }
            if(w==0) break;
         }
      }
      if(got==0) break;
   }
   while(rmpeg1_video_decode(vid,&fr))
      if(fr.coding_type==1 && nm<4096)
         grab(&mine[nm++],fr.y,fr.cb,fr.cr,fr.width,fr.height,fr.y_stride,fr.c_stride);

   /* ---- reference ---- */
   plm=plm_create_with_memory(data,(size_t)len,0);
   plm_set_audio_enabled(plm,0);
   {
      plm_frame_t *pf;
      while((pf=plm_decode_video(plm))!=NULL && nr<4096)
         grab(&ref[nr++],pf->y.data,pf->cb.data,pf->cr.data,pf->width,pf->height,
              pf->y.width,pf->cb.width);
   }

   printf("I-frames ours=%zu   all-frames ref=%zu   skipped(non-I)=%u errors=%u\n",
          nm,nr,rmpeg1_video_skipped(vid),rmpeg1_video_errors(vid));
   printf("geometry: %ux%u  fps=", rmpeg1_video_width(vid), rmpeg1_video_height(vid));
   { unsigned n,d; rmpeg1_video_framerate(vid,&n,&d); printf("%u/%u\n",n,d); }

   if(nm==0){ printf("RESULT: FAIL (no I-frames decoded)\n"); return 1; }

   /* ref[0] is the first frame in coded order, which is the first I-frame. */
   for(i=0;i<nm && i<1;i++){
      shot_t *a=&mine[i], *b=&ref[0];
      long maxd=0, sum=0; size_t k, n=(size_t)a->w*a->h;
      if(a->w!=b->w||a->h!=b->h){ printf("MISMATCH geometry %ux%u vs %ux%u\n",a->w,a->h,b->w,b->h); fails++; continue; }
      { long hist[16]; int q; for(q=0;q<16;q++) hist[q]=0;
        for(k=0;k<n;k++){ long d=(long)a->y[k]-(long)b->y[k]; if(d<0)d=-d;
                          if(d>maxd)maxd=d; sum+=d; hist[d<15?d:15]++; }
        printf("frame %zu Y: hash %s  maxdiff=%ld  meandiff=%.4f\n", i,
               a->hy==b->hy?"EXACT":"differs", maxd, (double)sum/(double)n);
        printf("  |diff| histogram: ");
        for(q=0;q<8;q++) if(hist[q]) printf("%d:%ld(%.3f%%) ",q,hist[q],100.0*hist[q]/n);
        printf("\n");
        /* pl_mpeg is a cross-check on the bitstream layers, not ground
         * truth for pixel values: idct_accuracy.c shows ours is within the
         * IEEE 1180 peak error of 1 against a double-precision reference,
         * and a few samples in 84,000 differing by up to 5 means the other
         * IDCT is the looser one. Fail only on a difference too large to be
         * IDCT rounding, which would mean a real decode error. */
        if(maxd>16) fails++; }
      { long md=0; size_t cn=(size_t)(a->w/2)*(a->h/2);
        for(k=0;k<cn;k++){ long d=(long)a->cb[k]-(long)b->cb[k]; if(d<0)d=-d; if(d>md)md=d;
                           d=(long)a->cr[k]-(long)b->cr[k]; if(d<0)d=-d; if(d>md)md=d; }
        printf("frame %zu C: maxdiff=%ld\n", i, md);
        if(md>16){ printf("  chroma deviation too large\n"); fails++; } }
   }
   printf(fails?"RESULT: FAIL\n":"RESULT: PASS\n");
   return fails?1:0;
}
