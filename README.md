# CPLUG

> _"I just want to build a plugin"_ - Me (and probably you too!)

CPLUG is a simple wrapper API for the VST3, Audio Unit v2 & CLAP plugin formats. There are no extra dependancies outside of the C APIs. It uses a C99 interface, making it easily compatable with other languages. Efforts have been made to use minimal code, making it easy to read and modify. It uses very few files, making it easy to build and include in other projects.

CPLUG only provides the plumbing of wrapping plugin APIs - no extras! It is intended to be compatible with other libraries of your choice (eg. [PUGL](https://github.com/lv2/pugl), [NanoVG](https://github.com/memononen/nanovg), Qt).

There is an optional window library extension ([window.h](src/cplug_extensions/window.h)) to help you get started with writing GUIs. More extensions may be added in future.

All strings are expected to be `\0` terminated & UTF8.

## Changes

Read the [changelog](CHANGELOG.md)

## Building

**TLDR;** To quickly get started, try building the example project using [CMake](CMakeLists.txt)

The idea is [cplug.h](src/cplug.h) contains forward declarations of functions which you implement in your own source file. Pair these with a single source file corresponding to the plugin format you're building.

The source files are configurable using macros you define. A full list of these macros can be found in the [example project](example/config.h). AUv2 and Standalone builds require a few extra macros which are set in [CMakeLists.txt](CMakeLists.txt). If you're building these targets, you should read the CMake file.

| Source file            | Lines of code | Description   | Extra dependencies        |
| ---------------------- | ------------- | ------------- | ------------------------- |
| cplug.h                | ~350          | Common API    | None                      |
| cplug_clap.c           | ~900          | CLAP          | `#include <clap/clap.h>`  |
| cplug_standalone_osx.m | ~1,500        | Standalone    | None                      |
| cplug_auv2.c           | ~1,700        | Audio Unit v2 | None                      |
| cplug_standalone_win.c | ~1,900        | Standalone    | None                      |
| cplug_vst3.c           | ~2,700        | VST3          | `#include <vst3_c_api.h>` |

Copies of the CLAP API and VST3 C API are included in the `src` folder for convenience. They're both single files.

Tested using compilers MinGW GCC 8, VS 17.5, Clang 18 (Windows), Clang 16 (Mac), using C99 & C++11

> [!NOTE]
> Some versions of MinGW may not ship with `mmeapi.h`, which is required for MIDI in Windows standalone builds. Either define the functions and structs yourself, or use a different compiler for this build

## Features

### Included:

-   Uses _sample accurate automation_ by default
-   Standalone builds include hotreloading, and a native menu for switching between sample rates, block sizes, MIDI devices and audio drivers.

### **Not** included

-   _"Distributable"_. Support for external GUIs and external processing
-   Parameter groups.
-   Bus activation/deactivation

Most plugins don't support these features, & most users don't ask for them or know about them. This library takes a YAGNI approach to uncommon features. Because this library is such a thin wrapper over the plugin APIs, adding any feature you need yourself should be a breeze.

## Roadmap

-   CPLUG 2.0:
    - Remove as many macros as possible. Make build scripts as short as possibe. Solve as many problems as possible with C/C++ code, not build scripts
    - Move all cplug methods to one big vtable. This makes supporting multiple plugins in a single dll trivial, want of extensions (eg. parameter, gui, midi) could be implied by setting function pointers on the vtable.
    - Update VST3 C API version to the new MIT licensed one
    - Convert everything into to single header library
    - Allow user to hijack `main()` and hook into file changes so they can write their own custom hotreloading code
-   Finish supporting other note expressions (volume, pan, vibrato, brightness)
-   Emscripten/Web backend
-   Extension: Parameters
-   AUv2: Support multiple input/output busses
-   AUv2: Support sample accurate processing AUv2
-   (Maybe) Support Max 4 Live?
-   (Maybe) Support FL Studio Plugins?
-   (Maybe) Support Linux
-   (I'd rather not) Support AAX

## Useful Resources

-   [VST3 Technical Documentation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Index.html)
-   [Audio Unit Programming Guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/Introduction/Introduction.html)
-   [Audio Unit v2 (C) API](https://developer.apple.com/documentation/audiotoolbox/audio_unit_v2_c_api?language=objc)
-   [CLAP Developers: Getting Started](https://cleveraudio.org/developers-getting-started/)
-   [pluginval](https://github.com/Tracktion/pluginval)
-   [clap-validator](https://github.com/free-audio/clap-validator)

## Licensing

The files [src/vst3_c_api.h](src/vst3_c_api.h) and [src/cplug_vst3.c](src/cplug_vst3.c) are required to use the [VST 3 SDK License](https://forums.steinberg.net/t/vst-3-sdk-license/201637)

The file [src/clap/clap.h](src/clap/clap.h) uses an MIT license included at the top of the file.

Everything else is in the public domain, or MIT if you insist. See [LICENSE](LICENSE).

## Credits

-   Filipe Coelho - Most of the VST3 wrapper and debugging code is a heavily edited version of his code from the [DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF)
-   Nakst - The drawing used in the example plugin was taken from the [CLAP tutorial](https://nakst.gitlab.io/tutorial/clap-part-1.html)
