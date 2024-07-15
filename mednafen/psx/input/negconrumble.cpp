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

#include "../psx.h"
#include "../frontio.h"
#include "negconrumble.h"

#include "../../mednafen.h"
#include "../../mednafen-endian.h"

/*
   TODO:
	If we ever call Update() more than once per video frame(IE 50/60Hz), we'll need to add debounce logic to the analog mode button evaluation code.
*/

/* Notes:
     While The neGcon itself never had rumble motors, quite a handfull of neGcon supported games support sending haptic feedback while in neGcon mode.
     By implementing a modified DualShock controller profile that identifies as a neGcon, this behavior can be replicated.

     Analog toggling has been left in place because, while the controller is essentially useless in 'Digital' mode, some games that support rumble refuse
     to continue past a controller error screen when starting in 'Analog' mode, so the toggle allows one to bypass this screen.

   Original notes from dualshock.cpp:
     Both DA and DS style rumblings work in both analog and digital modes.

     Regarding getting Dual Shock style rumble working, Sony is evil and/or mean.  The owl tells me to burn Sony with boiling oil.

     To enable Dual Shock-style rumble, the game has to at least enter MAD MUNCHKINS MODE with command 0x43, and send the appropriate data(not the actual rumble type data per-se)
     with command 0x4D.

     DualAnalog-style rumble support seems to be borked until power loss if MAD MUNCHKINS MODE is even entered once...investigate further.

     Command 0x44 in MAD MUNCHKINS MODE can turn on/off analog mode(and the light with it).

     Command 0x42 in MAD MUNCHKINS MODE will return the analog mode style gamepad data, even when analog mode is off.  In combination with command 0x44, this could hypothetically
     be used for using the light in the gamepad as some kind of game mechanic).

     Dual Analog-style rumble notes(some of which may apply to DS too):
	Rumble appears to stop if you hold DTR active(TODO: for how long? instant?). (TODO: investigate if it's still stopped even if a memory card device number is sent.  It may be, since rumble may
							      cause excessive current draw in combination with memory card access)

	Rumble will work even if you interrupt the communication process after you've sent the rumble data(via command 0x42).
		Though if you interrupt it when you've only sent partial rumble data, dragons will eat you and I don't know(seems to have timing-dependent or random effects or something;
	        based on VERY ROUGH testing).
*/

class InputDevice_neGconRumble : public InputDevice
{
   public:

      InputDevice_neGconRumble(const std::string &arg_name);
      virtual ~InputDevice_neGconRumble();

      virtual void Power(void);
      virtual int StateAction(StateMem* sm, int load, int data_only, const char* section_name);
      virtual void Update(const int32_t timestamp);
      virtual void ResetTS(void);
      virtual void UpdateInput(const void *data);

      virtual void SetAMCT(bool enabled);
      //
      //
      //
      virtual void SetDTR(bool new_dtr);
      virtual bool GetDSR(void);
      virtual bool Clock(bool TxD, int32 &dsr_pulse_delay);

   private:

      void CheckManualAnaModeChange(void);

      //
      //
      bool cur_ana_button_state;
      bool prev_ana_button_state;
      int64 combo_anatoggle_counter;
      //

      bool da_rumble_compat;

      bool analog_mode;
      bool analog_mode_locked;

      bool mad_munchkins;
      uint8 rumble_magic[6];

      uint8 rumble_param[2];

      bool dtr;

      uint8 buttons[2];
      uint8 twist;
      uint8 anabuttons[3];

      int32 command_phase;
      uint32 bitpos;
      uint8 receive_buffer;

      uint8 command;

      uint8 transmit_buffer[8];
      uint32 transmit_pos;
      uint32 transmit_count;

      //
      //
      //
      bool am_prev_info;
      bool aml_prev_info;
      std::string gp_name;
      int32_t lastts;

      //
      //
      bool amct_enabled;
};

InputDevice_neGconRumble::InputDevice_neGconRumble(const std::string &name)
{
   gp_name = name;
   Power();
   am_prev_info = analog_mode;
   aml_prev_info = analog_mode_locked;
   amct_enabled = false;
}

InputDevice_neGconRumble::~InputDevice_neGconRumble()
{

}

void InputDevice_neGconRumble::Update(const int32_t timestamp)
{
   lastts = timestamp;
}

void InputDevice_neGconRumble::ResetTS(void)
{
   //printf("%lld\n", combo_anatoggle_counter);
   if(combo_anatoggle_counter >= 0)
      combo_anatoggle_counter += lastts;
   lastts = 0;
}

#ifdef __LIBRETRO__
extern bool setting_apply_analog_default;
#endif

void InputDevice_neGconRumble::SetAMCT(bool enabled)
{
   bool amct_prev_info = amct_enabled;
   amct_enabled = enabled;
   analog_mode = true;

   if (amct_prev_info == analog_mode && amct_prev_info == amct_enabled)
      return;

   am_prev_info = analog_mode;
}

//
// This simulates the behavior of the actual DualShock(analog toggle button evaluation is suspended while DTR is active).
// Call in Update(), and whenever dtr goes inactive in the port access code.
void InputDevice_neGconRumble::CheckManualAnaModeChange(void)
{
   if(!dtr)
   {
      bool need_mode_toggle = false;

      if(buttons[0] == 0x01) // Map this to Retropad Select as most other buttons used by the analog toggle combo don't work
      {
         if(combo_anatoggle_counter == -1)
            combo_anatoggle_counter = 0;
         else if(combo_anatoggle_counter >= (44100))
         {
            need_mode_toggle = true;
            combo_anatoggle_counter = -2;
         }
      }
      else
         combo_anatoggle_counter = -1;

      if(need_mode_toggle)
         analog_mode = !analog_mode;

      prev_ana_button_state = cur_ana_button_state; 	// Don't move this outside of the if(!dtr) block!
   }
}

void InputDevice_neGconRumble::Power(void)
{
   combo_anatoggle_counter = -2;
   lastts = 0;
   //
   //

   dtr = 0;

   buttons[0] = buttons[1] = 0;
   twist = 0;
   anabuttons[0] = 0;
   anabuttons[1] = 0;
   anabuttons[2] = 0;

   command_phase = 0;

   bitpos = 0;

   receive_buffer = 0;

   command = 0;

   memset(transmit_buffer, 0, sizeof(transmit_buffer));

   transmit_pos = 0;
   transmit_count = 0;

   analog_mode = true;
   analog_mode_locked = false;

   mad_munchkins = false;
   memset(rumble_magic, 0xFF, sizeof(rumble_magic));
   memset(rumble_param, 0, sizeof(rumble_param));

   da_rumble_compat = true;

   prev_ana_button_state = false;
}

int InputDevice_neGconRumble::StateAction(StateMem* sm, int load, int data_only, const char* section_name)
{
   SFORMAT StateRegs[] =
   {
      SFVAR(cur_ana_button_state),
      SFVAR(prev_ana_button_state),
      SFVAR(combo_anatoggle_counter),

      SFVAR(da_rumble_compat),

      SFVAR(analog_mode),
      SFVAR(analog_mode_locked),

      SFVAR(mad_munchkins),
      SFARRAY(rumble_magic, sizeof(rumble_magic)),

      SFARRAY(rumble_param, sizeof(rumble_param)),

      SFVAR(dtr),

      SFARRAY(buttons, sizeof(buttons)),

      SFVAR(command_phase),
      SFVAR(bitpos),
      SFVAR(receive_buffer),

      SFVAR(command),

      SFARRAY(transmit_buffer, sizeof(transmit_buffer)),
      SFVAR(transmit_pos),
      SFVAR(transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)transmit_pos + transmit_count) > sizeof(transmit_buffer))
      {
         transmit_pos = 0;
         transmit_count = 0;
      }
   }

   return(ret);
}

void InputDevice_neGconRumble::UpdateInput(const void *data)
{
   uint8 *d8 = (uint8 *)data;

   buttons[0] = d8[0];
   buttons[1] = d8[1];
   cur_ana_button_state = d8[2] & 0x01;

   twist = ((32768 + MDFN_de32lsb<false>((const uint8 *)data + 4) - (((int32)MDFN_de32lsb<false>((const uint8 *)data + 8) * 32768 + 16383) / 32767)) * 255 + 32767) / 65535;

   anabuttons[0] = (MDFN_de32lsb<false>((const uint8 *)data + 12) * 255 + 16383) / 32767; 
   anabuttons[1] = (MDFN_de32lsb<false>((const uint8 *)data + 16) * 255 + 16383) / 32767;
   anabuttons[2] = (MDFN_de32lsb<false>((const uint8 *)data + 20) * 255 + 16383) / 32767;

   //printf("%02x %02x %02x %02x\n", twist, anabuttons[0], anabuttons[1], anabuttons[2]);

   //printf("RUMBLE: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", rumble_magic[0], rumble_magic[1], rumble_magic[2], rumble_magic[3], rumble_magic[4], rumble_magic[5]);
   //printf("%d, 0x%02x 0x%02x\n", da_rumble_compat, rumble_param[0], rumble_param[1]);
   if(da_rumble_compat == false)
   {
      uint8 sneaky_weaky = 0;

      if(rumble_param[0] == 0x01)
         sneaky_weaky = 0xFF;

      //revert to 0.9.33, should be fixed on libretro side instead
      //MDFN_en16lsb(rumb_dp, (sneaky_weaky << 0) | (rumble_param[1] << 8));

      MDFN_en32lsb<false>(&d8[4 + 32 + 0], (sneaky_weaky << 0) | (rumble_param[1] << 8));
   }
   else
   {
      uint8 sneaky_weaky = 0;

      if(((rumble_param[0] & 0xC0) == 0x40) && ((rumble_param[1] & 0x01) == 0x01))
         sneaky_weaky = 0xFF;

      //revert to 0.9.33, should be fixed on libretro side instead
      //MDFN_en16lsb(rumb_dp, sneaky_weaky << 0);
      MDFN_en32lsb<false>(&d8[4 + 32 + 0], sneaky_weaky << 0);
   }

   CheckManualAnaModeChange();

   if(am_prev_info != analog_mode || aml_prev_info != analog_mode_locked)
      MDFN_DispMessage(2, RETRO_LOG_INFO,
            RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
            "%s: neGcon mode is %s",
            gp_name.c_str(), analog_mode ? "ON" : "OFF");

   aml_prev_info = analog_mode_locked;
   am_prev_info = analog_mode;
}


void InputDevice_neGconRumble::SetDTR(bool new_dtr)
{
   const bool old_dtr = dtr;
   dtr = new_dtr;	// Set it to new state before we call CheckManualAnaModeChange().

   if(!old_dtr && dtr)
   {
      command_phase = 0;
      bitpos = 0;
      transmit_pos = 0;
      transmit_count = 0;
   }
   else if(old_dtr && !dtr)
   {
      CheckManualAnaModeChange();
      //if(bitpos || transmit_count)
      // printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
   }
}

bool InputDevice_neGconRumble::GetDSR(void)
{
   if(!dtr)
      return(0);

   if(!bitpos && transmit_count)
      return(1);

   return(0);
}

bool InputDevice_neGconRumble::Clock(bool TxD, int32 &dsr_pulse_delay)
{
   bool ret = 1;

   dsr_pulse_delay = 0;

   if(!dtr)
      return(1);

   if(transmit_count)
      ret = (transmit_buffer[transmit_pos] >> bitpos) & 1;

   receive_buffer &= ~(1 << bitpos);
   receive_buffer |= TxD << bitpos;
   bitpos = (bitpos + 1) & 0x7;

   if(!bitpos)
   {
      //if(command == 0x44)
      //if(command == 0x4D) //mad_munchkins) // || command == 0x43)
      // fprintf(stderr, "[PAD] Receive: %02x -- command=%02x, command_phase=%d, transmit_pos=%d\n", receive_buffer, command, command_phase, transmit_pos);

      if(transmit_count)
      {
         transmit_pos++;
         transmit_count--;
      }

      switch(command_phase)
      {
         case 0:
            if(receive_buffer != 0x01)
               command_phase = -1;
            else
            {
               if(mad_munchkins)
               {
                  transmit_buffer[0] = 0xF3;
                  transmit_pos = 0;
                  transmit_count = 1;
                  command_phase = 101;
               }
               else
               {
                  transmit_buffer[0] = analog_mode ? 0x23 : 0x41;
                  transmit_pos = 0;
                  transmit_count = 1;
                  command_phase++;
               }
            }
            break;

         case 1:
            command = receive_buffer;
            command_phase++;

            transmit_buffer[0] = 0x5A;

            //fprintf(stderr, "Gamepad command: 0x%02x\n", command);
            //if(command != 0x42 && command != 0x43)
            // fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command);

            if(command == 0x42)
            {
               transmit_buffer[0] = 0x5A;
               transmit_pos = 0;
               transmit_count = 1;
               command_phase = (command << 8) | 0x00;
            }
            else if(command == 0x43)
            {
               transmit_pos = 0;
               if(analog_mode)
               {
                  transmit_buffer[1] = 0xFF ^ buttons[0];
                  transmit_buffer[2] = 0xFF ^ buttons[1];
                  transmit_buffer[3] = twist;			// Twist, 0x00 through 0xFF, 0x80 center.
                  transmit_buffer[4] = anabuttons[0];		// Analog button I, 0x00 through 0xFF, 0x00 = no pressing, 0xFF = max.
                  transmit_buffer[5] = anabuttons[1];		// Analog button II, ""
                  transmit_buffer[6] = anabuttons[2];		// Left shoulder analog button, ""
                  transmit_count = 7;
               }
               else
               {
                  transmit_buffer[1] = 0xFF ^ buttons[0];
                  transmit_buffer[2] = 0xFF ^ buttons[1];
                  transmit_count = 3;
               }
            }
            else
            {
               command_phase = -1;
               transmit_buffer[1] = 0;
               transmit_buffer[2] = 0;
               transmit_pos = 0;
               transmit_count = 0;
            }
            break;

         case 2:
            {
               if(command == 0x43 && transmit_pos == 2 && (receive_buffer == 0x01))
               {
                  //fprintf(stderr, "Mad Munchkins mode entered!\n");
                  mad_munchkins = true;

                  if(da_rumble_compat)
                  {
                     rumble_param[0] = 0;
                     rumble_param[1] = 0;
                     da_rumble_compat = false;
                  }
                  command_phase = -1;
               }
            }
            break;

         case 101:
            command = receive_buffer;

            //fprintf(stderr, "Mad Munchkins DualShock command: 0x%02x\n", command);

            if(command >= 0x40 && command <= 0x4F)
            {
               transmit_buffer[0] = 0x5A;
               transmit_pos = 0;
               transmit_count = 1;
               command_phase = (command << 8) | 0x00;
            }
            else
            {
               transmit_count = 0;
               command_phase = -1;
            }
            break;

            /************************/
            /* MMMode 1, Command 0x40 */
            /************************/
         case 0x4000:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4001:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x41 */
            /************************/
         case 0x4100:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4101:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /**************************/
            /* MMMode 0&1, Command 0x42 */
            /**************************/
         case 0x4200:
            transmit_pos = 0;
            if(analog_mode || mad_munchkins)
            {
               transmit_buffer[0] = 0xFF ^ buttons[0];
               transmit_buffer[1] = 0xFF ^ buttons[1];
               transmit_buffer[2] = twist;			// Twist, 0x00 through 0xFF, 0x80 center.
               transmit_buffer[3] = anabuttons[0];		// Analog button I, 0x00 through 0xFF, 0x00 = no pressing, 0xFF = max.
               transmit_buffer[4] = anabuttons[1];		// Analog button II, ""
               transmit_buffer[5] = anabuttons[2];		// Left shoulder analog button, ""
               transmit_count = 6;
            }
            else
            {
               transmit_buffer[0] = 0xFF ^ buttons[0];
               transmit_buffer[1] = 0xFF ^ buttons[1];
               transmit_count = 2;

               if(!(rumble_magic[2] & 0xFE))
               {
                  transmit_buffer[transmit_count++] = 0x00;
                  transmit_buffer[transmit_count++] = 0x00;
               }
            }
            command_phase++;
            break;

         case 0x4201:			// Weak(in DS mode)
            if(da_rumble_compat)
               rumble_param[0] = receive_buffer;
            // Dualshock weak
            else if(rumble_magic[0] == 0x00 && rumble_magic[2] != 0x00 && rumble_magic[3] != 0x00 && rumble_magic[4] != 0x00 && rumble_magic[5] != 0x00)
               rumble_param[0] = receive_buffer;
            command_phase++;
            break;

         case 0x4202:
            if(da_rumble_compat)
               rumble_param[1] = receive_buffer;
            else if(rumble_magic[1] == 0x01)	// DualShock strong
               rumble_param[1] = receive_buffer;
            else if(rumble_magic[1] == 0x00 && rumble_magic[2] != 0x00 && rumble_magic[3] != 0x00 && rumble_magic[4] != 0x00 && rumble_magic[5] != 0x00)	// DualShock weak
               rumble_param[0] = receive_buffer;

            command_phase++;
            break;

         case 0x4203:
            if(da_rumble_compat)
            {

            }
            else if(rumble_magic[1] == 0x00 && rumble_magic[2] == 0x01)
               rumble_param[1] = receive_buffer;	// DualShock strong.
            command_phase++;	// Nowhere here we come!
            break;

            /************************/
            /* MMMode 1, Command 0x43 */
            /************************/
         case 0x4300:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4301:
            if(receive_buffer == 0x00)
            {
               //fprintf(stderr, "Mad Munchkins mode left!\n");
               mad_munchkins = false;
            }
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x44 */
            /************************/
         case 0x4400:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4401:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase++;

            // Ignores locking state.
            switch(receive_buffer)
            {
               case 0x00:
                  analog_mode = true;
                  //fprintf(stderr, "Analog mode disabled\n");
                  break;

               case 0x01:
                  analog_mode = true;
                  //fprintf(stderr, "Analog mode enabled\n");
                  break;
            }
            break;

         case 0x4402:
            switch(receive_buffer)
            {
               case 0x02:
                  analog_mode_locked = true;
                  break;

               case 0x03:
                  analog_mode_locked = true;
                  break;
            }
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x45 */
            /************************/
         case 0x4500:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0x01; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4501:
            transmit_buffer[0] = 0x02;
            transmit_buffer[1] = analog_mode ? 0x01 : 0x00;
            transmit_buffer[2] = 0x02;
            transmit_buffer[3] = 0x01;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x46 */
            /************************/
         case 0x4600:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4601:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x01;
               transmit_buffer[2] = 0x02;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x0A;
            }
            else if(receive_buffer == 0x01)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x01;
               transmit_buffer[2] = 0x01;
               transmit_buffer[3] = 0x01;
               transmit_buffer[4] = 0x14;
            }
            else
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x47 */
            /************************/
         case 0x4700:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4701:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x02;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x01;
               transmit_buffer[4] = 0x00;
            }
            else
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x48 */
            /************************/
         case 0x4800:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4801:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x01;
               transmit_buffer[4] = rumble_param[0];
            }
            else if(receive_buffer == 0x01)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x01;
               transmit_buffer[4] = rumble_param[1];
            }
            else
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x49 */
            /************************/
         case 0x4900:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4901:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4A */
            /************************/
         case 0x4A00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4A01:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4B */
            /************************/
         case 0x4B00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4B01:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4C */
            /************************/
         case 0x4C00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4C01:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x04;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }
            else if(receive_buffer == 0x01)
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x07;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }
            else
            {
               transmit_buffer[0] = 0x00;
               transmit_buffer[1] = 0x00;
               transmit_buffer[2] = 0x00;
               transmit_buffer[3] = 0x00;
               transmit_buffer[4] = 0x00;
            }

            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4D */
            /************************/
         case 0x4D00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = rumble_magic[0]; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4D01:
         case 0x4D02:
         case 0x4D03:
         case 0x4D04:
         case 0x4D05:
         case 0x4D06:
            {
               unsigned index = command_phase - 0x4D01;

               if(index < 5)
               {
                  transmit_buffer[0] = rumble_magic[1 + index];
                  transmit_pos = 0;
                  transmit_count = 1;
                  command_phase++;
               }
               else
                  command_phase = -1;

               rumble_magic[index] = receive_buffer;	 
            }
            break;

            /************************/
            /* MMMode 1, Command 0x4E */
            /************************/
         case 0x4E00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4E01:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x4F */
            /************************/
         case 0x4F00:
            if(receive_buffer == 0x00)
            {
               transmit_buffer[0] = 0; /**/ transmit_pos = 0; transmit_count = 1; /**/
               command_phase++;
            }
            else
               command_phase = -1;
            break;

         case 0x4F01:
            transmit_buffer[0] = 0x00;
            transmit_buffer[1] = 0x00;
            transmit_buffer[2] = 0x00;
            transmit_buffer[3] = 0x00;
            transmit_buffer[4] = 0x00;
            transmit_pos = 0;
            transmit_count = 5;
            command_phase = -1;
            break;
      }
   }

   if(!bitpos && transmit_count)
      dsr_pulse_delay = 0x40; //0x100;

   return(ret);
}

InputDevice *Device_neGconRumble_Create(const std::string &name)
{
   return new InputDevice_neGconRumble(name);
}


InputDeviceInputInfoStruct Device_neGconRumble_IDII[22] =
{
   { "select", "Analog(mode toggle)", 13, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { "start", "START", 4, IDIT_BUTTON, NULL },
   { "up", "D-Pad UP ↑", 0, IDIT_BUTTON, "down" },
   { "right", "D-Pad RIGHT →", 3, IDIT_BUTTON, "left" },
   { "down", "D-Pad DOWN ↓", 1, IDIT_BUTTON, "up" },
   { "left", "D-Pad LEFT ←", 2, IDIT_BUTTON, "right" },

   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { "r", "Right Shoulder", 12, IDIT_BUTTON },

   { "b", "B", 9, IDIT_BUTTON, NULL },
   { "a", "A", 10, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },
   { NULL, "empty", -1, IDIT_BUTTON, NULL },

   { "twist_cwise",  "Twist ↓|↑ (Analog, Turn Right)", 6, IDIT_BUTTON_ANALOG },
   { "twist_ccwise", "Twist ↑|↓ (Analog, Turn Left)", 5, IDIT_BUTTON_ANALOG },
   { "i", "I (Analog)", 8, IDIT_BUTTON_ANALOG },
   { "ii", "II (Analog)", 7, IDIT_BUTTON_ANALOG },

   { "l", "Left Shoulder (Analog)", 11, IDIT_BUTTON_ANALOG },

   { "rumble", "RUMBLE MONSTER RUMBA", 100, IDIT_RUMBLE },
};
