// This fixes an issue caused by possibly glibc-shipped headers
#include <stdint.h>
#ifndef char16_t
typedef int16_t char16_t;
#endif

#include "example/config.h"
#include "src/cplug_clap.c"
#include "src/cplug_vst3.c"

#ifdef _WIN32
#include "src/cplug_standalone_win.c"
#include "example/example.c"
#endif
