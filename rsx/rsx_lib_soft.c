#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <boolean.h>

#include "rsx.h"
#include "rsx_intf.h"
#include "../libretro_cbs.h"

bool rsx_soft_open(bool is_pal)
{
   content_is_pal = is_pal;
   return true;
}
