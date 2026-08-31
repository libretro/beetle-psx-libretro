#ifndef INPUT_COMPATIBILITY_H__
#define INPUT_COMPATIBILITY_H__

#include <stdint.h>

unsigned input_resolve_compatible_controller(
      unsigned requested_device, uint32_t supported_controllers);

#endif
