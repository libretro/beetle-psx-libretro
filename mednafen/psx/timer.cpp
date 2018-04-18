/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "psx.h"
#include "timer.h"

#include "../state_helpers.h"

/*
 Notes(some of it may be incomplete or wrong in subtle ways)

 Control bits:
    Lower 3 bits of mode, for timer1(when mode is | 0x100):
        0x1 = don't count while in vblank(except that the first count while in vblank does go through)
        0x3 = vblank going inactive triggers timer reset, then some interesting behavior where counting again is delayed...
        0x5 = vblank going inactive triggers timer reset, and only count within vblank.
        0x7 = Wait until vblank goes active then inactive, then start counting?
    For timer2:
        0x1 = timer stopped(TODO: confirm on real system)

    Target match counter reset enable 0x008
    Target match IRQ enable       0x010
    Overflow IRQ enable       0x020
    IRQ evaluation auto-reset     0x040
    --unknown--           0x080
    Clock selection           0x100
    Divide by 8(timer 2 only?)    0x200

 Counter:
    Reset to 0 on writes to the mode/status register.

 Status flags:
    Current IRQ line status?      0x0400
    Compare flag              0x0800
        Cleared on mode/status read.
        Set repeatedly while counter == target.

    Overflow/Carry flag       0x1000
        Cleared on mode/status read.
        Set repeatedly after counter overflows from 0xFFFF->0 while before the next clocking(FIXME: currently not emulated correctly in respect
        to the constant/repeated setting after an overflow but before the next clocking).

 Hidden flags:
    IRQ done
        Cleared on writes to the mode/status register, on writes to the count register, and apparently automatically when the counter
        increments if (Mode & 0x40) [Note: If target mode is enabled, and target is 0, IRQ done flag won't be automatically reset]

 There seems to be a brief period(edge condition?) where, if target match reset mode is enabled, you can (sometimes?) read the target value in the count
 register before it's reset to 0.  Currently not emulated; I doubt any games rely on this, but who knows.  Maybe a PSX equivalent
 of the PC Engine "Battle Royale"? ;)

 A timer is somewhat unreliable when target match reset mode is enabled and the 33MHz clock is used.  Average 2.4 counts seem to be
 skipped for that timer every target match reset, but oddly subtracting only 2 from the desired target match value seems to effectively
 negate the loss...wonder if my test program is faulty in some way.  Currently not emulated.

 Counters using GPU clock sources(hretrace,dot clock) reportedly will with a low probability return wrong count values on an actual PS1,
 so keep this in mind when writing test programs(IE keep reading the count value until two consecutive reads return the same value).
 Currently not emulated.
*/

/*
 FIXME: Clock appropriately(and update events) when using SetRegister() via the debugger.

 TODO: If we ever return randomish values to "simulate" open bus, remember to change the return type and such of the TIMER_Read() function to full 32-bit too.
*/

/*
 Dec. 26, 2011 Note
   Due to problems I've had with my GPU timing test program, timer2 appears to be unreliable(clocks are skipped?) when target mode is enabled and the full
   33MHz clock is used(rather than 33MHz / 8).  TODO: Investigate further and confirm(or not).

 Jan. 15, 2013 Note:
   Counters using GPU clock sources(hretrace,dot clock) reportedly will with a low probability return wrong count values on an actual PS1, so keep this in mind
   when writing test programs(IE keep reading the count value until two consecutive reads return the same value).
*/

/*
 FIXME: Clock appropriately(and update events) when using SetRegister() via the debugger.

 TODO: If we ever return randomish values to "simulate" open bus, remember to change the return type and such of the TIMER_Read() function to full 32-bit too.
*/

struct Timer
{
   uint32_t Mode;
   uint32_t Counter;  /* Counter value. Only 16-bit, but 32-bit here for detecting counting past target. */
   uint32_t Target;   /* Counter target */

   uint32_t Div8Counter;

   bool IRQDone;
   int32_t DoZeCounting;
};

static bool vblank;
static bool hretrace;
static Timer Timers[3];
static int32_t lastts;

static uint32_t CalcNextEvent(void)
{
   int32_t next_event = 1024; /**/

   unsigned i;
   for(i = 0; i < 3; i++)
   {
      if(!(Timers[i].Mode & 0x30)) /* If IRQ is disabled, abort for this timer. */
         continue;

      if((Timers[i].Mode & 0x8) && (Timers[i].Counter == 0) && (Timers[i].Target == 0) && !Timers[i].IRQDone)
      {
         next_event = 1;
         continue;
      }

      /**/
      /**/
      if((i == 0 || i == 1) && (Timers[i].Mode & 0x100)) /* If clocked by GPU, abort for this timer(will result in poor granularity for pixel-clock-derived timer IRQs, but whatever). */
         continue;

      if(Timers[i].DoZeCounting <= 0)
         continue;

      if((i == 0x2) && (Timers[i].Mode & 0x1))
         continue;

      /**/
      /**/
      /**/
      const uint32_t target = ((Timers[i].Mode & 0x18) && (Timers[i].Counter < Timers[i].Target)) ? Timers[i].Target : 0x10000;
      const uint32_t count_delta = target - Timers[i].Counter;
      uint32_t tmp_clocks;

      if((i == 0x2) && (Timers[i].Mode & 0x200))
         tmp_clocks = (count_delta * 8) - Timers[i].Div8Counter;
      else
         tmp_clocks = count_delta;

      if(next_event > tmp_clocks)
         next_event = tmp_clocks;
   }

   overclock_device_to_cpu(next_event);

   return(next_event);
}

static bool MDFN_FASTCALL TimerMatch(unsigned i)
{
   bool irq_exact = false;

   Timers[i].Mode |= 0x0800;

   if(Timers[i].Mode & 0x008)
      Timers[i].Counter %= 1 < Timers[i].Target ? Timers[i].Target : 1;

   if((Timers[i].Mode & 0x10) && !Timers[i].IRQDone)
   {
      if(Timers[i].Counter == 0 || Timers[i].Counter == Timers[i].Target)
         irq_exact = true;

#if 1
      {
         const uint16_t lateness = (Timers[i].Mode & 0x008) ? Timers[i].Counter : (Timers[i].Counter - Timers[i].Target);

         if(lateness > ((i == 1 && (Timers[i].Mode & 0x100)) ? 0 : 3))
            PSX_DBG(PSX_DBG_WARNING, "[TIMER] Timer %d match IRQ trigger late: %u\n", i, lateness);
      }
#endif

      Timers[i].IRQDone = true;
      IRQ_Assert(IRQ_TIMER_0 + i, true);
      IRQ_Assert(IRQ_TIMER_0 + i, false);
   }

   return irq_exact;
}

static bool MDFN_FASTCALL TimerOverflow(unsigned i)
{
   bool irq_exact = false;

   Timers[i].Mode |= 0x1000;
   Timers[i].Counter &= 0xFFFF;

   if((Timers[i].Mode & 0x20) && !Timers[i].IRQDone)
   {
      if(Timers[i].Counter == 0)
         irq_exact = true;

#if 1
      if(Timers[i].Counter > ((i == 1 && (Timers[i].Mode & 0x100)) ? 0 : 3))
         PSX_DBG(PSX_DBG_WARNING, "[TIMER] Timer %d overflow IRQ trigger late: %u\n", i, Timers[i].Counter);
#endif

      Timers[i].IRQDone = true;
      IRQ_Assert(IRQ_TIMER_0 + i, true);
      IRQ_Assert(IRQ_TIMER_0 + i, false);
   }

   return irq_exact;
}

static void MDFN_FASTCALL ClockTimer(int i, uint32_t clocks)
{
   int32_t before = Timers[i].Counter;
   int32_t target = 0x10000;
   bool zero_tm = false;

   if(Timers[i].DoZeCounting <= 0)
      clocks = 0;

   if(i == 0x2)
   {
      uint32_t d8_clocks;

      Timers[i].Div8Counter += clocks;
      d8_clocks = Timers[i].Div8Counter >> 3;
      Timers[i].Div8Counter &= 0x7;

      if(Timers[i].Mode & 0x200) /* Divide by 8, at least for timer 0x2 */
         clocks = d8_clocks;

      if(Timers[i].Mode & 1)
         clocks = 0;
   }

   if((Timers[i].Mode & 0x008) && Timers[i].Target == 0 && Timers[i].Counter == 0)
      TimerMatch(i);
   else if(clocks)
   {
      uint32_t before = Timers[i].Counter;

      Timers[i].Counter += clocks;

      if(Timers[i].Mode & 0x40)
         Timers[i].IRQDone = false;

      bool irq_exact = false;

      /*
       * Target match handling
       */
      if((before < Timers[i].Target && Timers[i].Counter >= Timers[i].Target) || (Timers[i].Counter >= Timers[i].Target + 0x10000))
         irq_exact |= TimerMatch(i);

      /*
       * Overflow handling
       */
      if(Timers[i].Counter >= 0x10000)
         irq_exact |= TimerOverflow(i);

      /**/
      if((Timers[i].Mode & 0x40) && !irq_exact)
         Timers[i].IRQDone = false;
   }
}

void MDFN_FASTCALL TIMER_SetVBlank(bool status)
{
   switch(Timers[1].Mode & 0x7)
   {
      case 0x1:
         Timers[1].DoZeCounting = !status;
         break;

      case 0x3:
         if(vblank && !status)
         {
            Timers[1].Counter = 0;
            if(Timers[1].Counter == Timers[1].Target)
               TimerMatch(1);
         }
         break;

      case 0x5:
         Timers[1].DoZeCounting = status;
         if(vblank && !status)
         {
            Timers[1].Counter = 0;
            if(Timers[1].Counter == Timers[1].Target)
               TimerMatch(1);
         }
         break;

      case 0x7:
         if(Timers[1].DoZeCounting == -1)
         {
            if(!vblank && status)
               Timers[1].DoZeCounting = 0;
         }
         else if(Timers[1].DoZeCounting == 0)
         {
            if(vblank && !status)
               Timers[1].DoZeCounting = 1;
         }
         break;
   }
   vblank = status;
}

void MDFN_FASTCALL TIMER_SetHRetrace(bool status)
{
   if(hretrace && !status)
   {
      if((Timers[0].Mode & 0x7) == 0x3)
      {
         Timers[0].Counter = 0;

         if(Timers[0].Counter == Timers[0].Target)
            TimerMatch(0);
      }
   }

   hretrace = status;
}

void MDFN_FASTCALL TIMER_AddDotClocks(uint32_t count)
{
   if(Timers[0].Mode & 0x100)
      ClockTimer(0, count);
}

void TIMER_ClockHRetrace(void)
{
   if(Timers[1].Mode & 0x100)
      ClockTimer(1, 1);
}

int32_t MDFN_FASTCALL TIMER_Update(const int32_t timestamp)
{
   int32_t cpu_clocks = timestamp - lastts;

   overclock_cpu_to_device(cpu_clocks);

   int32_t i;
   for(i = 0; i < 3; i++)
   {
      if(Timers[i].Mode & 0x100)
         continue;

      ClockTimer(i, cpu_clocks);
   }

   lastts = timestamp;

   return(timestamp + CalcNextEvent());
}

static void MDFN_FASTCALL CalcCountingStart(unsigned which)
{
   Timers[which].DoZeCounting = true;

   switch(which)
   {
      case 1:
         switch(Timers[which].Mode & 0x07)
         {
            case 0x1:
               Timers[which].DoZeCounting = !vblank;
               break;

            case 0x5:
               Timers[which].DoZeCounting = vblank;
               break;

            case 0x7:
               Timers[which].DoZeCounting = -1;
               break;
         }
         break;
   }

}

void TIMER_Write(const int32_t timestamp, uint32_t A, uint16_t V)
{
   TIMER_Update(timestamp);

   int which = (A >> 4) & 0x3;

   V <<= (A & 3) * 8;

   /*
   PSX_DBGINFO("[TIMER] Write: %08x %04x\n", A, V);
   */

   if(which >= 3)
      return;

   switch(A & 0xC)
   {
      case 0x0: Timers[which].IRQDone = false;
                Timers[which].Counter = V & 0xFFFF;
                break;

      case 0x4: Timers[which].Mode = (V & 0x3FF) | (Timers[which].Mode & 0x1C00);
                Timers[which].IRQDone = false;
                Timers[which].Counter = 0;

                CalcCountingStart(which); /* Call after setting .Mode */
                break;

      case 0x8: Timers[which].Target = V & 0xFFFF;
                break;

      case 0xC: /* Open bus */
                break;
   }

   if(Timers[which].Counter == Timers[which].Target)
      TimerMatch(which);

   PSX_SetEventNT(PSX_EVENT_TIMER, timestamp + CalcNextEvent());
}

uint16_t MDFN_FASTCALL TIMER_Read(const int32_t timestamp, uint32_t A)
{
   uint16_t ret = 0;
   int which = (A >> 4) & 0x3;

   if(which >= 3)
   {
      PSX_WARNING("[TIMER] Open Bus Read: 0x%08x", A);

      return(ret >> ((A & 3) * 8));
   }

   TIMER_Update(timestamp);

   switch(A & 0xC)
   {
      case 0x0: ret = Timers[which].Counter;
                        break;

      case 0x4: ret = Timers[which].Mode;
                Timers[which].Mode &= ~0x1000;
                if(Timers[which].Counter != Timers[which].Target)
                   Timers[which].Mode &= ~0x0800;
                 break;

      case 0x8: ret = Timers[which].Target;
                break;

      case 0xC: /* PSX_WARNING("[TIMER] Open Bus Read: 0x%08x", A); */
                break;
   }

   return(ret >> ((A & 3) * 8));
}

void TIMER_ResetTS(void)
{
   lastts = 0;
}

void TIMER_Power(void)
{
   lastts = 0;

   hretrace = false;
   vblank = false;
   memset(Timers, 0, sizeof(Timers));
}


int TIMER_StateAction(void *sm, const unsigned load, const bool data_only)
{
   int ret;
   SFORMAT StateRegs[] =
   {
#define SFTIMER(n)   SFVARN(Timers[n].Mode, #n "Mode"),        \
      SFVARN(Timers[n].Counter, #n "Counter"),     \
      SFVARN(Timers[n].Target, #n "Target"),       \
      SFVARN(Timers[n].Div8Counter, #n "Div8Counter"),   \
      SFVARN(Timers[n].IRQDone, #n "IRQDone"),     \
      SFVARN(Timers[n].DoZeCounting, #n "DoZeCounting")
      SFTIMER(0),
      SFTIMER(1),
      SFTIMER(2),
#undef SFTIMER

      SFVAR(vblank),
      SFVAR(hretrace),

      SFEND
   };
   ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "TIMER");

   if(load)
   {
      unsigned n;
      for(n = 0; n < 3; n++)
      {
         Timers[n].Counter &= 0xFFFF;
         Timers[n].Target &= 0xFFFF;
         Timers[n].Div8Counter &= 0x7;
      }
   }

   return(ret);
}

uint32_t TIMER_GetRegister(unsigned int which, char *special, const uint32_t special_len)
{
   int tw = (which >> 4) & 0x3;

   switch(which & 0xF)
   {
      case TIMER_GSREG_COUNTER0:
         return Timers[tw].Counter;
      case TIMER_GSREG_MODE0:
         return Timers[tw].Mode;
      case TIMER_GSREG_TARGET0:
         return Timers[tw].Target;
   }

   return 0;
}

void TIMER_SetRegister(unsigned int which, uint32_t value)
{
   int tw = (which >> 4) & 0x3;

   switch(which & 0xF)
   {
      case TIMER_GSREG_COUNTER0:
            Timers[tw].Counter = value & 0xFFFF;
            break;

      case TIMER_GSREG_MODE0:
            Timers[tw].Mode = value & 0xFFFF;
            break;

      case TIMER_GSREG_TARGET0:
            Timers[tw].Target = value & 0xFFFF;
            break;
   }

   if(Timers[tw].Counter == Timers[tw].Target)
      TimerMatch(tw);
}
