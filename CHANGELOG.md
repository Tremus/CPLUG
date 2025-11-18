## Version 1.1

Breaking changes:
- `cplug_createPlugin()` now takes a parameter to `cplug_createPlugin(CplugHostContext*)`
- The following functions now pass a buffer and buffer size for retrieving strings. This was required to support dynamic name changes and name generation more easily:
    - `cplug_getInputBusName()`
    - `cplug_getOutputBusName()`
    - `cplug_getParameterName()`
    
    We recommend filling the buffer using something like `snprintf()` to ensure that the string buffers are still null terminated, as this is required by all plugin formats
- Removed `cplug_getResizeHints()`. This was a clap only feature and IMO was a bad inclusion to the API. What plugins really need is to detect the corner or edge the user is resizing the window from. This information is only accissible to the top level window ie. the plugin host. No plugin extension relays this information to plugins. If plugins had this info, they could use their own algorithms to appropriately handle resizing, which may include fixed aspect ratio scaling. If your app needs to detect the resize edge/corner, that feature exists in the new [window extension](src/cplug_extensions/window.h).
- Added new mandatory `cplug_getParameterID()` & `cplug_getNumParameters()` function

New features:
- Support parameter IDs. See `cplug_getParameterID()`
- Support note expressions. Currently tuning only
- Better support for unified plugin builds. ie. bunding AU/VST3/CLAP in one dll

- `CplugHostContext->type`: detect wrapper type at runtime
- `CplugHostContext->sendParamEvent()`: Send param change begin/update/end events from the UI thread
- `CplugHostContext->rescan()`: request rescan of plugins latency, tail time, bus count, bus names, parameter values, parameter names, parameter metadata (ranges, default values, param types like float bool and int)
- `CplugHostContext->requestResize()`: Ask the host to resize your window to a certain size
