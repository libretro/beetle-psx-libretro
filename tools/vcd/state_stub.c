/* Link support for the VCD harnesses.
 *
 * mednafen/state.c reaches for the core's whole-machine StateAction when
 * writing a complete state file. These harnesses drive one section directly
 * and never get there, so a stub satisfies the link without pulling in the
 * rest of the emulator. */
#include "../../mednafen/state.h"

int StateAction(StateMem *sm, int load, int data_only);
int StateAction(StateMem *sm, int load, int data_only)
{
   (void)sm;
   (void)load;
   (void)data_only;
   return 1;
}
