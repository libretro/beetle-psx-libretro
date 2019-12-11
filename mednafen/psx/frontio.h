#ifndef __MDFN_PSX_FRONTIO_H
#define __MDFN_PSX_FRONTIO_H

#include "../state_helpers.h"

class InputDevice_Multitap;

class InputDevice
{
   public:

      InputDevice();
      virtual ~InputDevice();

      virtual void Power(void);
      virtual void UpdateInput(const void *data);
      virtual int StateAction(StateMem* sm, int load, int data_only, const char* section_name);

      virtual bool RequireNoFrameskip(void);
      // Divide mouse X coordinate by pix_clock_divider in the lightgun code to get the coordinate in pixel(clocks).
      // GPULineHook modified to take upscale_factor for color detection (surf_pitchinpix unused)
      virtual int32_t GPULineHook(const int32_t line_timestamp, bool vsync, uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);

      virtual void Update(const int32_t timestamp);	// Partially-implemented, don't rely on for timing any more fine-grained than a video frame for now.
      virtual void ResetTS(void);

      // DrawCrosshairs modified to take surface pitch (in pixels) and upscale factor for software renderer internal upscaling
      void DrawCrosshairs(uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock, const unsigned surf_pitchinpix, const unsigned upscale_factor);

      virtual void SetAMCT(bool enabled);
      virtual void SetCrosshairsCursor(int cursor);
      virtual void SetCrosshairsColor(uint32_t color);

      virtual void SetDTR(bool new_dtr);
      virtual bool GetDSR(void);	// Currently unused.

      virtual bool Clock(bool TxD, int32_t &dsr_pulse_delay);

      virtual uint8 *GetNVData() { return NULL; }
      virtual uint32_t GetNVSize(void);
      virtual void ReadNV(uint8_t *buffer, uint32_t offset, uint32_t count);
      virtual void WriteNV(const uint8_t *buffer, uint32_t offset, uint32_t count);

      // Dirty count should be incremented on each call to a method this class that causes at least 1 write to occur to the
      // nonvolatile memory(IE Clock() in the correct command phase, and WriteNV()).
      virtual uint64_t GetNVDirtyCount(void);
      virtual void ResetNVDirtyCount(void);

   private:
      unsigned chair_r, chair_g, chair_b;
      int chair_cursor;
   protected:
      int32 chair_x, chair_y;
};

class FrontIO
{
   public:

		enum {
			SETTING_GUN_CROSSHAIR_OFF,
			SETTING_GUN_CROSSHAIR_CROSS,
			SETTING_GUN_CROSSHAIR_DOT,

			SETTING_GUN_CROSSHAIR_LAST,
		};

      FrontIO(bool emulate_memcards_[8], bool emulate_multitap_[2]);
      ~FrontIO();

      void Power(void);
      void Write(int32_t timestamp, uint32_t A, uint32_t V);
      uint32_t Read(int32_t timestamp, uint32_t A);
      int32_t CalcNextEventTS(int32_t timestamp, int32_t next_event);
      int32_t Update(int32_t timestamp);
      void ResetTS(void);

      bool RequireNoFrameskip(void);

      // GPULineHook modified to take surface pitch (in pixels) and upscale factor for software renderer internal upscaling
      void GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);

      void UpdateInput(void);
      void SetInput(unsigned int port, const char *type, void *ptr);
      void SetAMCT(bool enabled);
      void SetCrosshairsCursor(unsigned port, int cursor);
      void SetCrosshairsColor(unsigned port, uint32_t color);

      InputDevice *GetMemcardDevice(unsigned int which);
      uint64_t GetMemcardDirtyCount(unsigned int which);
      void LoadMemcard(unsigned int which, const char *path);
      void LoadMemcard(unsigned int which);
      void SaveMemcard(unsigned int which, const char *path); //, bool force_save = false);
      void SaveMemcard(unsigned int which);

      int StateAction(StateMem* sm, int load, int data_only);

   private:

      void DoDSRIRQ(void);
      void CheckStartStopPending(int32_t timestamp, bool skip_event_set = false);

      void MapDevicesToPorts(void);

      bool emulate_memcards[8];
      bool emulate_multitap[2];

      InputDevice *Ports[2];
      InputDevice *MCPorts[2];

      InputDevice *DummyDevice;
      InputDevice_Multitap *DevicesTap[2];

      InputDevice *Devices[8];
      void *DeviceData[8];

      InputDevice *DevicesMC[8];

      int32_t ClockDivider;

      bool ReceivePending;
      bool TransmitPending;

      bool ReceiveInProgress;
      bool TransmitInProgress;

      bool ReceiveBufferAvail;

      uint8_t ReceiveBuffer;
      uint8_t TransmitBuffer;

      int32_t ReceiveBitCounter;
      int32_t TransmitBitCounter;

      uint16_t Mode;
      uint16_t Control;
      uint16_t Baudrate;


      bool istatus;
      int32_t irq10_pulse_ts[2];

      int32_t dsr_pulse_delay[4];
      int32_t dsr_active_until_ts[4];
      int32_t lastts;
      bool amct_enabled;
      int chair_cursor[8];
      uint32_t chair_colors[8];
};

extern InputInfoStruct FIO_InputInfo;

#endif
