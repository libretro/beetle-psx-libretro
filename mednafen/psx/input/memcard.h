#ifndef __MDFN_PSX_INPUT_MEMCARD_H
#define __MDFN_PSX_INPUT_MEMCARD_H

InputDevice *Device_Memcard_Create(void);
void Device_Memcard_Power(InputDevice* memcard);
void Device_Memcard_Format(InputDevice *memcard);

#endif
