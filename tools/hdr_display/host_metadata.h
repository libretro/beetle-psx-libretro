/* GET_HDR_METADATA / GET_DISPLAY_METADATA for the test hosts (vkhost,
 * glhost), so a lane can present the core with any display. Defaults describe
 * a PQ Rec.2020 presentation with passthrough, paper white 200, peak 1000 and
 * an unknown panel; the environment overrides them:
 *
 *   HOST_HDR_PANEL        Rx,Ry,Gx,Gy,Bx,By in 1/10000 (caps.primaries_xy)
 *   HOST_HDR_PANEL_BLACK  panel black in nits (caps.min_nits)
 *   HOST_HDR_PANEL_PEAK   panel peak in nits (caps.peak_nits)
 *   HOST_HDR_BLACK        the frontend's black target in nits (min_nits)
 *   HOST_HDR_MAX          the frontend's peak target in nits (0 = unreported)
 *   HOST_HDR_PASSTHROUGH  0 to report that HDR10 input is re-graded
 *   HOST_HDR_NO_DISPLAY   answer GET_HDR_METADATA only
 *
 * Unknown versions and undersized buffers are refused, as the API asks. */
#ifndef HOST_HDR_METADATA_H
#define HOST_HDR_METADATA_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "libretro.h"

static float host_hdr_env_float(const char *name, float fallback)
{
   const char *e = getenv(name);
   return e ? (float)atof(e) : fallback;
}

static void host_hdr_fill(struct retro_hdr_metadata *m)
{
   const char *pt = getenv("HOST_HDR_PASSTHROUGH");
   memset(m, 0, sizeof(*m));
   m->version           = 1;
   m->size              = sizeof(*m);
   m->output_mode       = 1;  /* PQ Rec.2020 */
   m->screen_10bpc      = true;
   m->paper_white_nits  = 200.0f;
   m->max_nits          = host_hdr_env_float("HOST_HDR_MAX", 1000.0f);
   m->min_nits          = host_hdr_env_float("HOST_HDR_BLACK", 0.0f);
   m->expand_gamut      = 0;
   m->color_primaries   = 9;  /* H.273 BT.2020 */
   m->transfer_function = 16; /* H.273 PQ */
   m->passthrough       = !(pt && atoi(pt) == 0);
   m->tonemap_operator  = 0;
}

static bool host_hdr_metadata(unsigned cmd, void *data)
{
   unsigned header[2];
   memcpy(header, data, sizeof(header));
   if (cmd == RETRO_ENVIRONMENT_GET_HDR_METADATA)
   {
      if (header[0] != 1 || header[1] < sizeof(struct retro_hdr_metadata))
         return false;
      host_hdr_fill((struct retro_hdr_metadata *)data);
      return true;
   }
   if (cmd == RETRO_ENVIRONMENT_GET_DISPLAY_METADATA)
   {
      struct retro_display_metadata *d = (struct retro_display_metadata *)data;
      const char *panel                = getenv("HOST_HDR_PANEL");
      if (getenv("HOST_HDR_NO_DISPLAY"))
         return false;
      if (header[0] != 1 || header[1] < sizeof(struct retro_display_metadata))
         return false;
      memset(d, 0, sizeof(*d));
      d->version = 1;
      d->size    = sizeof(*d);
      host_hdr_fill(&d->hdr);
      d->caps.min_nits  = host_hdr_env_float("HOST_HDR_PANEL_BLACK", 0.0f);
      d->caps.peak_nits = host_hdr_env_float("HOST_HDR_PANEL_PEAK", 0.0f);
      if (panel)
      {
         unsigned v[6];
         unsigned i;
         if (sscanf(panel, "%u,%u,%u,%u,%u,%u",
                  &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
         {
            for (i = 0; i < 6; i++)
               d->caps.primaries_xy[i] = (uint16_t)v[i];
            d->caps.primaries_xy[6]  = 3127;
            d->caps.primaries_xy[7]  = 3290;
            d->caps.capability_source = 1;
         }
      }
      return true;
   }
   return false;
}

#endif
