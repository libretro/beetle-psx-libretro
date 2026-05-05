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

/* TODO:
	Note to self: Emulating the SPU at more timing accuracy than sample, and emulating the whole SPU RAM write port FIFO thing and hypothetical periodic FIFO commit to
	SPU RAM(maybe every 32 CPU cycles, with exceptions?) will likely necessitate a much more timing-accurate CPU core, and emulation of the SPU delay register(or at least the
	effects of the standard value written to it), to avoid game glitches.  Probably more trouble than it's worth....

	SPU IRQ emulation isn't totally correct, behavior is kind of complex; run more tests on PS1.

	Test reverb upsampler on the real thing.

	Alter reverb algorithm to process in the pattern of L,R,L,R,L,R on each input sample, instead of doing both L and R on every 2 input samples(make
   sure the real thing does it this way too, I think it at least runs the downsampler this way).

	Alter reverb algorithm to perform saturation more often, as occurs on the real thing.

	See if sample flag & 0x8 does anything weird, like suppressing the program-readable block end flag setting.

	Determine the actual purpose of global register 0x2C(is it REALLY an address multiplier?  And if so, does it affect the reverb offsets too?)

	For ADSR and volume sweep, should the divider be reset to 0 on &0x8000 == true, or should the upper bit be cleared?

	Should shift occur on all stages of ADPCM sample decoding, or only at the end?

	On the real thing, there's some kind of weirdness with ADSR when you voice on when attack_rate(raw) = 0x7F; the envelope level register is repeatedly
	reset to 0, which you can see by manual writes to the envelope level register.  Normally in the attack phase when attack_rate = 0x7F, enveloping is 		effectively stuck/paused such that the value you write is sticky and won't be replaced or reset.  Note that after you voice on, you can write a new 		attack_rate < 0x7F, and enveloping will work "normally" again shortly afterwards.  You can even write an attack_rate of 0x7F at that point to pause 		enveloping 		clocking.  I doubt any games rely on this, but it's something to keep in mind if we ever need greater insight as to how the SPU 	functions at a low-level in 		order to emulate it at cycle granularity rather than sample granularity, and it may not be a bad idea to 		investigate this oddity further and emulate it in 		the future regardless.

	Voice 1 and 3 waveform output writes to SPURAM might not be correct(noted due to problems reading this area of SPU RAM on the real thing
	based on my expectations of how this should work).
*/

/*
 Notes:

	All addresses(for 16-bit access, at least) within the SPU address space appear to be fully read/write as if they were RAM, though
	values at some addresses(like the envelope current value) will be "overwritten" by the sound processing at certain times.

	32-bit and 8-bit reads act as if it were RAM(not tested with all addresses, but a few, assuming the rest are the same), but 8-bit writes
	to odd addresses appear to be ignored, and 8-bit writes to even addresses are treated as 16-bit writes(most likely, but, need to code custom assembly to
	fully test the upper 8 bits).  NOTE: the preceding information doesn't necessarily cover accesses with side effects, they still need to be tested; and it
	of course covers reads/writes from the point of view of software running on the CPU.

	It doesn't appear to be possible to enable FM on the first channel/voice(channel/voice 0).

	Lower bit of channel start address appears to be masked out to 0(such that ADPCM block decoding is always 8 16-bit units, 16 bytes, aligned), as far as
	block-decoding and flag-set program-readable loop address go.
*/

/*
 Update() isn't called on Read and Writes for performance reasons, it's called with sufficient granularity from the event
 system, though this will obviously need to change if we ever emulate the SPU with better precision than per-sample(pair).
*/

#include <stdint.h>

#include "../mednafen-types.h"
#include "../state.h"
#include "../state_helpers.h"
#include "../clamp.h"

#include <libretro.h>

#include "irq.h"
#include "cdc_c.h"
#include "spu.h"

uint32_t IntermediateBufferPos;
int16_t IntermediateBuffer[4096][2];

extern uint8_t spu_samples;

static const int16 FIR_Table[256][4] =
{
 { (int16)0x12c7, (int16)0x59b3, (int16)0x1307, (int16)0xffff },
 { (int16)0x1288, (int16)0x59b2, (int16)0x1347, (int16)0xffff },
 { (int16)0x1249, (int16)0x59b0, (int16)0x1388, (int16)0xffff },
 { (int16)0x120b, (int16)0x59ad, (int16)0x13c9, (int16)0xffff },
 { (int16)0x11cd, (int16)0x59a9, (int16)0x140b, (int16)0xffff },
 { (int16)0x118f, (int16)0x59a4, (int16)0x144d, (int16)0xffff },
 { (int16)0x1153, (int16)0x599e, (int16)0x1490, (int16)0xffff },
 { (int16)0x1116, (int16)0x5997, (int16)0x14d4, (int16)0xffff },
 { (int16)0x10db, (int16)0x598f, (int16)0x1517, (int16)0xffff },
 { (int16)0x109f, (int16)0x5986, (int16)0x155c, (int16)0xffff },
 { (int16)0x1065, (int16)0x597c, (int16)0x15a0, (int16)0xffff },
 { (int16)0x102a, (int16)0x5971, (int16)0x15e6, (int16)0xffff },
 { (int16)0x0ff1, (int16)0x5965, (int16)0x162c, (int16)0xffff },
 { (int16)0x0fb7, (int16)0x5958, (int16)0x1672, (int16)0xffff },
 { (int16)0x0f7f, (int16)0x5949, (int16)0x16b9, (int16)0xffff },
 { (int16)0x0f46, (int16)0x593a, (int16)0x1700, (int16)0xffff },
 { (int16)0x0f0f, (int16)0x592a, (int16)0x1747, (int16)0x0000 },
 { (int16)0x0ed7, (int16)0x5919, (int16)0x1790, (int16)0x0000 },
 { (int16)0x0ea1, (int16)0x5907, (int16)0x17d8, (int16)0x0000 },
 { (int16)0x0e6b, (int16)0x58f4, (int16)0x1821, (int16)0x0000 },
 { (int16)0x0e35, (int16)0x58e0, (int16)0x186b, (int16)0x0000 },
 { (int16)0x0e00, (int16)0x58cb, (int16)0x18b5, (int16)0x0000 },
 { (int16)0x0dcb, (int16)0x58b5, (int16)0x1900, (int16)0x0000 },
 { (int16)0x0d97, (int16)0x589e, (int16)0x194b, (int16)0x0001 },
 { (int16)0x0d63, (int16)0x5886, (int16)0x1996, (int16)0x0001 },
 { (int16)0x0d30, (int16)0x586d, (int16)0x19e2, (int16)0x0001 },
 { (int16)0x0cfd, (int16)0x5853, (int16)0x1a2e, (int16)0x0001 },
 { (int16)0x0ccb, (int16)0x5838, (int16)0x1a7b, (int16)0x0002 },
 { (int16)0x0c99, (int16)0x581c, (int16)0x1ac8, (int16)0x0002 },
 { (int16)0x0c68, (int16)0x57ff, (int16)0x1b16, (int16)0x0002 },
 { (int16)0x0c38, (int16)0x57e2, (int16)0x1b64, (int16)0x0003 },
 { (int16)0x0c07, (int16)0x57c3, (int16)0x1bb3, (int16)0x0003 },
 { (int16)0x0bd8, (int16)0x57a3, (int16)0x1c02, (int16)0x0003 },
 { (int16)0x0ba9, (int16)0x5782, (int16)0x1c51, (int16)0x0004 },
 { (int16)0x0b7a, (int16)0x5761, (int16)0x1ca1, (int16)0x0004 },
 { (int16)0x0b4c, (int16)0x573e, (int16)0x1cf1, (int16)0x0005 },
 { (int16)0x0b1e, (int16)0x571b, (int16)0x1d42, (int16)0x0005 },
 { (int16)0x0af1, (int16)0x56f6, (int16)0x1d93, (int16)0x0006 },
 { (int16)0x0ac4, (int16)0x56d1, (int16)0x1de5, (int16)0x0007 },
 { (int16)0x0a98, (int16)0x56ab, (int16)0x1e37, (int16)0x0007 },
 { (int16)0x0a6c, (int16)0x5684, (int16)0x1e89, (int16)0x0008 },
 { (int16)0x0a40, (int16)0x565b, (int16)0x1edc, (int16)0x0009 },
 { (int16)0x0a16, (int16)0x5632, (int16)0x1f2f, (int16)0x0009 },
 { (int16)0x09eb, (int16)0x5609, (int16)0x1f82, (int16)0x000a },
 { (int16)0x09c1, (int16)0x55de, (int16)0x1fd6, (int16)0x000b },
 { (int16)0x0998, (int16)0x55b2, (int16)0x202a, (int16)0x000c },
 { (int16)0x096f, (int16)0x5585, (int16)0x207f, (int16)0x000d },
 { (int16)0x0946, (int16)0x5558, (int16)0x20d4, (int16)0x000e },
 { (int16)0x091e, (int16)0x5529, (int16)0x2129, (int16)0x000f },
 { (int16)0x08f7, (int16)0x54fa, (int16)0x217f, (int16)0x0010 },
 { (int16)0x08d0, (int16)0x54ca, (int16)0x21d5, (int16)0x0011 },
 { (int16)0x08a9, (int16)0x5499, (int16)0x222c, (int16)0x0012 },
 { (int16)0x0883, (int16)0x5467, (int16)0x2282, (int16)0x0013 },
 { (int16)0x085d, (int16)0x5434, (int16)0x22da, (int16)0x0015 },
 { (int16)0x0838, (int16)0x5401, (int16)0x2331, (int16)0x0016 },
 { (int16)0x0813, (int16)0x53cc, (int16)0x2389, (int16)0x0018 },
 { (int16)0x07ef, (int16)0x5397, (int16)0x23e1, (int16)0x0019 },
 { (int16)0x07cb, (int16)0x5361, (int16)0x2439, (int16)0x001b },
 { (int16)0x07a7, (int16)0x532a, (int16)0x2492, (int16)0x001c },
 { (int16)0x0784, (int16)0x52f3, (int16)0x24eb, (int16)0x001e },
 { (int16)0x0762, (int16)0x52ba, (int16)0x2545, (int16)0x0020 },
 { (int16)0x0740, (int16)0x5281, (int16)0x259e, (int16)0x0021 },
 { (int16)0x071e, (int16)0x5247, (int16)0x25f8, (int16)0x0023 },
 { (int16)0x06fd, (int16)0x520c, (int16)0x2653, (int16)0x0025 },
 { (int16)0x06dc, (int16)0x51d0, (int16)0x26ad, (int16)0x0027 },
 { (int16)0x06bb, (int16)0x5194, (int16)0x2708, (int16)0x0029 },
 { (int16)0x069b, (int16)0x5156, (int16)0x2763, (int16)0x002c },
 { (int16)0x067c, (int16)0x5118, (int16)0x27be, (int16)0x002e },
 { (int16)0x065c, (int16)0x50da, (int16)0x281a, (int16)0x0030 },
 { (int16)0x063e, (int16)0x509a, (int16)0x2876, (int16)0x0033 },
 { (int16)0x061f, (int16)0x505a, (int16)0x28d2, (int16)0x0035 },
 { (int16)0x0601, (int16)0x5019, (int16)0x292e, (int16)0x0038 },
 { (int16)0x05e4, (int16)0x4fd7, (int16)0x298b, (int16)0x003a },
 { (int16)0x05c7, (int16)0x4f95, (int16)0x29e7, (int16)0x003d },
 { (int16)0x05aa, (int16)0x4f52, (int16)0x2a44, (int16)0x0040 },
 { (int16)0x058e, (int16)0x4f0e, (int16)0x2aa1, (int16)0x0043 },
 { (int16)0x0572, (int16)0x4ec9, (int16)0x2aff, (int16)0x0046 },
 { (int16)0x0556, (int16)0x4e84, (int16)0x2b5c, (int16)0x0049 },
 { (int16)0x053b, (int16)0x4e3e, (int16)0x2bba, (int16)0x004d },
 { (int16)0x0520, (int16)0x4df7, (int16)0x2c18, (int16)0x0050 },
 { (int16)0x0506, (int16)0x4db0, (int16)0x2c76, (int16)0x0054 },
 { (int16)0x04ec, (int16)0x4d68, (int16)0x2cd4, (int16)0x0057 },
 { (int16)0x04d2, (int16)0x4d20, (int16)0x2d33, (int16)0x005b },
 { (int16)0x04b9, (int16)0x4cd7, (int16)0x2d91, (int16)0x005f },
 { (int16)0x04a0, (int16)0x4c8d, (int16)0x2df0, (int16)0x0063 },
 { (int16)0x0488, (int16)0x4c42, (int16)0x2e4f, (int16)0x0067 },
 { (int16)0x0470, (int16)0x4bf7, (int16)0x2eae, (int16)0x006b },
 { (int16)0x0458, (int16)0x4bac, (int16)0x2f0d, (int16)0x006f },
 { (int16)0x0441, (int16)0x4b5f, (int16)0x2f6c, (int16)0x0074 },
 { (int16)0x042a, (int16)0x4b13, (int16)0x2fcc, (int16)0x0078 },
 { (int16)0x0413, (int16)0x4ac5, (int16)0x302b, (int16)0x007d },
 { (int16)0x03fc, (int16)0x4a77, (int16)0x308b, (int16)0x0082 },
 { (int16)0x03e7, (int16)0x4a29, (int16)0x30ea, (int16)0x0087 },
 { (int16)0x03d1, (int16)0x49d9, (int16)0x314a, (int16)0x008c },
 { (int16)0x03bc, (int16)0x498a, (int16)0x31aa, (int16)0x0091 },
 { (int16)0x03a7, (int16)0x493a, (int16)0x3209, (int16)0x0096 },
 { (int16)0x0392, (int16)0x48e9, (int16)0x3269, (int16)0x009c },
 { (int16)0x037e, (int16)0x4898, (int16)0x32c9, (int16)0x00a1 },
 { (int16)0x036a, (int16)0x4846, (int16)0x3329, (int16)0x00a7 },
 { (int16)0x0356, (int16)0x47f4, (int16)0x3389, (int16)0x00ad },
 { (int16)0x0343, (int16)0x47a1, (int16)0x33e9, (int16)0x00b3 },
 { (int16)0x0330, (int16)0x474e, (int16)0x3449, (int16)0x00ba },
 { (int16)0x031d, (int16)0x46fa, (int16)0x34a9, (int16)0x00c0 },
 { (int16)0x030b, (int16)0x46a6, (int16)0x3509, (int16)0x00c7 },
 { (int16)0x02f9, (int16)0x4651, (int16)0x3569, (int16)0x00cd },
 { (int16)0x02e7, (int16)0x45fc, (int16)0x35c9, (int16)0x00d4 },
 { (int16)0x02d6, (int16)0x45a6, (int16)0x3629, (int16)0x00db },
 { (int16)0x02c4, (int16)0x4550, (int16)0x3689, (int16)0x00e3 },
 { (int16)0x02b4, (int16)0x44fa, (int16)0x36e8, (int16)0x00ea },
 { (int16)0x02a3, (int16)0x44a3, (int16)0x3748, (int16)0x00f2 },
 { (int16)0x0293, (int16)0x444c, (int16)0x37a8, (int16)0x00fa },
 { (int16)0x0283, (int16)0x43f4, (int16)0x3807, (int16)0x0101 },
 { (int16)0x0273, (int16)0x439c, (int16)0x3867, (int16)0x010a },
 { (int16)0x0264, (int16)0x4344, (int16)0x38c6, (int16)0x0112 },
 { (int16)0x0255, (int16)0x42eb, (int16)0x3926, (int16)0x011b },
 { (int16)0x0246, (int16)0x4292, (int16)0x3985, (int16)0x0123 },
 { (int16)0x0237, (int16)0x4239, (int16)0x39e4, (int16)0x012c },
 { (int16)0x0229, (int16)0x41df, (int16)0x3a43, (int16)0x0135 },
 { (int16)0x021b, (int16)0x4185, (int16)0x3aa2, (int16)0x013f },
 { (int16)0x020d, (int16)0x412a, (int16)0x3b00, (int16)0x0148 },
 { (int16)0x0200, (int16)0x40d0, (int16)0x3b5f, (int16)0x0152 },
 { (int16)0x01f2, (int16)0x4074, (int16)0x3bbd, (int16)0x015c },
 { (int16)0x01e5, (int16)0x4019, (int16)0x3c1b, (int16)0x0166 },
 { (int16)0x01d9, (int16)0x3fbd, (int16)0x3c79, (int16)0x0171 },
 { (int16)0x01cc, (int16)0x3f62, (int16)0x3cd7, (int16)0x017b },
 { (int16)0x01c0, (int16)0x3f05, (int16)0x3d35, (int16)0x0186 },
 { (int16)0x01b4, (int16)0x3ea9, (int16)0x3d92, (int16)0x0191 },
 { (int16)0x01a8, (int16)0x3e4c, (int16)0x3def, (int16)0x019c },
 { (int16)0x019c, (int16)0x3def, (int16)0x3e4c, (int16)0x01a8 },
 { (int16)0x0191, (int16)0x3d92, (int16)0x3ea9, (int16)0x01b4 },
 { (int16)0x0186, (int16)0x3d35, (int16)0x3f05, (int16)0x01c0 },
 { (int16)0x017b, (int16)0x3cd7, (int16)0x3f62, (int16)0x01cc },
 { (int16)0x0171, (int16)0x3c79, (int16)0x3fbd, (int16)0x01d9 },
 { (int16)0x0166, (int16)0x3c1b, (int16)0x4019, (int16)0x01e5 },
 { (int16)0x015c, (int16)0x3bbd, (int16)0x4074, (int16)0x01f2 },
 { (int16)0x0152, (int16)0x3b5f, (int16)0x40d0, (int16)0x0200 },
 { (int16)0x0148, (int16)0x3b00, (int16)0x412a, (int16)0x020d },
 { (int16)0x013f, (int16)0x3aa2, (int16)0x4185, (int16)0x021b },
 { (int16)0x0135, (int16)0x3a43, (int16)0x41df, (int16)0x0229 },
 { (int16)0x012c, (int16)0x39e4, (int16)0x4239, (int16)0x0237 },
 { (int16)0x0123, (int16)0x3985, (int16)0x4292, (int16)0x0246 },
 { (int16)0x011b, (int16)0x3926, (int16)0x42eb, (int16)0x0255 },
 { (int16)0x0112, (int16)0x38c6, (int16)0x4344, (int16)0x0264 },
 { (int16)0x010a, (int16)0x3867, (int16)0x439c, (int16)0x0273 },
 { (int16)0x0101, (int16)0x3807, (int16)0x43f4, (int16)0x0283 },
 { (int16)0x00fa, (int16)0x37a8, (int16)0x444c, (int16)0x0293 },
 { (int16)0x00f2, (int16)0x3748, (int16)0x44a3, (int16)0x02a3 },
 { (int16)0x00ea, (int16)0x36e8, (int16)0x44fa, (int16)0x02b4 },
 { (int16)0x00e3, (int16)0x3689, (int16)0x4550, (int16)0x02c4 },
 { (int16)0x00db, (int16)0x3629, (int16)0x45a6, (int16)0x02d6 },
 { (int16)0x00d4, (int16)0x35c9, (int16)0x45fc, (int16)0x02e7 },
 { (int16)0x00cd, (int16)0x3569, (int16)0x4651, (int16)0x02f9 },
 { (int16)0x00c7, (int16)0x3509, (int16)0x46a6, (int16)0x030b },
 { (int16)0x00c0, (int16)0x34a9, (int16)0x46fa, (int16)0x031d },
 { (int16)0x00ba, (int16)0x3449, (int16)0x474e, (int16)0x0330 },
 { (int16)0x00b3, (int16)0x33e9, (int16)0x47a1, (int16)0x0343 },
 { (int16)0x00ad, (int16)0x3389, (int16)0x47f4, (int16)0x0356 },
 { (int16)0x00a7, (int16)0x3329, (int16)0x4846, (int16)0x036a },
 { (int16)0x00a1, (int16)0x32c9, (int16)0x4898, (int16)0x037e },
 { (int16)0x009c, (int16)0x3269, (int16)0x48e9, (int16)0x0392 },
 { (int16)0x0096, (int16)0x3209, (int16)0x493a, (int16)0x03a7 },
 { (int16)0x0091, (int16)0x31aa, (int16)0x498a, (int16)0x03bc },
 { (int16)0x008c, (int16)0x314a, (int16)0x49d9, (int16)0x03d1 },
 { (int16)0x0087, (int16)0x30ea, (int16)0x4a29, (int16)0x03e7 },
 { (int16)0x0082, (int16)0x308b, (int16)0x4a77, (int16)0x03fc },
 { (int16)0x007d, (int16)0x302b, (int16)0x4ac5, (int16)0x0413 },
 { (int16)0x0078, (int16)0x2fcc, (int16)0x4b13, (int16)0x042a },
 { (int16)0x0074, (int16)0x2f6c, (int16)0x4b5f, (int16)0x0441 },
 { (int16)0x006f, (int16)0x2f0d, (int16)0x4bac, (int16)0x0458 },
 { (int16)0x006b, (int16)0x2eae, (int16)0x4bf7, (int16)0x0470 },
 { (int16)0x0067, (int16)0x2e4f, (int16)0x4c42, (int16)0x0488 },
 { (int16)0x0063, (int16)0x2df0, (int16)0x4c8d, (int16)0x04a0 },
 { (int16)0x005f, (int16)0x2d91, (int16)0x4cd7, (int16)0x04b9 },
 { (int16)0x005b, (int16)0x2d33, (int16)0x4d20, (int16)0x04d2 },
 { (int16)0x0057, (int16)0x2cd4, (int16)0x4d68, (int16)0x04ec },
 { (int16)0x0054, (int16)0x2c76, (int16)0x4db0, (int16)0x0506 },
 { (int16)0x0050, (int16)0x2c18, (int16)0x4df7, (int16)0x0520 },
 { (int16)0x004d, (int16)0x2bba, (int16)0x4e3e, (int16)0x053b },
 { (int16)0x0049, (int16)0x2b5c, (int16)0x4e84, (int16)0x0556 },
 { (int16)0x0046, (int16)0x2aff, (int16)0x4ec9, (int16)0x0572 },
 { (int16)0x0043, (int16)0x2aa1, (int16)0x4f0e, (int16)0x058e },
 { (int16)0x0040, (int16)0x2a44, (int16)0x4f52, (int16)0x05aa },
 { (int16)0x003d, (int16)0x29e7, (int16)0x4f95, (int16)0x05c7 },
 { (int16)0x003a, (int16)0x298b, (int16)0x4fd7, (int16)0x05e4 },
 { (int16)0x0038, (int16)0x292e, (int16)0x5019, (int16)0x0601 },
 { (int16)0x0035, (int16)0x28d2, (int16)0x505a, (int16)0x061f },
 { (int16)0x0033, (int16)0x2876, (int16)0x509a, (int16)0x063e },
 { (int16)0x0030, (int16)0x281a, (int16)0x50da, (int16)0x065c },
 { (int16)0x002e, (int16)0x27be, (int16)0x5118, (int16)0x067c },
 { (int16)0x002c, (int16)0x2763, (int16)0x5156, (int16)0x069b },
 { (int16)0x0029, (int16)0x2708, (int16)0x5194, (int16)0x06bb },
 { (int16)0x0027, (int16)0x26ad, (int16)0x51d0, (int16)0x06dc },
 { (int16)0x0025, (int16)0x2653, (int16)0x520c, (int16)0x06fd },
 { (int16)0x0023, (int16)0x25f8, (int16)0x5247, (int16)0x071e },
 { (int16)0x0021, (int16)0x259e, (int16)0x5281, (int16)0x0740 },
 { (int16)0x0020, (int16)0x2545, (int16)0x52ba, (int16)0x0762 },
 { (int16)0x001e, (int16)0x24eb, (int16)0x52f3, (int16)0x0784 },
 { (int16)0x001c, (int16)0x2492, (int16)0x532a, (int16)0x07a7 },
 { (int16)0x001b, (int16)0x2439, (int16)0x5361, (int16)0x07cb },
 { (int16)0x0019, (int16)0x23e1, (int16)0x5397, (int16)0x07ef },
 { (int16)0x0018, (int16)0x2389, (int16)0x53cc, (int16)0x0813 },
 { (int16)0x0016, (int16)0x2331, (int16)0x5401, (int16)0x0838 },
 { (int16)0x0015, (int16)0x22da, (int16)0x5434, (int16)0x085d },
 { (int16)0x0013, (int16)0x2282, (int16)0x5467, (int16)0x0883 },
 { (int16)0x0012, (int16)0x222c, (int16)0x5499, (int16)0x08a9 },
 { (int16)0x0011, (int16)0x21d5, (int16)0x54ca, (int16)0x08d0 },
 { (int16)0x0010, (int16)0x217f, (int16)0x54fa, (int16)0x08f7 },
 { (int16)0x000f, (int16)0x2129, (int16)0x5529, (int16)0x091e },
 { (int16)0x000e, (int16)0x20d4, (int16)0x5558, (int16)0x0946 },
 { (int16)0x000d, (int16)0x207f, (int16)0x5585, (int16)0x096f },
 { (int16)0x000c, (int16)0x202a, (int16)0x55b2, (int16)0x0998 },
 { (int16)0x000b, (int16)0x1fd6, (int16)0x55de, (int16)0x09c1 },
 { (int16)0x000a, (int16)0x1f82, (int16)0x5609, (int16)0x09eb },
 { (int16)0x0009, (int16)0x1f2f, (int16)0x5632, (int16)0x0a16 },
 { (int16)0x0009, (int16)0x1edc, (int16)0x565b, (int16)0x0a40 },
 { (int16)0x0008, (int16)0x1e89, (int16)0x5684, (int16)0x0a6c },
 { (int16)0x0007, (int16)0x1e37, (int16)0x56ab, (int16)0x0a98 },
 { (int16)0x0007, (int16)0x1de5, (int16)0x56d1, (int16)0x0ac4 },
 { (int16)0x0006, (int16)0x1d93, (int16)0x56f6, (int16)0x0af1 },
 { (int16)0x0005, (int16)0x1d42, (int16)0x571b, (int16)0x0b1e },
 { (int16)0x0005, (int16)0x1cf1, (int16)0x573e, (int16)0x0b4c },
 { (int16)0x0004, (int16)0x1ca1, (int16)0x5761, (int16)0x0b7a },
 { (int16)0x0004, (int16)0x1c51, (int16)0x5782, (int16)0x0ba9 },
 { (int16)0x0003, (int16)0x1c02, (int16)0x57a3, (int16)0x0bd8 },
 { (int16)0x0003, (int16)0x1bb3, (int16)0x57c3, (int16)0x0c07 },
 { (int16)0x0003, (int16)0x1b64, (int16)0x57e2, (int16)0x0c38 },
 { (int16)0x0002, (int16)0x1b16, (int16)0x57ff, (int16)0x0c68 },
 { (int16)0x0002, (int16)0x1ac8, (int16)0x581c, (int16)0x0c99 },
 { (int16)0x0002, (int16)0x1a7b, (int16)0x5838, (int16)0x0ccb },
 { (int16)0x0001, (int16)0x1a2e, (int16)0x5853, (int16)0x0cfd },
 { (int16)0x0001, (int16)0x19e2, (int16)0x586d, (int16)0x0d30 },
 { (int16)0x0001, (int16)0x1996, (int16)0x5886, (int16)0x0d63 },
 { (int16)0x0001, (int16)0x194b, (int16)0x589e, (int16)0x0d97 },
 { (int16)0x0000, (int16)0x1900, (int16)0x58b5, (int16)0x0dcb },
 { (int16)0x0000, (int16)0x18b5, (int16)0x58cb, (int16)0x0e00 },
 { (int16)0x0000, (int16)0x186b, (int16)0x58e0, (int16)0x0e35 },
 { (int16)0x0000, (int16)0x1821, (int16)0x58f4, (int16)0x0e6b },
 { (int16)0x0000, (int16)0x17d8, (int16)0x5907, (int16)0x0ea1 },
 { (int16)0x0000, (int16)0x1790, (int16)0x5919, (int16)0x0ed7 },
 { (int16)0x0000, (int16)0x1747, (int16)0x592a, (int16)0x0f0f },
 { (int16)0xffff, (int16)0x1700, (int16)0x593a, (int16)0x0f46 },
 { (int16)0xffff, (int16)0x16b9, (int16)0x5949, (int16)0x0f7f },
 { (int16)0xffff, (int16)0x1672, (int16)0x5958, (int16)0x0fb7 },
 { (int16)0xffff, (int16)0x162c, (int16)0x5965, (int16)0x0ff1 },
 { (int16)0xffff, (int16)0x15e6, (int16)0x5971, (int16)0x102a },
 { (int16)0xffff, (int16)0x15a0, (int16)0x597c, (int16)0x1065 },
 { (int16)0xffff, (int16)0x155c, (int16)0x5986, (int16)0x109f },
 { (int16)0xffff, (int16)0x1517, (int16)0x598f, (int16)0x10db },
 { (int16)0xffff, (int16)0x14d4, (int16)0x5997, (int16)0x1116 },
 { (int16)0xffff, (int16)0x1490, (int16)0x599e, (int16)0x1153 },
 { (int16)0xffff, (int16)0x144d, (int16)0x59a4, (int16)0x118f },
 { (int16)0xffff, (int16)0x140b, (int16)0x59a9, (int16)0x11cd },
 { (int16)0xffff, (int16)0x13c9, (int16)0x59ad, (int16)0x120b },
 { (int16)0xffff, (int16)0x1388, (int16)0x59b0, (int16)0x1249 },
 { (int16)0xffff, (int16)0x1347, (int16)0x59b2, (int16)0x1288 },
 { (int16)0xffff, (int16)0x1307, (int16)0x59b3, (int16)0x12c7 },
};

/*
 * SPU module state. Previously held inside a heap-allocated PS_SPU
 * class instance pointed to by `PSX_SPU`; now plain file-scope
 * statics. Init zeroes the state, Kill is a no-op - no heap, no
 * leak surface, no failure path for allocation.
 *
 * The register block is the only awkward shape: hardware exposes
 * a 0x100-entry uint16 array but with various named sub-regions
 * overlapping it (Voice regs, global regs with named SPUStatus,
 * reverb regs with named coefficient fields). Pre-conversion this
 * was an anonymous union with anonymous struct nesting, which is
 * a C++ feature; portable C can't use anonymous structs inside
 * unions. The conversion gives every nested aggregate a name and
 * accesses go through the explicit chain (e.g.
 * regs.s.global.s.SPUStatus). The `s` field name is short for
 * "structured" - just a tag to navigate the union.
 */
typedef union
{
   uint16_t Regs[0x100];
   struct
   {
      uint16_t VoiceRegs[0xC0];
      union
      {
         uint16_t GlobalRegs[0x20];
         struct
         {
            uint16_t _Global0[0x17];
            uint16_t SPUStatus;
            uint16_t _Global1[0x08];
         } s;
      } global;
      union
      {
         uint16 ReverbRegs[0x20];
         struct
         {
            uint16 FB_SRC_A;
            uint16 FB_SRC_B;
            int16 IIR_ALPHA;
            int16 ACC_COEF_A;
            int16 ACC_COEF_B;
            int16 ACC_COEF_C;
            int16 ACC_COEF_D;
            int16 IIR_COEF;
            int16 FB_ALPHA;
            int16 FB_X;
            uint16 IIR_DEST_A[2];
            uint16 ACC_SRC_A[2];
            uint16 ACC_SRC_B[2];
            uint16 IIR_SRC_A[2];
            uint16 IIR_DEST_B[2];
            uint16 ACC_SRC_C[2];
            uint16 ACC_SRC_D[2];
            uint16 IIR_SRC_B[2];
            uint16 MIX_DEST_A[2];
            uint16 MIX_DEST_B[2];
            int16 IN_COEF[2];
         } s;
      } reverb;
   } s;
} SPU_RegBlock;

static SPU_Voice Voices[24];

static uint32_t NoiseDivider;
static uint32_t NoiseCounter;
static uint16_t LFSR;

static uint32_t FM_Mode;
static uint32_t Noise_Mode;
static uint32_t Reverb_Mode;

static uint32_t ReverbWA;

static SPU_Sweep GlobalSweep[2];	// Doesn't affect reverb volume!

static int32_t ReverbVol[2];

static int32_t CDVol[2];
static int32_t ExternVol[2];

static uint32_t IRQAddr;

static uint32_t RWAddr;

static uint16_t SPUControl;

static uint32_t VoiceOn;
static uint32_t VoiceOff;

static uint32_t BlockEnd;

static uint32_t CWA;

static SPU_RegBlock regs;

static uint16_t AuxRegs[0x10];

static int16 RDSB[2][128];	// [40]
static int16 RUSB[2][64];
static int32_t RvbResPos;

static uint32_t ReverbCur;

static bool IRQAsserted;

static int32_t clock_divider;

static uint16_t SPURAM[524288 / sizeof(uint16)];

/* Forward declarations for SPU_Sweep operations; defined further down. */
static INLINE void SPU_Sweep_Power(SPU_Sweep *sweep);
static INLINE void SPU_Sweep_WriteControl(SPU_Sweep *sweep, uint16 value);
static INLINE int16 SPU_Sweep_ReadVolume(SPU_Sweep *sweep);
static INLINE void SPU_Sweep_WriteVolume(SPU_Sweep *sweep, int16 value);
static void SPU_Sweep_Clock(SPU_Sweep *sweep);

/* Forward declarations for the static reverb helpers. */
static int16 SPU_RD_RVB(uint16 raw_offs, int32 extra_offs);
static void SPU_WR_RVB(uint16 raw_offs, int16 sample);

  void SPU_Power(void)
{
   clock_divider = 768;

   memset(SPURAM, 0, sizeof(SPURAM));

   for(int i = 0; i < 24; i++)
   {
      memset(Voices[i].DecodeBuffer, 0, sizeof(Voices[i].DecodeBuffer));
      Voices[i].DecodeM2 = 0;
      Voices[i].DecodeM1 = 0;

      Voices[i].DecodePlayDelay = 0;
      Voices[i].DecodeWritePos = 0;
      Voices[i].DecodeReadPos = 0;
      Voices[i].DecodeAvail = 0;

      Voices[i].DecodeShift = 0;
      Voices[i].DecodeWeight = 0;
      Voices[i].DecodeFlags = 0;

      Voices[i].IgnoreSampLA = false;

      SPU_Sweep_Power(&Voices[i].Sweep[0]);
      SPU_Sweep_Power(&Voices[i].Sweep[1]);

      Voices[i].Pitch = 0;
      Voices[i].CurPhase = 0;

      Voices[i].StartAddr = 0;

      Voices[i].CurAddr = 0;

      Voices[i].ADSRControl = 0;

      Voices[i].LoopAddr = 0;

      Voices[i].PreLRSample = 0;

      memset(&Voices[i].ADSR, 0, sizeof(SPU_ADSR));
   }

   SPU_Sweep_Power(&GlobalSweep[0]);
   SPU_Sweep_Power(&GlobalSweep[1]);

   NoiseDivider = 0;
   NoiseCounter = 0;
   LFSR = 0;

   FM_Mode = 0;
   Noise_Mode = 0;
   Reverb_Mode = 0;
   ReverbWA = 0;

   ReverbVol[0] = ReverbVol[1] = 0;

   CDVol[0] = CDVol[1] = 0;

   ExternVol[0] = ExternVol[1] = 0;

   IRQAddr = 0;

   RWAddr = 0;

   SPUControl = 0;

   VoiceOn = 0;
   VoiceOff = 0;

   BlockEnd = 0;

   CWA = 0;

   memset(&regs, 0, sizeof(regs));
   memset(AuxRegs, 0, sizeof(AuxRegs));
   memset(RDSB, 0, sizeof(RDSB));
   memset(RUSB, 0, sizeof(RUSB));
   RvbResPos = 0;

   ReverbCur = ReverbWA;

   IRQAsserted = false;
}

static INLINE void CalcVCDelta(const uint8 zs, uint8 speed, bool log_mode, bool dec_mode, bool inv_increment, int16 Current, int *increment, int *divinco)
{
   *increment = (7 - (speed & 0x3));

   if(inv_increment)
      *increment = ~*increment;

   *divinco = 32768;

   if(speed < 0x2C)
      *increment = (unsigned)*increment << ((0x2F - speed) >> 2);

   if(speed >= 0x30)
      *divinco >>= (speed - 0x2C) >> 2;

   if(log_mode)
   {
      if(dec_mode)	// Log decrement mode
         *increment = (Current * *increment) >> 15;
      else			// Log increment mode
      {
         if((Current & 0x7FFF) >= 0x6000)
         {
            if(speed < 0x28)
               *increment >>= 2;
            else if(speed >= 0x2C)
               *divinco >>= 2;
            else	// 0x28 ... 0x2B
            {
               *increment >>= 1;
               *divinco >>= 1;
            }
         }
      }
   } // end if(log_mode)

   if(*divinco == 0 && speed < zs) //0x7F)
      *divinco = 1;
}


static INLINE void SPU_Sweep_Power(SPU_Sweep *sweep)
{
   sweep->Control = 0;
   sweep->Current = 0;
   sweep->Divider = 0;
}

static INLINE void SPU_Sweep_WriteControl(SPU_Sweep *sweep, uint16 value)
{
   sweep->Control = value;
}

static INLINE int16 SPU_Sweep_ReadVolume(SPU_Sweep *sweep)
{
   return((int16)sweep->Current);
}

static void SPU_Sweep_Clock(SPU_Sweep *sweep)
{
   const bool log_mode = (bool)(sweep->Control & 0x4000);
   const bool dec_mode = (bool)(sweep->Control & 0x2000);
   const bool inv_mode = (bool)(sweep->Control & 0x1000);
   const bool inv_increment = (dec_mode ^ inv_mode) | (dec_mode & log_mode);
   const uint16 vc_cv_xor = (inv_mode & !(dec_mode & log_mode)) ? 0xFFFF : 0x0000;
   const uint16 TestInvert = inv_mode ? 0xFFFF : 0x0000;
   int increment;
   int divinco;

   CalcVCDelta(0x7F, sweep->Control & 0x7F, log_mode, dec_mode, inv_increment, (int16)(sweep->Current ^ vc_cv_xor), &increment, &divinco);

   if((dec_mode & !(inv_mode & log_mode)) && ((sweep->Current & 0x8000) == (inv_mode ? 0x0000 : 0x8000) || (sweep->Current == 0)))
   {
      // Not sure if this condition should stop the Divider adding or force the increment value to 0.
      sweep->Current = 0;
   }
   else
   {
      sweep->Divider += divinco;

      if(sweep->Divider & 0x8000)
      {
         sweep->Divider = 0;

         if(dec_mode || ((sweep->Current ^ TestInvert) != 0x7FFF))
         {
            uint16 PrevCurrent = sweep->Current;
            sweep->Current = sweep->Current + increment;

            if(!dec_mode && ((sweep->Current ^ PrevCurrent) & 0x8000) && ((sweep->Current ^ TestInvert) & 0x8000))
               sweep->Current = 0x7FFF ^ TestInvert;
         }
      }
   }
}

static INLINE void SPU_Sweep_WriteVolume(SPU_Sweep *sweep, int16 value)
{
   sweep->Current = value;
}


// Take care not to trigger SPU IRQ for the next block before its decoding start.
static void SPU_RunDecoder(SPU_Voice *voice)
{
   // 5 through 0xF appear to be 0 on the real thing.
   static const int32 Weights[16][2] =
   {
      // s-1    s-2
      {   0,    0 },
      {  60,    0 },
      { 115,  -52 },
      {  98,  -55 },
      { 122,  -60 },
   };

   if(voice->DecodeAvail >= 11)
   {
      if(SPUControl & 0x40)
      {
         unsigned test_addr = (voice->CurAddr - 1) & 0x3FFFF;
         if(IRQAddr == test_addr || IRQAddr == (test_addr & 0x3FFF8))
         {
            IRQAsserted = true;
            IRQ_Assert(IRQ_SPU, IRQAsserted);
         }
      }
      return;
   }

   if((voice->CurAddr & 0x7) == 0)
   {
      // Handle delayed flags from the previously-decoded block.
      //
      // NOTE: The timing of setting the BlockEnd bit here, and forcing ADSR envelope volume to 0, is a bit late.  (And I'm not sure if it should be done once
      // per decoded block, or more than once, but that's probably not something games would rely on, but we should test it anyway).
      //
      // Correctish timing can be achieved by moving this block of code up above voice->DecodeAvail >= 11, and sticking it inside an: if(voice->DecodeAvail <= 12),
      // though more tests are needed on the ADPCM decoding process as a whole before we should actually make such a change.  Additionally, we'd probably
      // have to separate the CurAddr = LoopAddr logic, so we don't generate spurious early SPU IRQs.
      if(voice->DecodeFlags & 0x1)
      {
         voice->CurAddr = voice->LoopAddr & ~0x7;

         BlockEnd |= 1 << (voice - Voices);

         if(!(voice->DecodeFlags & 0x2))	// Force enveloping to 0 if not "looping".  TODO: Should we reset the ADSR divider counter too?
         {
            if(!(Noise_Mode & (1 << (voice - Voices))))
            {
               voice->ADSR.Phase = ADSR_RELEASE;
               voice->ADSR.EnvLevel = 0;
            }
         }
      }
   }

   if(SPUControl & 0x40)
   {
      unsigned test_addr = voice->CurAddr & 0x3FFFF;
      if(IRQAddr == test_addr || IRQAddr == (test_addr & 0x3FFF8))
      {
         IRQAsserted = true;
         IRQ_Assert(IRQ_SPU, IRQAsserted);
      }
   }

   if((voice->CurAddr & 0x7) == 0)
   {
      const uint16 CV = SPURAM[voice->CurAddr];
      voice->DecodeShift = CV & 0xF;
      voice->DecodeWeight = (CV >> 4) & 0xF;
      voice->DecodeFlags = (CV >> 8) & 0xFF;

      if(voice->DecodeFlags & 0x4)
      {
         if(!voice->IgnoreSampLA)
            voice->LoopAddr = voice->CurAddr;
      }
      voice->CurAddr = (voice->CurAddr + 1) & 0x3FFFF;
   }

   // Don't else this block; we need to ALWAYS decode 4 samples per call to SPU_RunDecoder() if DecodeAvail < 11, or else sample playback
   // at higher rates will fail horribly.
   {
      const int32 weight_m1 = Weights[voice->DecodeWeight][0];
      const int32 weight_m2 = Weights[voice->DecodeWeight][1];
      uint16 CV;
      unsigned shift;
      uint32 coded;
      int16 *tb = &voice->DecodeBuffer[voice->DecodeWritePos];

      CV = SPURAM[voice->CurAddr];
      shift = voice->DecodeShift;

      if(MDFN_UNLIKELY(shift > 12))
      {
         shift = 8;
         CV &= 0x8888;
      }

      coded = (uint32)CV << 12;


      for(int i = 0; i < 4; i++)
      {
         int32 sample = (int16)(coded & 0xF000) >> shift;

         sample += ((voice->DecodeM2 * weight_m2) >> 6);
         sample += ((voice->DecodeM1 * weight_m1) >> 6);

         clamp(&sample, -32768, 32767);

         tb[i] = sample;
         voice->DecodeM2 = voice->DecodeM1;
         voice->DecodeM1 = sample;
         coded >>= 4;
      }
      voice->DecodeWritePos = (voice->DecodeWritePos + 4) & 0x1F;
      voice->DecodeAvail += 4;
      voice->CurAddr = (voice->CurAddr + 1) & 0x3FFFF;
   }
}

static void SPU_CacheEnvelope(SPU_Voice *voice)
{
   uint32_t   raw    = voice->ADSRControl;
   SPU_ADSR *ADSR    = &voice->ADSR;
   int32_t     Sl    = (raw >> 0) & 0x0F;
   int32_t     Dr    = (raw >> 4) & 0x0F;
   int32_t     Ar    = (raw >> 8) & 0x7F;

   int32_t     Rr    = (raw >> 16) & 0x1F;
   int32_t     Sr    = (raw >> 22) & 0x7F;

   ADSR->AttackExp   = (bool)(raw & (1 << 15));
   ADSR->ReleaseExp  = (bool)(raw & (1 << 21));
   ADSR->SustainExp  = (bool)(raw & (1 << 31));
   ADSR->SustainDec  = (bool)(raw & (1 << 30));

   ADSR->AttackRate  = Ar;
   ADSR->DecayRate   = Dr << 2;
   ADSR->SustainRate = Sr;
   ADSR->ReleaseRate = Rr << 2;

   ADSR->SustainLevel = (Sl + 1) << 11;
}

static void SPU_ResetEnvelope(SPU_Voice *voice)
{
   SPU_ADSR *ADSR = &voice->ADSR;

   ADSR->EnvLevel = 0;
   ADSR->Divider = 0;
   ADSR->Phase = ADSR_ATTACK;
}

static void SPU_ReleaseEnvelope(SPU_Voice *voice)
{
   SPU_ADSR *ADSR = &voice->ADSR;

   ADSR->Divider = 0;
   ADSR->Phase = ADSR_RELEASE;
}


static void SPU_RunEnvelope(SPU_Voice *voice)
{
   SPU_ADSR *ADSR = &voice->ADSR;
   int increment;
   int divinco;
   int16 uoflow_reset;

   if(ADSR->Phase == ADSR_ATTACK && ADSR->EnvLevel == 0x7FFF)
      ADSR->Phase++;

   switch(ADSR->Phase)
   {
      default: assert(0);
               break;

      case ADSR_ATTACK:
               CalcVCDelta(0x7F, ADSR->AttackRate, ADSR->AttackExp, false, false, (int16)ADSR->EnvLevel, &increment, &divinco);
               uoflow_reset = 0x7FFF;
               break;

      case ADSR_DECAY:
               CalcVCDelta(0x1F << 2, ADSR->DecayRate, true, true, true, (int16)ADSR->EnvLevel, &increment, &divinco);
               uoflow_reset = 0;
               break;

      case ADSR_SUSTAIN:
               CalcVCDelta(0x7F, ADSR->SustainRate, ADSR->SustainExp, ADSR->SustainDec, ADSR->SustainDec, (int16)ADSR->EnvLevel, &increment, &divinco);
               uoflow_reset = ADSR->SustainDec ? 0 : 0x7FFF;
               break;

      case ADSR_RELEASE:
               CalcVCDelta(0x1F << 2, ADSR->ReleaseRate, ADSR->ReleaseExp, true, true, (int16)ADSR->EnvLevel, &increment, &divinco);
               uoflow_reset = 0;
               break;
   }

   ADSR->Divider += divinco;
   if(ADSR->Divider & 0x8000)
   {
      const uint16 prev_level = ADSR->EnvLevel;

      ADSR->Divider = 0;
      ADSR->EnvLevel += increment;

      if(ADSR->Phase == ADSR_ATTACK)
      {
         // If previous the upper bit was 0, but now it's 1, handle overflow.
         if(((prev_level ^ ADSR->EnvLevel) & ADSR->EnvLevel) & 0x8000)
            ADSR->EnvLevel = uoflow_reset;
      }
      else
      {
         if(ADSR->EnvLevel & 0x8000)
            ADSR->EnvLevel = uoflow_reset;
      }
      if(ADSR->Phase == ADSR_DECAY && (uint16)ADSR->EnvLevel < ADSR->SustainLevel)
         ADSR->Phase++;
   }
}

static INLINE void SPU_CheckIRQAddr(uint32 addr)
{
   if(SPUControl & 0x40)
   {
      if(IRQAddr != addr)
         return;

      IRQAsserted = true;
      IRQ_Assert(IRQ_SPU, IRQAsserted);
   }
}

static INLINE void SPU_WriteSPURAM(uint32 addr, uint16 value)
{
   SPU_CheckIRQAddr(addr);

   SPURAM[addr] = value;
}

static INLINE uint16 SPU_ReadSPURAM(uint32 addr)
{
   SPU_CheckIRQAddr(addr);
   return(SPURAM[addr]);
}

static INLINE int16 ReverbSat(int32 samp)
{
 if(samp > 32767)
  samp = 32767;

 if(samp < -32768)
  samp = -32768;

 return(samp);
}

#define REVERB_NEG(samp) (((samp) == -32768) ? 0x7FFF : -(samp))

static INLINE uint32 SPU_Get_Reverb_Offset(uint32 in_offset)
{
 uint32 offset = ReverbCur + (in_offset & 0x3FFFF);

 offset += ReverbWA & ((int32)(offset << 13) >> 31);
 offset &= 0x3FFFF;

 return(offset);
}

static int16 NO_INLINE SPU_RD_RVB(uint16 raw_offs, int32 extra_offs)
{
 return SPU_ReadSPURAM(SPU_Get_Reverb_Offset((raw_offs << 2) + extra_offs));
}

static void NO_INLINE SPU_WR_RVB(uint16 raw_offs, int16 sample)
{
   if(SPUControl & 0x80)
      SPU_WriteSPURAM(SPU_Get_Reverb_Offset(raw_offs << 2), sample);
}

// Zeroes optimized out; middle removed too(it's 16384)
static const int16 ResampTable[20] =
{
 -1, 2, -10, 35, -103, 266, -616, 1332, -2960, 10246, 10246, -2960, 1332, -616, 266, -103, 35, -10, 2, -1,
};

static INLINE int32 Reverb4422(const int16 *src)
{
 int32 out = 0;	// 32-bits is adequate(it won't overflow)

 for(unsigned i = 0; i < 20; i++)
  out += ResampTable[i] * src[i * 2];

 // Middle non-zero
 out += 0x4000 * src[19];

 out >>= 15;

 clamp(&out, -32768, 32767);

 return(out);
}

static INLINE int32 Reverb2244(const int16 *src)
{
   unsigned i;
   int32_t out = 0; /* 32bits is adequate (it won't overflow) */

   for(i = 0; i < 20; i++)
      out += ResampTable[i] * src[i];

   out >>= 14;

   clamp(&out, -32768, 32767);

   return out;
}

static int32 IIASM(const int16 alpha, const int16 insamp)
{
   if(MDFN_UNLIKELY(alpha == -32768))
   {
      if(insamp == -32768)
         return 0;
      return insamp * -65536;
   }

   return insamp * (32768 - alpha);
}

// Take care to thoroughly test the reverb resampling code when modifying anything that uses RvbResPos.
static void SPU_RunReverb(const int32* in, int32* out)
{
   unsigned lr;
 int32 upsampled[2];

 upsampled[0] = upsampled[1] = 0;

 for(lr = 0; lr < 2; lr++)
 {
  RDSB[lr][RvbResPos | 0x00] = in[lr];
  RDSB[lr][RvbResPos | 0x40] = in[lr];	// So we don't have to &/bounds check in our MAC loop
 }

 if(RvbResPos & 1)
 {
  int32 downsampled[2];

  for(unsigned lr = 0; lr < 2; lr++)
   downsampled[lr] = Reverb4422(&RDSB[lr][(RvbResPos - 38) & 0x3F]);

  /* Run algorithm */
  for(unsigned lr = 0; lr < 2; lr++)
  {
     const int16 IIR_INPUT_A = ReverbSat((((SPU_RD_RVB(regs.s.reverb.s.IIR_SRC_A[lr ^ 0], 0) * regs.s.reverb.s.IIR_COEF) >> 14) + ((downsampled[lr] * regs.s.reverb.s.IN_COEF[lr]) >> 14)) >> 1);
     const int16 IIR_INPUT_B = ReverbSat((((SPU_RD_RVB(regs.s.reverb.s.IIR_SRC_B[lr ^ 1], 0) * regs.s.reverb.s.IIR_COEF) >> 14) + ((downsampled[lr] * regs.s.reverb.s.IN_COEF[lr]) >> 14)) >> 1);
     const int16 IIR_A = ReverbSat((((IIR_INPUT_A * regs.s.reverb.s.IIR_ALPHA) >> 14) + (IIASM(regs.s.reverb.s.IIR_ALPHA, SPU_RD_RVB(regs.s.reverb.s.IIR_DEST_A[lr], -1)) >> 14)) >> 1);
     const int16 IIR_B = ReverbSat((((IIR_INPUT_B * regs.s.reverb.s.IIR_ALPHA) >> 14) + (IIASM(regs.s.reverb.s.IIR_ALPHA, SPU_RD_RVB(regs.s.reverb.s.IIR_DEST_B[lr], -1)) >> 14)) >> 1);

     SPU_WR_RVB(regs.s.reverb.s.IIR_DEST_A[lr], IIR_A);
     SPU_WR_RVB(regs.s.reverb.s.IIR_DEST_B[lr], IIR_B);

     const int32 ACC = ((SPU_RD_RVB(regs.s.reverb.s.ACC_SRC_A[lr], 0) * regs.s.reverb.s.ACC_COEF_A) >> 14) +
        ((SPU_RD_RVB(regs.s.reverb.s.ACC_SRC_B[lr], 0) * regs.s.reverb.s.ACC_COEF_B) >> 14) +
        ((SPU_RD_RVB(regs.s.reverb.s.ACC_SRC_C[lr], 0) * regs.s.reverb.s.ACC_COEF_C) >> 14) +
        ((SPU_RD_RVB(regs.s.reverb.s.ACC_SRC_D[lr], 0) * regs.s.reverb.s.ACC_COEF_D) >> 14);

     const int16 FB_A = SPU_RD_RVB(regs.s.reverb.s.MIX_DEST_A[lr] - regs.s.reverb.s.FB_SRC_A, 0);
     const int16 FB_B = SPU_RD_RVB(regs.s.reverb.s.MIX_DEST_B[lr] - regs.s.reverb.s.FB_SRC_B, 0);
     const int16 MDA = ReverbSat((ACC + ((FB_A * REVERB_NEG(regs.s.reverb.s.FB_ALPHA)) >> 14)) >> 1);
     const int16 MDB = ReverbSat(FB_A + ((((MDA * regs.s.reverb.s.FB_ALPHA) >> 14) + ((FB_B * REVERB_NEG(regs.s.reverb.s.FB_X)) >> 14)) >> 1));
     const int16 IVB = ReverbSat(FB_B + ((MDB * regs.s.reverb.s.FB_X) >> 15));

     SPU_WR_RVB(regs.s.reverb.s.MIX_DEST_A[lr], MDA);
     SPU_WR_RVB(regs.s.reverb.s.MIX_DEST_B[lr], MDB);
#if 0
     {
        static uint32 sqcounter;
        RUSB[lr][(RvbResPos >> 1) | 0x20] = RUSB[lr][RvbResPos >> 1] = ((sqcounter & 0xFF) == 0) ? 0x8000 : 0x0000; //((sqcounter & 0x80) ? 0x7000 : 0x9000);
        sqcounter += lr;
     }
#else
     RUSB[lr][(RvbResPos >> 1) | 0x20] = RUSB[lr][RvbResPos >> 1] = IVB;	// Output sample
#endif
  }

  ReverbCur = (ReverbCur + 1) & 0x3FFFF;
  if(!ReverbCur)
   ReverbCur = ReverbWA;

  for(unsigned lr = 0; lr < 2; lr++)
  {
     const int16 *src = &RUSB[lr][((RvbResPos >> 1) - 19) & 0x1F];
     upsampled[lr] = Reverb2244(src);
  }
 }
 else
 {
  for(unsigned lr = 0; lr < 2; lr++)
  {
     const int16 *src = &RUSB[lr][((RvbResPos >> 1) - 19) & 0x1F];
     upsampled[lr] = src[9]; /* Reverb 2244 (Middle non-zero */
  }
 }

 RvbResPos = (RvbResPos + 1) & 0x3F;

 for(unsigned lr = 0; lr < 2; lr++)
  out[lr] = upsampled[lr];
}


static INLINE void SPU_RunNoise(void)
{
   const unsigned rf = ((SPUControl >> 8) & 0x3F);
   uint32 NoiseDividerInc = (2 << (rf >> 2));
   uint32 NoiseCounterInc = 4 + (rf & 0x3);

   if(rf >= 0x3C)
   {
      NoiseDividerInc = 0x8000;
      NoiseCounterInc = 8;
   }

   NoiseDivider += NoiseDividerInc;

   if(NoiseDivider & 0x8000)
   {
      NoiseDivider = 0;

      NoiseCounter += NoiseCounterInc;

      if(NoiseCounter & 0x8)
      {
         NoiseCounter &= 0x7;
         LFSR = (LFSR << 1) | (((LFSR >> 15) ^ (LFSR >> 12) ^ (LFSR >> 11) ^ (LFSR >> 10) ^ 1) & 1);
      }
   }
}

  int32 SPU_UpdateFromCDC(int32 clocks)
{
   int32 sample_clocks = 0;

   clock_divider -= clocks;

   while(clock_divider <= 0)
   {
      clock_divider += spu_samples*768;
      sample_clocks += spu_samples;
   }

   while(sample_clocks > 0)
   {
      // xxx[0] = left, xxx[1] = right

      // Accumulated sound output.
      int32 accum[2];

      // Accumulated sound output for reverb input
      int32 accum_fv[2];

      // Output of reverb processing.
      int32 reverb[2];

      // Final output.
      int32 output[2];

      accum[0]    = accum[1]    = 0;
      accum_fv[0] = accum_fv[1] = 0;
      reverb[0]   = reverb[1]   = 0;
      output[0]   = output[1]   = 0;

      const uint32 PhaseModCache = FM_Mode & ~ 1;
      /*
       **
       ** 0x1F801DAE Notes and Conjecture:
       **   -------------------------------------------------------------------------------------
       **   |   15   14 | 13 | 12 | 11 | 10  | 9  | 8 |  7 |  6  | 5    4    3    2    1    0   |
       **   |      ?    | *13| ?  | ba | *10 | wrr|rdr| df |  is |      c                       |
       **   -------------------------------------------------------------------------------------
       **
       **	c - Appears to be delayed copy of lower 6 bits from 0x1F801DAA.
       **
       **     is - Interrupt asserted out status. (apparently not instantaneous status though...)
       **
       **     df - Related to (c & 0x30) == 0x20 or (c & 0x30) == 0x30, at least.
       **          0 = DMA busy(FIFO not empty when in DMA write mode?)?
       **	    1 = DMA ready?  Something to do with the FIFO?
       **
       **     rdr - SPU_Read(DMA read?) Ready?
       **
       **     wrr - SPU_Write(DMA write?) Ready?
       **
       **     *10 - Unknown.  Some sort of (FIFO?) busy status?(BIOS tests for this bit in places)
       **
       **     ba - Alternates between 0 and 1, even when SPUControl bit15 is 0; might be related to CD audio and voice 1 and 3 writing to SPU RAM.
       **
       **     *13 - Unknown, was set to 1 when testing with an SPU delay system reg value of 0x200921E1(test result might not be reliable, re-run).
       */
      regs.s.global.s.SPUStatus = SPUControl & 0x3F;
      regs.s.global.s.SPUStatus |= IRQAsserted ? 0x40 : 0x00;

      if(regs.Regs[0xD6] == 0x4)	// TODO: Investigate more(case 0x2C in global regs r/w handler)
         regs.s.global.s.SPUStatus |= (CWA & 0x100) ? 0x800 : 0x000;

      for(int voice_num = 0; voice_num < 24; voice_num++)
      {
         SPU_Voice *voice = &Voices[voice_num];
         int32 voice_pvs;

         voice->PreLRSample = 0;


         if(voice->DecodePlayDelay)
         {
            voice->IgnoreSampLA = false;
         }

         // Decode new samples if necessary.
         SPU_RunDecoder(voice);


         int l, r;

         if(Noise_Mode & (1 << voice_num))
            voice_pvs = (int16)LFSR;
         else
         {
            const int si = voice->DecodeReadPos;
            const int pi = ((voice->CurPhase & 0xFFF) >> 4);

            voice_pvs = ((voice->DecodeBuffer[(si + 0) & 0x1F] * FIR_Table[pi][0]) +
                  (voice->DecodeBuffer[(si + 1) & 0x1F] * FIR_Table[pi][1]) +
                  (voice->DecodeBuffer[(si + 2) & 0x1F] * FIR_Table[pi][2]) +   
                  (voice->DecodeBuffer[(si + 3) & 0x1F] * FIR_Table[pi][3])) >> 15;
         }

         voice_pvs = (voice_pvs * (int16)voice->ADSR.EnvLevel) >> 15;
         voice->PreLRSample = voice_pvs;

         if(voice_num == 1 || voice_num == 3)
         {
            int index = voice_num >> 1;

            SPU_WriteSPURAM(0x400 | (index * 0x200) | CWA, voice_pvs);
         }


         l = (voice_pvs * SPU_Sweep_ReadVolume(&voice->Sweep[0])) >> 15;
         r = (voice_pvs * SPU_Sweep_ReadVolume(&voice->Sweep[1])) >> 15;

         accum[0] += l;
         accum[1] += r;

         if(Reverb_Mode & (1 << voice_num))
         {
            accum_fv[0] += l;
            accum_fv[1] += r;
         }

         // Run sweep
         for(int lr = 0; lr < 2; lr++)
         {
            if((voice->Sweep[lr].Control & 0x8000))
               SPU_Sweep_Clock(&voice->Sweep[lr]);
            else
               voice->Sweep[lr].Current = (voice->Sweep[lr].Control & 0x7FFF) << 1;
         }

         // Increment stuff
         if(!voice->DecodePlayDelay)
         {
            unsigned phase_inc;

            // Run enveloping
            SPU_RunEnvelope(voice);

            if(PhaseModCache & (1 << voice_num))
            {
               // This old formula: phase_inc = (voice->Pitch * ((voice - 1)->PreLRSample + 0x8000)) >> 15;
               // is incorrect, as it does not handle carrier pitches >= 0x8000 properly.
               phase_inc = voice->Pitch + (((int16)voice->Pitch * ((voice - 1)->PreLRSample)) >> 15);
            }
            else
               phase_inc = voice->Pitch;

            if(phase_inc > 0x3FFF)
               phase_inc = 0x3FFF;

            {
               const uint32 tmp_phase = voice->CurPhase + phase_inc;
               const unsigned used = tmp_phase >> 12;

               voice->CurPhase = tmp_phase & 0xFFF;
               voice->DecodeAvail -= used;
               voice->DecodeReadPos = (voice->DecodeReadPos + used) & 0x1F;
            }
         }
         else
            voice->DecodePlayDelay--;

         if(VoiceOff & (1U << voice_num))
         {
            if(voice->ADSR.Phase != ADSR_RELEASE)
            {
               // TODO/FIXME:
               //  To fix all the missing notes in "Dragon Ball GT: Final Bout" music, !voice->DecodePlayDelay instead of
               //  voice->DecodePlayDelay < 3 is necessary, but that would cause the length of time for which the voice off is
               //  effectively ignored to be too long by about half a sample(rough test measurement).  That, combined with current
               //  CPU and DMA emulation timing inaccuracies(execution generally too fast), creates a significant risk of regressions
               //  in other games, so be very conservative for now.
               //
               //  Also, voice on should be ignored during the delay as well, but comprehensive tests are needed before implementing that
               //  due to some effects that appear to occur repeatedly during the delay on a PS1 but are currently only emulated as
               //  performed when the voice on is processed(e.g. curaddr = startaddr).
               //
               if(voice->DecodePlayDelay < 3)
               {
                  SPU_ReleaseEnvelope(voice);
               }
            }
         }

         if(VoiceOn & (1U << voice_num))
         {
            SPU_ResetEnvelope(voice);

            voice->DecodeFlags = 0;
            voice->DecodeWritePos = 0;
            voice->DecodeReadPos = 0;
            voice->DecodeAvail = 0;
            voice->DecodePlayDelay = 4;

            BlockEnd &= ~(1 << voice_num);

            // Weight/filter previous value initialization:
            voice->DecodeM2 = 0;
            voice->DecodeM1 = 0;

            voice->CurPhase = 0;
            voice->CurAddr = voice->StartAddr & ~0x7;
            voice->IgnoreSampLA = false;
         }

         if(!(SPUControl & 0x8000))
         {
            voice->ADSR.Phase = ADSR_RELEASE;
            voice->ADSR.EnvLevel = 0;
         }
      }

      VoiceOff = 0;
      VoiceOn = 0; 

      // "Mute" control doesn't seem to affect CD audio(though CD audio reverb wasn't tested...)
      // TODO: If we add sub-sample timing accuracy, see if it's checked for every channel at different times, or just once.
      if(!(SPUControl & 0x4000))
      {
         accum[0] = 0;
         accum[1] = 0;
         accum_fv[0] = 0;
         accum_fv[1] = 0;
      }

      // Get CD-DA. CDC_GetCDAudioSample wraps the AudioBuffer
      // position/freq probe and the GetCDAudio() call; both
      // channels are guaranteed written, with values clamped to
      // -32768..32767 (the historical contract from
      // PS_CDC::GetCDAudio).
      {
         int32 cda_raw[2];
         int32 cdav[2];

         CDC_GetCDAudioSample(cda_raw);

         SPU_WriteSPURAM(CWA | 0x000, cda_raw[0]);
         SPU_WriteSPURAM(CWA | 0x200, cda_raw[1]);

         for(unsigned i = 0; i < 2; i++)
            cdav[i] = (cda_raw[i] * CDVol[i]) >> 15;

         if(SPUControl & 0x0001)
         {
            accum[0] += cdav[0];
            accum[1] += cdav[1];

            if(SPUControl & 0x0004)	// TODO: Test this bit(and see if it is really dependent on bit0)
            {
               accum_fv[0] += cdav[0];
               accum_fv[1] += cdav[1];
            }
         }
      }

      CWA = (CWA + 1) & 0x1FF;

      SPU_RunNoise();

      for (unsigned lr = 0; lr < 2; lr++)
         clamp(&accum_fv[lr], -32768, 32767);

      SPU_RunReverb(accum_fv, reverb);

      for(unsigned lr = 0; lr < 2; lr++)
      {
         accum[lr] += ((reverb[lr] * ReverbVol[lr]) >> 15);
         clamp(&accum[lr],  -32768, 32767);
         output[lr] = (accum[lr] * SPU_Sweep_ReadVolume(&GlobalSweep[lr])) >> 15;
         clamp(&output[lr], -32768, 32767);
      }

      if(IntermediateBufferPos < 4096)	// Overflow might occur in some debugger use cases.
      {
         // 75%, for some (resampling) headroom.
         for(unsigned lr = 0; lr < 2; lr++)
            IntermediateBuffer[IntermediateBufferPos][lr] = (output[lr] * 3 + 2) >> 2;

         IntermediateBufferPos++;
      }

      sample_clocks--;

      // Clock global sweep
      for(unsigned lr = 0; lr < 2; lr++)
      {
         if((GlobalSweep[lr].Control & 0x8000))
            SPU_Sweep_Clock(&GlobalSweep[lr]);
         else
            GlobalSweep[lr].Current = (GlobalSweep[lr].Control & 0x7FFF) << 1;
      }
   }

   return clock_divider;
}

  void SPU_WriteDMA(uint32 V)
{
   SPU_WriteSPURAM(RWAddr, V);
   RWAddr = (RWAddr + 1) & 0x3FFFF;

   SPU_WriteSPURAM(RWAddr, V >> 16);
   RWAddr = (RWAddr + 1) & 0x3FFFF;


   SPU_CheckIRQAddr(RWAddr);
}

  uint32 SPU_ReadDMA(void)
{
   uint32 ret = (uint16)SPU_ReadSPURAM(RWAddr);
   RWAddr = (RWAddr + 1) & 0x3FFFF;

   ret |= (uint32)(uint16)SPU_ReadSPURAM(RWAddr) << 16;
   RWAddr = (RWAddr + 1) & 0x3FFFF;

   SPU_CheckIRQAddr(RWAddr);


   return(ret);
}

/*
 * External API for the SPU module. The public functions
 * (SPU_Power / SPU_Write / SPU_Read / etc.) are defined directly
 * earlier in this file as plain free functions operating on the
 * file-scope state - no forwarder indirection. spu_c.h declares
 * them with `extern "C"` so C++ consumers (libretro.cpp,
 * cdc.cpp) link against the same C-linkage symbols this TU
 * emits.
 *
 * SPU_Init clears every piece of mutable state to a known-zero
 * value (matching what `new PS_SPU()` did historically: ctor
 * left everything as default-constructed POD plus zeroed the
 * IntermediateBuffer; Power then took it from there). SPU_Kill is
 * a no-op now that there's no heap allocation - retained as a
 * stable lifecycle hook so libretro.cpp's teardown sequence
 * doesn't change shape.
 */
void SPU_Init(void)
{
   memset(Voices, 0, sizeof(Voices));
   memset(GlobalSweep, 0, sizeof(GlobalSweep));
   memset(&regs, 0, sizeof(regs));
   memset(AuxRegs, 0, sizeof(AuxRegs));
   memset(RDSB, 0, sizeof(RDSB));
   memset(RUSB, 0, sizeof(RUSB));
   memset(SPURAM, 0, sizeof(SPURAM));
   NoiseDivider = 0;
   NoiseCounter = 0;
   LFSR = 0;
   FM_Mode = 0;
   Noise_Mode = 0;
   Reverb_Mode = 0;
   ReverbWA = 0;
   ReverbVol[0] = ReverbVol[1] = 0;
   CDVol[0] = CDVol[1] = 0;
   ExternVol[0] = ExternVol[1] = 0;
   IRQAddr = 0;
   RWAddr = 0;
   SPUControl = 0;
   VoiceOn = 0;
   VoiceOff = 0;
   BlockEnd = 0;
   CWA = 0;
   RvbResPos = 0;
   ReverbCur = 0;
   IRQAsserted = false;
   clock_divider = 0;

   IntermediateBufferPos = 0;
   memset(IntermediateBuffer, 0, sizeof(IntermediateBuffer));
}

  void SPU_Kill(void)
{
}

  void SPU_Write(int32_t timestamp, uint32 A, uint16 V)
{
   A &= 0x3FF;

   if(A >= 0x200)
   {
      if(A < 0x260)
      {
         SPU_Voice *voice = &Voices[(A - 0x200) >> 2];
         SPU_Sweep_WriteVolume(&voice->Sweep[(A & 2) >> 1], V);
      }
      else if(A < 0x280)
         AuxRegs[(A & 0x1F) >> 1] = V;

      return;
   }

   if(A < 0x180)
   {
      SPU_Voice *voice = &Voices[A >> 4];

      switch(A & 0xF)
      {
         case 0x00:
         case 0x02:
            SPU_Sweep_WriteControl(&voice->Sweep[(A & 2) >> 1], V);
            break;
         case 0x04:
            voice->Pitch = V;
            break;
         case 0x06:
            voice->StartAddr = (V << 2) & 0x3FFFF;
            break;
         case 0x08:
            voice->ADSRControl &= 0xFFFF0000;
            voice->ADSRControl |= V;
            SPU_CacheEnvelope(voice);
            break;
         case 0x0A:
            voice->ADSRControl &= 0x0000FFFF;
            voice->ADSRControl |= V << 16;
            SPU_CacheEnvelope(voice);
            break;
         case 0x0C:
            voice->ADSR.EnvLevel = V;
            break;
         case 0x0E:
            voice->LoopAddr = (V << 2) & 0x3FFFF;
            voice->IgnoreSampLA = true;
            break;
      }
   }
   else
   {
      switch(A & 0x7F)
      {
         case 0x00:
         case 0x02: SPU_Sweep_WriteControl(&GlobalSweep[(A & 2) >> 1], V);
                    break;

         case 0x04: ReverbVol[0] = (int16)V;
                    break;

         case 0x06: ReverbVol[1] = (int16)V;
                    break;

                    // Voice ON:
         case 0x08: VoiceOn &= 0xFFFF0000;
                    VoiceOn |= V << 0;
                    break;

         case 0x0a: VoiceOn &= 0x0000FFFF;
                    VoiceOn |= (V & 0xFF) << 16;
                    break;

                    // Voice OFF:
         case 0x0c: VoiceOff &= 0xFFFF0000;
                    VoiceOff |= V << 0;
                    break;

         case 0x0e: VoiceOff &= 0x0000FFFF;
                    VoiceOff |= (V & 0xFF) << 16;
                    break;

         case 0x10: FM_Mode &= 0xFFFF0000;
                    FM_Mode |= V << 0;
                    break;

         case 0x12: FM_Mode &= 0x0000FFFF;
                    FM_Mode |= (V & 0xFF) << 16;
                    break;

         case 0x14: Noise_Mode &= 0xFFFF0000;
                    Noise_Mode |= V << 0;
                    break;

         case 0x16: Noise_Mode &= 0x0000FFFF;
                    Noise_Mode |= (V & 0xFF) << 16;
                    break;

         case 0x18: Reverb_Mode &= 0xFFFF0000;
                    Reverb_Mode |= V << 0;
                    break;

         case 0x1A: Reverb_Mode &= 0x0000FFFF;
                    Reverb_Mode |= (V & 0xFF) << 16;
                    break;

         case 0x1C: BlockEnd &= 0xFFFF0000;
                    BlockEnd |= V << 0;
                    break;

         case 0x1E: BlockEnd &= 0x0000FFFF;
                    BlockEnd |= V << 16;
                    break;

         case 0x22: ReverbWA = (V << 2) & 0x3FFFF;
                    ReverbCur = ReverbWA;
                    break;

         case 0x24:
                    IRQAddr = (V << 2) & 0x3FFFF;
                    SPU_CheckIRQAddr(RWAddr);
                    break;

         case 0x26:
                    RWAddr = (V << 2) & 0x3FFFF;	      
                    SPU_CheckIRQAddr(RWAddr);
                    break;

         case 0x28: SPU_WriteSPURAM(RWAddr, V);
                    RWAddr = (RWAddr + 1) & 0x3FFFF;
                    SPU_CheckIRQAddr(RWAddr);
                    break;

         case 0x2A: 

                    SPUControl = V;
                    if(!(V & 0x40))
                    {
                       IRQAsserted = false;
                       IRQ_Assert(IRQ_SPU, IRQAsserted);
                    }
                    SPU_CheckIRQAddr(RWAddr);
                    break;

         case 0x2C: 
                    break;

         case 0x30: CDVol[0] = (int16_t)V;
                    break;

         case 0x32: CDVol[1] = (int16_t)V;
                    break;

         case 0x34: ExternVol[0] = (int16_t)V;
                    break;

         case 0x36: ExternVol[1] = (int16_t)V;
                    break;

         case 0x38:
         case 0x3A: SPU_Sweep_WriteVolume(&GlobalSweep[(A & 2) >> 1], V);
                    break;
      }
   }

   regs.Regs[(A & 0x1FF) >> 1] = V;
}

  uint16 SPU_Read(int32_t timestamp, uint32 A)
{
   A &= 0x3FF;

   if(A >= 0x200)
   {
      if(A < 0x260)
      {
         SPU_Voice *voice = &Voices[(A - 0x200) >> 2];
         return SPU_Sweep_ReadVolume(&voice->Sweep[(A & 2) >> 1]);
      }
      else if(A < 0x280)
         return(AuxRegs[(A & 0x1F) >> 1]);

      return(0xFFFF);
   }


   if(A < 0x180)
   {
      SPU_Voice *voice = &Voices[A >> 4];

      switch(A & 0xF)
      {
         case 0x0C:
            return(voice->ADSR.EnvLevel);
         case 0x0E:
            return(voice->LoopAddr >> 2);
      }
   }
   else
   {
      switch(A & 0x7F)
      {
         case 0x1C:
            return(BlockEnd);
         case 0x1E:
            return(BlockEnd >> 16);
         case 0x26:
            break;
         case 0x28:
            {
               uint16 ret = SPU_ReadSPURAM(RWAddr);

               RWAddr = (RWAddr + 1) & 0x3FFFF;
               SPU_CheckIRQAddr(RWAddr);

               return (ret);
            }

         case 0x2a:
            return(SPUControl);

            /* FIXME: What is this used for? */
         case 0x3C:
            return(0);
         case 0x38:
         case 0x3A:
            return(SPU_Sweep_ReadVolume(&GlobalSweep[(A & 2) >> 1]));
      }
   }

   return(regs.Regs[(A & 0x1FF) >> 1]);
}

  int SPU_StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
#define SFSWEEP(r) SFVAR((r).Control),	\
      SFVAR((r).Current),	\
      SFVAR((r).Divider)

#define SFVOICE(n) SFARRAY16(&Voices[n].DecodeBuffer[0], sizeof(Voices[n].DecodeBuffer) / sizeof(Voices[n].DecodeBuffer[0])),	\
      SFVAR(Voices[n].DecodeM2),											\
      SFVAR(Voices[n].DecodeM1),											\
      SFVAR(Voices[n].DecodePlayDelay),										\
      SFVAR(Voices[n].DecodeWritePos),										\
      SFVAR(Voices[n].DecodeReadPos),										\
      SFVAR(Voices[n].DecodeAvail),										\
      SFVAR(Voices[n].DecodeShift),										\
      SFVAR(Voices[n].DecodeWeight),										\
      SFVAR(Voices[n].DecodeFlags),										\
      SFVAR(Voices[n].IgnoreSampLA),										\
      \
      SFSWEEP(Voices[n].Sweep[0]),											\
      SFSWEEP(Voices[n].Sweep[1]),											\
      \
      SFVAR(Voices[n].Pitch),											\
      SFVAR(Voices[n].CurPhase),											\
      \
      SFVAR(Voices[n].StartAddr),											\
      SFVAR(Voices[n].CurAddr),											\
      SFVAR(Voices[n].ADSRControl),										\
      SFVAR(Voices[n].LoopAddr),											\
      SFVAR(Voices[n].PreLRSample),										\
      \
      SFVAR(Voices[n].ADSR.EnvLevel),										\
      SFVAR(Voices[n].ADSR.Divider),										\
      SFVAR(Voices[n].ADSR.Phase),											\
      \
      SFVAR(Voices[n].ADSR.AttackExp),										\
      SFVAR(Voices[n].ADSR.SustainExp),										\
      SFVAR(Voices[n].ADSR.SustainDec),										\
      SFVAR(Voices[n].ADSR.ReleaseExp),										\
      \
      SFVAR(Voices[n].ADSR.AttackRate),										\
      SFVAR(Voices[n].ADSR.DecayRate),										\
      SFVAR(Voices[n].ADSR.SustainRate),										\
      SFVAR(Voices[n].ADSR.ReleaseRate),										\
      \
      SFVAR(Voices[n].ADSR.SustainLevel)

      SFVOICE(0),
      SFVOICE(1),
      SFVOICE(2),
      SFVOICE(3),
      SFVOICE(4),
      SFVOICE(5),
      SFVOICE(6),
      SFVOICE(7),
      SFVOICE(8),
      SFVOICE(9),
      SFVOICE(10),
      SFVOICE(11),
      SFVOICE(12),
      SFVOICE(13),
      SFVOICE(14),
      SFVOICE(15),
      SFVOICE(16),
      SFVOICE(17),
      SFVOICE(18),
      SFVOICE(19),
      SFVOICE(20),
      SFVOICE(21),
      SFVOICE(22),
      SFVOICE(23),
#undef SFVOICE

      SFVAR(NoiseDivider),
      SFVAR(NoiseCounter),
      SFVAR(LFSR),

      SFVAR(FM_Mode),
      SFVAR(Noise_Mode),
      SFVAR(Reverb_Mode),

      SFVAR(ReverbWA),

      SFSWEEP(GlobalSweep[0]),
      SFSWEEP(GlobalSweep[1]),

      SFARRAY32(ReverbVol, sizeof(ReverbVol) / sizeof(ReverbVol[0])),

      SFARRAY32(CDVol, sizeof(CDVol) / sizeof(CDVol[0])),
      SFARRAY32(ExternVol, sizeof(ExternVol) / sizeof(ExternVol[0])),

      SFVAR(IRQAddr),

      SFVAR(RWAddr),

      SFVAR(SPUControl),

      SFVAR(VoiceOn),
      SFVAR(VoiceOff),

      SFVAR(BlockEnd),

      SFVAR(CWA),

      SFARRAY16(regs.Regs, sizeof(regs.Regs) / sizeof(regs.Regs[0])),
      SFARRAY16(AuxRegs, sizeof(AuxRegs) / sizeof(AuxRegs[0])),

      SFARRAY16(&RDSB[0][0], sizeof(RDSB) / sizeof(RDSB[0][0])),
      SFVAR(RvbResPos),

      SFARRAY16(&RUSB[0][0], sizeof(RUSB) / sizeof(RUSB[0][0])),

      SFVAR(ReverbCur),
      SFVAR(IRQAsserted),

      SFVAR(clock_divider),

      SFARRAY16(SPURAM, 524288 / sizeof(uint16)),
      SFEND
   };
#undef SFSWEEP
   int ret = 1;

   ret &= MDFNSS_StateAction(sm, load, data_only, StateRegs, "SPU");

   if(load)
   {
      for(unsigned i = 0; i < 24; i++)
      {
         Voices[i].DecodeReadPos &= 0x1F;
         Voices[i].DecodeWritePos &= 0x1F;
         Voices[i].CurAddr &= 0x3FFFF;
         Voices[i].StartAddr &= 0x3FFFF;
         Voices[i].LoopAddr &= 0x3FFFF;
      }

      if(clock_divider <= 0 || clock_divider > spu_samples*768)
         clock_divider = spu_samples*768;

      RWAddr &= 0x3FFFF;
      CWA &= 0x1FF;

      ReverbWA &= 0x3FFFF;
      ReverbCur &= 0x3FFFF;

      RvbResPos &= 0x3F;

      IRQ_Assert(IRQ_SPU, IRQAsserted);
   }

   return(ret);
}
