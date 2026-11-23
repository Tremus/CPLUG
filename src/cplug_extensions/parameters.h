/* Released into the public domain by Tré Dudman - 2026
 * For licensing and more info see https://github.com/Tremus/CPLUG */
/*
CPLUG Parameters extension
Thread-safe parameter storage and synchronisation for audio plugins.
Single header, C99. Depends on libc and cplug.h (with CPLUG's src folder on the include path, like the window
extension). CPLUG itself doesn't depend on this extension: use it or don't.

    // In exactly one .c file:
    #define CPLUG_PARAMETERS_IMPL
    #include <cplug_extensions/parameters.h>

*****************
* The problem   *
*****************
Parameter changes arrive from many places, often on threads the plugin format doesn't name:
- The host's automation, sample-accurate, inside the audio callback
- The host's own UI, control surfaces and preset recalls, on the main thread or on some worker thread
- The plugin's GUI, on the main thread
- State (preset) loads, on whatever thread the host likes

Every change has to reach the audio thread (DSP), the main thread (GUI and other side effects) and, if the plugin made
it, the host (so it can record automation). The host also needs gesture begin/end around user edits.

A common approach keeps two copies of the values (one for audio, one for main) and sends value-carrying messages
between them through fixed-size queues. When a queue fills, a message is dropped, and the two copies disagree until
that parameter changes again.

*****************
* The model     *
*****************
Parameters are state, not events. If a parameter changes twice before anybody looks, only the newest value matters.
So this library has:

1. One store. Each parameter's value is held as an atomic 64-bit word (the bits of a double). It is the only source of
   truth. cparams_get() reads it from any thread.

2. A dirty bitset per consumer. There are three consumers: AUDIO (the DSP), MAIN (the plugin's main-thread side
   effects) and HOST (telling the host about the plugin's own edits). A set bit means "this parameter changed; go and
   re-read the store". It never carries a value, so it can't overflow, can't be lost, and setting it twice is
   harmless. The consumer clears the bit before it reads the value, so a write racing the read sets the bit again and
   is seen next time.

3. Roles instead of flags. Each write function is named after who is writing, and that decides which consumers are
   told. This is what stops echoes: a value the host sent is never reported back to the host.

   | Function                         | Thread                      | Tells                                    |
   |----------------------------------|-----------------------------|------------------------------------------|
   | cparams_gui_set()                | main                        | AUDIO, MAIN, HOST                        |
   | cparams_host_set()               | any                         | AUDIO, MAIN                              |
   | cparams_cplug_event() (a change) | audio                       | MAIN (at block end)                      |
   | cparams_audio_set()              | audio                       | MAIN, HOST                               |
   | cparams_load_set()               | any, between load_begin/end | AUDIO, MAIN, plus a "rescan values" flag |

4. Gestures as bitsets. cparams_gui_begin/end mark a gesture as active. They also leave "begin pending" and "end
   pending" bits for the host pump, which turns them into the host's begin/end calls in a valid order. No queue is
   needed: two quick clicks between pumps merge into one begin, value, end.

5. An audio-thread snapshot. The audio thread reads cparams_audio_get(), a plain array that only changes:
   - at cparams_audio_events_begin() (pulling in changes made elsewhere), and
   - when the host sends a sample-accurate event (cparams_cplug_event()) or the DSP sets a value itself.
   So values are consistent within a block, and reading them costs nothing. The snapshot is a cache of the store, not a
   second truth: a dirty bit always leads back to the store.

All operations are wait-free. No thread ever waits for another, and nothing allocates.

*****************
* Conflicts     *
*****************
CURRENT POLICY: THE PLUGIN'S GUI WINS. This may change in a future version, after more testing in real hosts.
- While a gesture is active (between cparams_gui_begin and cparams_gui_end), host writes to that parameter are
  ignored: cparams_host_set() and cparams_cplug_event() return false.
- Within one audio block, if the GUI (or a state load) writes a parameter that the host's automation also changed,
  the audio thread doesn't overwrite it at block end. The block-end write-back is a compare-and-swap against the value
  the audio thread last saw, so it fails if anyone else wrote in the meantime.
- Once the gesture ends, the host can win again. For example, read-mode automation takes over on its next point.

*****************
* State loads   *
*****************
Wrap a state or preset load in cparams_load_begin() / cparams_load_end(). While any load is in progress,
cparams_audio_events_begin() keeps playing the previous snapshot instead of pulling in changes, so the DSP never sees
half of one preset and half of another. The audio thread doesn't wait: it just picks the whole preset up on a later
block. Loads may run on any thread, including at the same time as each other.

Keep loads short and occasional. While loads run back to back with no gap, the audio thread pulls in no changes at
all, from anyone, until there is a gap. Nothing is lost: everything arrives on the first block after the gap.

The host isn't told about each loaded value. cparams_load_end() raises a "rescan values" flag, and
cparams_main_events_begin() asks the host to re-read every value, with CplugHostContext::rescan and
CPLUG_FLAG_RESCAN_PARAM_VALUES.

*****************
* Threads       *
*****************
Each consumer must be drained by one thread at a time:
- AUDIO: cparams_audio_events_begin / next, cparams_cplug_event, cparams_audio_set and cparams_audio_end. Call them
  from the thread running the audio callback. Hosts may change that thread over time, but never run two audio
  callbacks at the same time for one instance.
- MAIN: cparams_main_events_begin / next, and every cparams_gui_* function.
Everything else (cparams_init_cplug, cparams_index_of, cparams_id_of, cparams_get, cparams_host_set, cparams_load_*)
may be called from any thread.

*****************
* With CPLUG    *
*****************
Functions take parameter indices (0 to num_params - 1), not CPLUG parameter IDs. Works the same way for VST3, CLAP,
AUv2, AAX and standalone:
- cparams_init_cplug(): the count, IDs and defaults come from your own cplug_getNumParameters,
  cplug_getParameterID and cplug_getDefaultParameterValue.
- cparams_index_of() / cparams_id_of(): convert between CPLUG parameter IDs and indices.
- cparams_cplug_event(): call it for each event your cplug_process dequeues. It applies host automation at its frame.
- cparams_main_events_begin(): call it on the main thread from a timer that runs while the plugin exists, and
  optionally each GUI frame. Besides collecting changes for main, it sends your GUI's gestures and values to the host
  with CplugHostContext::sendParamEvent, and asks the host to rescan values after loads. CLAP needs a CPLUG version
  whose CLAP sendParamEvent delivers events (it queues them and asks the host for a flush).
You still implement cplug_getParameterValue and cplug_setParameterValue yourself (see "Usage"), because they need
your plugin's pointer to CParams and your own clamping.

*****************
* Not included  *
*****************
- Parameter metadata: names, ranges, flags. Defaults and IDs are only read, by cparams_init_cplug().
- Normalising. Values are plain (denormalised) doubles. Convert at the format boundary.
- Clamping, and checking for NaN. Validate values before writing them.
- Smoothing. Use cparams_audio_events_next() and the return value of cparams_cplug_event() to start your own ramps.
- Changing ranges at runtime. The library assumes host-facing ranges stay fixed.
- Non-parameter state (sample data, modulation routings, and so on).

*****************
* Usage         *
*****************/
#if 0
// Optional, default 1024. Use the same value in every file that includes this header
#define CPARAMS_MAX_PARAMS 512
#include <cplug.h>
#include <cplug_extensions/parameters.h>

typedef struct Plugin
{
    CplugHostContext* host;
    CParams*          params;
    // ...
} Plugin;

void* cplug_createPlugin(CplugHostContext* host)
{
    Plugin* plugin = calloc(1, sizeof(*plugin));
    plugin->host   = host;
    plugin->params = calloc(1, sizeof(CParams));
    // Once cplug_getNumParameters, cplug_getParameterID and cplug_getDefaultParameterValue can answer
    cparams_init_cplug(plugin->params, plugin);
    return plugin;
}

// [Any thread]
double cplug_getParameterValue(void* ptr, uint32_t id)
{
    Plugin*    plugin = ptr;
    uint32_t   idx    = 0;
    const bool known  = cparams_index_of(plugin->params, id, &idx);
    return known ? cparams_get(plugin->params, idx) : 0;
}

// [Any thread] Host writes that aren't tied to a sample position
void cplug_setParameterValue(void* ptr, uint32_t id, double value)
{
    Plugin*    plugin = ptr;
    uint32_t   idx    = 0;
    const bool known  = cparams_index_of(plugin->params, id, &idx);
    if (known)
        cparams_host_set(plugin->params, idx, my_clamp(idx, value));
}

// [Audio] Also called with numFrames == 0, to exchange events only
void cplug_process(void* ptr, CplugProcessContext* ctx)
{
    Plugin*  plugin = ptr;
    uint32_t idx    = 0;

    cparams_audio_events_begin(plugin->params);
    while (cparams_audio_events_next(plugin->params, &idx))
        my_dsp_param_changed(idx, cparams_audio_get(plugin->params, idx));

    CplugEvent event;
    uint32_t   frame = 0;
    while (ctx->dequeueEvent(ctx, &event, frame))
    {
        const bool changed = cparams_cplug_event(plugin->params, &event, &idx);
        if (changed)
        {
            my_dsp_param_changed(idx, cparams_audio_get(plugin->params, idx));
            continue;
        }
        // Parameter events with nothing to do also land here. They match no case, so nothing happens
        switch (event.type)
        {
        case CPLUG_EVENT_PROCESS_AUDIO:
            frame = my_render_audio(plugin, ctx, frame, event.processAudio.endFrame);
            break;
        // ...MIDI and other events
        default:
            break;
        }
    }

    cparams_audio_end(plugin->params);
}

// [Any thread]
void cplug_loadState(void* ptr, const void* stateCtx, cplug_readProc readProc)
{
    Plugin* plugin = ptr;
    cparams_load_begin(plugin->params);
    // ...for each saved parameter: cparams_load_set(plugin->params, idx, value)
    cparams_load_end(plugin->params);
}

// [Main] GUI knob drag
cparams_gui_begin(plugin->params, idx);
cparams_gui_set(plugin->params, idx, value); // as often as you like
cparams_gui_end(plugin->params, idx);
// A one-off edit (eg. a click on a toggle) is just cparams_gui_set(): it adds its own begin/end
// Draw knobs from cparams_get(plugin->params, idx)

// [Main] a timer that runs while the plugin exists (eg. every 200 ms), and optionally each GUI frame
cparams_main_events_begin(plugin->params, plugin->host); // also tells the host about GUI edits and loads
while (cparams_main_events_next(plugin->params, &idx))
    my_main_param_changed(idx, cparams_get(plugin->params, idx));
#endif // example code
/****************
* Tests         *
*****************
A unit test and multi-threaded stress test are built in. In a program of its own:

    #define CPLUG_PARAMETERS_IMPL
    #define CPLUG_PARAMETERS_TEST
    #include <cplug_extensions/parameters.h>
    int main(void) { return cparams_test_run(); }

Requires CPARAMS_MAX_PARAMS >= 300. Preferably don't set it, use the code above
*/

#ifndef CPLUG_PARAMETERS_H
#define CPLUG_PARAMETERS_H

#include <cplug.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CPARAMS_MAX_PARAMS
#define CPARAMS_MAX_PARAMS 1024
#endif

#define CPARAMS_WORDS ((CPARAMS_MAX_PARAMS + 63) / 64)

// Keeps data written by different threads on different cache lines. 128 covers Apple Silicon
#define CPARAMS_CACHE_LINE 128

// Treat every field as private. The layout is public only so you can allocate it
typedef struct CParams
{
    uint32_t num_params;

    // Set by cparams__init / cparams_init_cplug, read-only after. cparams__init makes every ID equal its index
    uint32_t id[CPARAMS_MAX_PARAMS];        // index -> CPLUG parameter ID
    uint64_t id_lookup[CPARAMS_MAX_PARAMS]; // (ID << 32 | index), sorted by ID, for cparams_index_of

    // Shared: written and read from any thread, always through atomics
    char     pad0[CPARAMS_CACHE_LINE];
    uint64_t value[CPARAMS_MAX_PARAMS];  // the store: bits of a double
    uint64_t dirty_audio[CPARAMS_WORDS]; // consumer bitsets: "re-read the store"
    uint64_t dirty_main[CPARAMS_WORDS];
    uint64_t dirty_host[CPARAMS_WORDS];
    uint64_t gesture_active[CPARAMS_WORDS]; // written by main, read by everyone
    uint64_t gesture_begin[CPARAMS_WORDS];  // pending for the host pump
    uint64_t gesture_end[CPARAMS_WORDS];    // pending for the host pump
    uint32_t load_count;                    // loads in progress
    uint32_t load_epoch;                    // incremented by every cparams_load_begin
    uint32_t rescan;                        // set by cparams_load_end, taken by main

    // Audio thread only. Plain memory
    char     pad1[CPARAMS_CACHE_LINE];
    double   audio_value[CPARAMS_MAX_PARAMS];  // the snapshot DSP reads
    uint64_t audio_synced[CPARAMS_MAX_PARAMS]; // store bits when audio last synced: the write-back CAS's expected value
    uint64_t audio_staged[CPARAMS_MAX_PARAMS]; // values read in cparams_audio_events_begin, before they are known to be
                                               // whole
    uint64_t audio_changed[CPARAMS_WORDS];     // reported by cparams_audio_events_next
    uint64_t audio_touched[CPARAMS_WORDS];     // changed by host events this block

    // Main thread only. Plain memory
    char     pad2[CPARAMS_CACHE_LINE];
    uint64_t main_changed[CPARAMS_WORDS]; // reported by cparams_main_events_next

    // Host pump thread only. Plain memory
    char     pad3[CPARAMS_CACHE_LINE];
    uint64_t host_begin[CPARAMS_WORDS]; // captured by cparams__host_begin
    uint64_t host_value[CPARAMS_WORDS];
    uint64_t host_end[CPARAMS_WORDS];
    uint64_t host_open[CPARAMS_WORDS]; // gestures the host has been told began and not yet ended
    char     pad4[CPARAMS_CACHE_LINE];
} CParams;

// ---------------------------------------------------------------------------------------------------------------------
// Setup

// [Any thread]
// Ideally call within cplug_createPlugin, or before any system requires param values
// cparams_init_cplug will call cplug_getNumParameters, cplug_getParameterID and cplug_getDefaultParameterValue.
void cparams_init_cplug(CParams* p, void* userPlugin);

// [Any thread] CPLUG parameter ID -> index. Returns false for an ID the plugin doesn't have
bool cparams_index_of(const CParams* p, uint32_t param_id, uint32_t* idx);
// [Any thread] Index -> CPLUG parameter ID
uint32_t cparams_id_of(const CParams* p, uint32_t idx);

// ---------------------------------------------------------------------------------------------------------------------
// Any thread

// The newest value from any writer. Use it for the GUI, host getters (cplug_getParameterValue) and saving state. On
// the audio thread, use cparams_audio_get() instead
double cparams_get(const CParams* p, uint32_t idx);

// A value change from the host that isn't tied to a sample position (cplug_setParameterValue). Tells AUDIO and MAIN,
// never HOST. Returns false if it was ignored because the GUI is editing that parameter (see "Conflicts")
bool cparams_host_set(CParams* p, uint32_t idx, double value);

// Start a state or preset load. Loads may overlap. Until the last one ends, the audio thread keeps the old snapshot
void cparams_load_begin(CParams* p);
// Write one loaded value. Tells AUDIO and MAIN. The host is told about the whole load once, by
// cparams_main_events_begin(). Loads ignore gestures: loading a preset is a deliberate act
void cparams_load_set(CParams* p, uint32_t idx, double value);
void cparams_load_end(CParams* p);

// ---------------------------------------------------------------------------------------------------------------------
// Main thread

// The user started editing idx (eg. mouse down on a knob)
void cparams_gui_begin(CParams* p, uint32_t idx);
// The GUI (or any main-thread plugin logic) sets idx. Tells AUDIO, MAIN and HOST.
// Outside a gesture this is a one-off edit, and gets its own begin/end for the host
void cparams_gui_set(CParams* p, uint32_t idx, double value);
// The user stopped editing idx (eg. mouse up)
void cparams_gui_end(CParams* p, uint32_t idx);

// Call from a timer for the plugin's whole lifetime, even while the GUI is closed, and optionally each GUI frame. The
// time between calls is how late the host hears about GUI edits. Calling it more often is always safe.
// 1. Sends the GUI's (and cparams_audio_set's) gestures and values to the host with ctx->sendParamEvent
// 2. If a load has finished, calls ctx->rescan(ctx, CPLUG_FLAG_RESCAN_PARAM_VALUES)
// 3. Collects every parameter changed by anyone (GUI, host, loads, DSP) since the last call. Iterate them with
//    cparams_main_events_next(). A change not iterated is kept for the next call
// ctx may be NULL: steps 1 and 2 are skipped, and what they would send stays pending for a later call with a ctx.
// A host whose sendParamEvent or rescan is NULL is skipped for that step
void cparams_main_events_begin(CParams* p, CplugHostContext* ctx);
// Read the value with cparams_get(). It may be newer than the change that set the bit
bool cparams_main_events_next(CParams* p, uint32_t* idx);

// ---------------------------------------------------------------------------------------------------------------------
// Audio thread

// Start of an audio block. Pulls changes made elsewhere into the snapshot. Iterate them with
// cparams_audio_events_next(). While a state load is in progress it pulls nothing, and the DSP keeps the previous
// snapshot
void cparams_audio_events_begin(CParams* p);
// Parameters pulled in by cparams_audio_events_begin(). Read the value with cparams_audio_get()
bool cparams_audio_events_next(CParams* p, uint32_t* idx);

// For each event cplug_process dequeues, between cparams_audio_events_begin and cparams_audio_end. A host parameter
// change updates the snapshot now; the store and MAIN are updated at cparams_audio_end().
// Returns true if a parameter's value changed: *idx is set, and the DSP should apply cparams_audio_get(p, idx) at this
// frame. Returns false for everything else: events that aren't parameter changes (handle audio, MIDI and so on
// yourself), unknown IDs, gestures, and changes ignored because the GUI is editing that parameter (see "Conflicts")
bool cparams_cplug_event(CParams* p, const CplugEvent* event, uint32_t* idx);

// The value the DSP should use now. Only changes inside cparams_audio_events_begin(), cparams_cplug_event() and
// cparams_audio_set()
double cparams_audio_get(const CParams* p, uint32_t idx);
// The DSP sets one of its own parameters (eg. a value learned from the input). Tells MAIN and HOST
void cparams_audio_set(CParams* p, uint32_t idx, double value);
// End of an audio block. Writes the final value of each host-automated parameter to the store, unless someone else
// wrote it during the block. That writer wins, and the next cparams_audio_events_begin() pulls its value in
void cparams_audio_end(CParams* p);

#ifdef __cplusplus
}
#endif

#endif // CPLUG_PARAMETERS_H

#ifdef CPLUG_PARAMETERS_IMPL
#undef CPLUG_PARAMETERS_IMPL
#define CPARAMS__IMPL_INCLUDED

#include <stdlib.h>
#include <string.h>

#ifndef CPARAMS_ASSERT
#include <assert.h>
#define CPARAMS_ASSERT(cond) assert(cond)
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Private API, used by the CPLUG helpers and the tests. Not part of the public API, and may change

// Setup without CPLUG: every ID equals its index. defaults may be NULL, meaning every value starts at 0
void cparams__init(CParams* p, uint32_t num_params, const double* defaults);

// [Any thread] True if the host pump has something to send
bool cparams__host_pending(const CParams* p);
// [Any thread] True while the GUI is editing idx
bool cparams__gui_is_editing(const CParams* p, uint32_t idx);
// [Main] True once after one or more state loads have finished. cparams_main_events_begin() takes it
bool cparams__take_rescan(CParams* p);

// [Audio] A sample-accurate host event at the current position in the block. Updates the snapshot now. The store and
// MAIN are updated at cparams_audio_end(). Returns false if it was ignored because the GUI is editing that parameter.
// cparams_cplug_event() calls it
bool cparams__audio_host_event(CParams* p, uint32_t idx, double value);

// Host pump: one thread at a time. cparams_main_events_begin() runs it on main
typedef enum CParams__HostEventType
{
    CPARAMS__GESTURE_BEGIN, // the user started editing idx
    CPARAMS__VALUE,         // idx now has value
    CPARAMS__GESTURE_END,   // the user stopped editing idx
} CParams__HostEventType;

typedef struct CParams__HostEvent
{
    CParams__HostEventType type;
    uint32_t               idx;
    double                 value; // CPARAMS__VALUE only
} CParams__HostEvent;

// Captures everything to send to the host since the last call
void cparams__host_begin(CParams* p);
// Returns events in the order: every gesture begin, then every value, then every gesture end.
// The host is never sent a begin for a gesture it thinks is open, or an end for one it thinks is closed
bool cparams__host_next(CParams* p, CParams__HostEvent* ev);

// ---------------------------------------------------------------------------------------------------------------------
// Atomics
// Every shared access is sequentially consistent. The rates involved (a few thousand operations per second) make the
// cost irrelevant, and it keeps the reasoning in cparams_audio_events_begin simple: it relies on one total order of the
// loader's epoch increment, the loader's value writes, and the audio thread's reads.

#if defined(__GNUC__) || defined(__clang__)

static inline uint64_t cparams__load64(const uint64_t* p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void     cparams__store64(uint64_t* p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static inline uint64_t cparams__exchange64(uint64_t* p, uint64_t v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
static inline uint64_t cparams__fetch_add64(uint64_t* p, uint64_t v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
static inline void cparams__or64(uint64_t* p, uint64_t v) { __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static inline void cparams__and64(uint64_t* p, uint64_t v) { __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static inline bool cparams__cas64(uint64_t* p, uint64_t expected, uint64_t desired)
{
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline uint32_t cparams__load32(const uint32_t* p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void     cparams__store32(uint32_t* p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static inline uint32_t cparams__exchange32(uint32_t* p, uint32_t v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
static inline uint32_t cparams__fetch_add32(uint32_t* p, uint32_t v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
static inline int cparams__ctz64(uint64_t v) { return __builtin_ctzll(v); }

#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))

#include <intrin.h>
// Interlocked operations are full barriers. On x64, a plain load is sequentially consistent when every store is an
// interlocked exchange, which is how the stores here work. arm64 needs a load-acquire instruction
static inline uint64_t cparams__load64(const uint64_t* p)
{
#if defined(_M_ARM64)
    return __ldar64((unsigned __int64 volatile*)p);
#else
    uint64_t v = *(const volatile uint64_t*)p;
    _ReadWriteBarrier();
    return v;
#endif
}
static inline void cparams__store64(uint64_t* p, uint64_t v)
{
    _InterlockedExchange64((volatile __int64*)p, (__int64)v);
}
static inline uint64_t cparams__exchange64(uint64_t* p, uint64_t v)
{
    return (uint64_t)_InterlockedExchange64((volatile __int64*)p, (__int64)v);
}
static inline uint64_t cparams__fetch_add64(uint64_t* p, uint64_t v)
{
    return (uint64_t)_InterlockedExchangeAdd64((volatile __int64*)p, (__int64)v);
}
static inline void cparams__or64(uint64_t* p, uint64_t v) { _InterlockedOr64((volatile __int64*)p, (__int64)v); }
static inline void cparams__and64(uint64_t* p, uint64_t v) { _InterlockedAnd64((volatile __int64*)p, (__int64)v); }
static inline bool cparams__cas64(uint64_t* p, uint64_t expected, uint64_t desired)
{
    const __int64 prev = _InterlockedCompareExchange64((volatile __int64*)p, (__int64)desired, (__int64)expected);
    return (uint64_t)prev == expected;
}
static inline uint32_t cparams__load32(const uint32_t* p)
{
#if defined(_M_ARM64)
    return __ldar32((unsigned __int32 volatile*)p);
#else
    uint32_t v = *(const volatile uint32_t*)p;
    _ReadWriteBarrier();
    return v;
#endif
}
static inline void     cparams__store32(uint32_t* p, uint32_t v) { _InterlockedExchange((volatile long*)p, (long)v); }
static inline uint32_t cparams__exchange32(uint32_t* p, uint32_t v)
{
    return (uint32_t)_InterlockedExchange((volatile long*)p, (long)v);
}
static inline uint32_t cparams__fetch_add32(uint32_t* p, uint32_t v)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)p, (long)v);
}
static inline int cparams__ctz64(uint64_t v)
{
    unsigned long idx = 0;
    _BitScanForward64(&idx, v);
    return (int)idx;
}

#else
#error "cplug parameters: needs 64-bit atomics (gcc/clang, or MSVC targeting x64 or arm64)"
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Internal helpers

static inline uint64_t cparams__bits(double v)
{
    uint64_t b;
    memcpy(&b, &v, sizeof(b));
    return b;
}
static inline double cparams__double(uint64_t b)
{
    double v;
    memcpy(&v, &b, sizeof(v));
    return v;
}
static inline uint32_t cparams__num_words(const CParams* p) { return (p->num_params + 63) / 64; }
static inline uint64_t cparams__mask(uint32_t idx) { return (uint64_t)1 << (idx & 63); }
static inline void     cparams__set_bit(uint64_t* words, uint32_t idx)
{
    cparams__or64(&words[idx >> 6], cparams__mask(idx));
}
static inline bool cparams__test_bit(const uint64_t* words, uint32_t idx)
{
    const uint64_t word = cparams__load64(&words[idx >> 6]);
    return (word & cparams__mask(idx)) != 0;
}

// Pops the lowest set bit from a thread-owned (non-atomic) bitset
static inline bool cparams__pop(uint64_t* words, uint32_t num_words, uint32_t* idx)
{
    for (uint32_t w = 0; w < num_words; w++)
    {
        const uint64_t bits = words[w];
        if (bits)
        {
            const int bit = cparams__ctz64(bits);
            words[w]      = bits & (bits - 1);
            *idx          = w * 64 + (uint32_t)bit;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup

void cparams__init(CParams* p, uint32_t num_params, const double* defaults)
{
    CPARAMS_ASSERT(num_params <= CPARAMS_MAX_PARAMS);
    if (num_params > CPARAMS_MAX_PARAMS)
        num_params = CPARAMS_MAX_PARAMS;

    memset(p, 0, sizeof(*p));
    p->num_params = num_params;
    for (uint32_t i = 0; i < num_params; i++)
    {
        const double   v    = defaults ? defaults[i] : 0.0;
        const uint64_t bits = cparams__bits(v);
        p->value[i]         = bits;
        p->audio_value[i]   = v;
        p->audio_synced[i]  = bits;
        p->id[i]            = i;
        p->id_lookup[i]     = ((uint64_t)i << 32) | i; // Already sorted
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Any thread

double cparams_get(const CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const uint64_t bits = cparams__load64(&p->value[idx]);
    return cparams__double(bits);
}

bool cparams_host_set(CParams* p, uint32_t idx, double value)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const bool is_gesturing = cparams__test_bit(p->gesture_active, idx);
    if (is_gesturing)
        return false;

    // Hosts may call this repeatedly with the value they already have. Skipping it saves the consumers a wake-up
    const uint64_t bits = cparams__bits(value);
    const uint64_t prev = cparams__load64(&p->value[idx]);
    if (prev == bits)
        return true;

    cparams__store64(&p->value[idx], bits);
    cparams__set_bit(p->dirty_audio, idx);
    cparams__set_bit(p->dirty_main, idx);
    return true;
}

bool cparams__host_pending(const CParams* p)
{
    const uint32_t num_words = cparams__num_words(p);
    for (uint32_t w = 0; w < num_words; w++)
    {
        const uint64_t begins = cparams__load64(&p->gesture_begin[w]);
        const uint64_t values = cparams__load64(&p->dirty_host[w]);
        const uint64_t ends   = cparams__load64(&p->gesture_end[w]);
        if (begins | values | ends)
            return true;
    }
    return false;
}

void cparams_load_begin(CParams* p)
{
    cparams__fetch_add32(&p->load_count, 1);
    cparams__fetch_add32(&p->load_epoch, 1);
}

void cparams_load_set(CParams* p, uint32_t idx, double value)
{
    CPARAMS_ASSERT(idx < p->num_params);
    cparams__store64(&p->value[idx], cparams__bits(value));
    cparams__set_bit(p->dirty_audio, idx);
    cparams__set_bit(p->dirty_main, idx);
}

void cparams_load_end(CParams* p)
{
    cparams__store32(&p->rescan, 1);
    cparams__fetch_add32(&p->load_count, (uint32_t)-1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Main thread: GUI edits

void cparams_gui_begin(CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const bool is_gesturing = cparams__test_bit(p->gesture_active, idx);
    if (is_gesturing)
        return;
    cparams__set_bit(p->gesture_active, idx);
    cparams__set_bit(p->gesture_begin, idx);
}

// The bits are set in the order begin, value, end. cparams__host_begin() captures them in reverse order, so a value it
// captures always has its begin captured too
void cparams_gui_set(CParams* p, uint32_t idx, double value)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const uint64_t bits = cparams__bits(value);
    const uint64_t prev = cparams__load64(&p->value[idx]);
    if (prev == bits)
        return;

    const bool is_gesturing = cparams__test_bit(p->gesture_active, idx);
    if (!is_gesturing)
        cparams__set_bit(p->gesture_begin, idx);

    cparams__store64(&p->value[idx], bits);
    cparams__set_bit(p->dirty_audio, idx);
    cparams__set_bit(p->dirty_main, idx);
    cparams__set_bit(p->dirty_host, idx);

    if (!is_gesturing)
        cparams__set_bit(p->gesture_end, idx);
}

void cparams_gui_end(CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const bool is_gesturing = cparams__test_bit(p->gesture_active, idx);
    if (!is_gesturing)
        return;
    cparams__and64(&p->gesture_active[idx >> 6], ~cparams__mask(idx));
    cparams__set_bit(p->gesture_end, idx);
}

bool cparams__gui_is_editing(const CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    return cparams__test_bit(p->gesture_active, idx);
}

// ---------------------------------------------------------------------------------------------------------------------
// Main thread: reacting to changes

// Host first, then main's changes. A host that answers sendParamEvent or rescan by calling cplug_setParameterValue
// straight back has that change collected in this same call
void cparams_main_events_begin(CParams* p, CplugHostContext* ctx)
{
    if (ctx && ctx->sendParamEvent)
    {
        CParams__HostEvent ev;
        cparams__host_begin(p);
        while (true)
        {
            const bool has_event = cparams__host_next(p, &ev);
            if (!has_event)
                break;

            CplugEvent event;
            memset(&event, 0, sizeof(event));
            switch (ev.type)
            {
            case CPARAMS__GESTURE_BEGIN:
                event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
                break;
            case CPARAMS__VALUE:
                event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                break;
            case CPARAMS__GESTURE_END:
                event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_END;
                break;
            }
            event.parameter.id    = p->id[ev.idx];
            event.parameter.value = ev.value;
            ctx->sendParamEvent(ctx, &event);
        }
    }

    if (ctx)
    {
        const bool rescan = cparams__take_rescan(p);
        if (rescan && ctx->rescan)
            ctx->rescan(ctx, CPLUG_FLAG_RESCAN_PARAM_VALUES);
    }

    {
        const uint32_t num_words = cparams__num_words(p);
        for (uint32_t w = 0; w < num_words; w++)
        {
            const uint64_t bits  = cparams__exchange64(&p->dirty_main[w], 0);
            p->main_changed[w]  |= bits;
        }
    }
}

bool cparams_main_events_next(CParams* p, uint32_t* idx)
{
    return cparams__pop(p->main_changed, cparams__num_words(p), idx);
}

bool cparams__take_rescan(CParams* p)
{
    const uint32_t prev = cparams__exchange32(&p->rescan, 0);
    return prev != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Audio thread

void cparams_audio_events_begin(CParams* p)
{
    // A load that starts after epoch_before is read either raises load_count (seen below) or changes the epoch (seen
    // at the end). In both cases nothing torn is applied
    const uint32_t epoch_before = cparams__load32(&p->load_epoch);
    const uint32_t loading      = cparams__load32(&p->load_count);
    if (loading)
        return;

    const uint32_t num_words = cparams__num_words(p);
    uint64_t       taken[CPARAMS_WORDS];
    {
        // Clear first, then read. A writer racing this sets the bit again, and its value is read next block
        for (uint32_t w = 0; w < num_words; w++)
        {
            taken[w]      = cparams__exchange64(&p->dirty_audio[w], 0);
            uint64_t bits = taken[w];
            while (bits)
            {
                const uint32_t idx    = w * 64 + (uint32_t)cparams__ctz64(bits);
                bits                 &= bits - 1;
                p->audio_staged[idx]  = cparams__load64(&p->value[idx]);
            }
        }
    }

    const uint32_t epoch_after = cparams__load32(&p->load_epoch);
    if (epoch_after != epoch_before)
    {
        // A load began while we were reading, so some staged values may be half of a preset. Drop them and give the
        // bits back. The snapshot stays as it was
        for (uint32_t w = 0; w < num_words; w++)
        {
            if (taken[w])
                cparams__or64(&p->dirty_audio[w], taken[w]);
        }
        return;
    }

    for (uint32_t w = 0; w < num_words; w++)
    {
        uint64_t bits = taken[w];
        while (bits)
        {
            const uint32_t idx    = w * 64 + (uint32_t)cparams__ctz64(bits);
            bits                 &= bits - 1;
            p->audio_synced[idx]  = p->audio_staged[idx];
            p->audio_value[idx]   = cparams__double(p->audio_staged[idx]);
        }
        p->audio_changed[w] |= taken[w];
    }
}

bool cparams_audio_events_next(CParams* p, uint32_t* idx)
{
    return cparams__pop(p->audio_changed, cparams__num_words(p), idx);
}

double cparams_audio_get(const CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    return p->audio_value[idx];
}

bool cparams__audio_host_event(CParams* p, uint32_t idx, double value)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const bool is_gesturing = cparams__test_bit(p->gesture_active, idx);
    if (is_gesturing)
        return false;
    p->audio_value[idx]         = value;
    p->audio_touched[idx >> 6] |= cparams__mask(idx);
    return true;
}

void cparams_audio_set(CParams* p, uint32_t idx, double value)
{
    CPARAMS_ASSERT(idx < p->num_params);
    const uint64_t bits  = cparams__bits(value);
    p->audio_value[idx]  = value;
    p->audio_synced[idx] = bits;
    cparams__store64(&p->value[idx], bits);
    cparams__set_bit(p->dirty_main, idx);
    cparams__set_bit(p->dirty_host, idx);
}

// The write-back is a compare-and-swap against the store bits audio last synced. It fails if anyone else wrote
// during the block; that writer's dirty bit is already set, so the next cparams_audio_events_begin() pulls its value in
void cparams_audio_end(CParams* p)
{
    const uint32_t num_words = cparams__num_words(p);
    for (uint32_t w = 0; w < num_words; w++)
    {
        uint64_t bits = p->audio_touched[w];
        while (bits)
        {
            const uint32_t idx  = w * 64 + (uint32_t)cparams__ctz64(bits);
            bits               &= bits - 1;

            const uint64_t next     = cparams__bits(p->audio_value[idx]);
            const uint64_t expected = p->audio_synced[idx];
            if (next == expected)
                continue; // Hosts often resend the same value every block

            const bool swapped = cparams__cas64(&p->value[idx], expected, next);
            if (swapped)
            {
                p->audio_synced[idx] = next;
                cparams__set_bit(p->dirty_main, idx);
            }
        }
        p->audio_touched[w] = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Host pump

// Capture order is ends, values, begins: the reverse of the order cparams_gui_set/begin/end set them. So if a value or
// end is captured, the begin before it is captured too (or was sent by an earlier pump)
void cparams__host_begin(CParams* p)
{
    const uint32_t num_words = cparams__num_words(p);
    for (uint32_t w = 0; w < num_words; w++)
    {
        const uint64_t ends  = cparams__exchange64(&p->gesture_end[w], 0);
        p->host_end[w]      |= ends;
    }
    for (uint32_t w = 0; w < num_words; w++)
    {
        const uint64_t values  = cparams__exchange64(&p->dirty_host[w], 0);
        p->host_value[w]      |= values;
    }
    for (uint32_t w = 0; w < num_words; w++)
    {
        const uint64_t begins  = cparams__exchange64(&p->gesture_begin[w], 0);
        p->host_begin[w]      |= begins;
    }
}

// An end is held back while the GUI has already started a new gesture on that parameter. That gesture's own end is
// sent later
bool cparams__host_next(CParams* p, CParams__HostEvent* ev)
{
    const uint32_t num_words = cparams__num_words(p);
    uint32_t       idx       = 0;

    while (cparams__pop(p->host_begin, num_words, &idx))
    {
        const uint64_t mask    = cparams__mask(idx);
        const bool     is_open = (p->host_open[idx >> 6] & mask) != 0;
        if (is_open)
            continue;
        p->host_open[idx >> 6] |= mask;
        ev->type                = CPARAMS__GESTURE_BEGIN;
        ev->idx                 = idx;
        ev->value               = 0;
        return true;
    }

    const bool has_value = cparams__pop(p->host_value, num_words, &idx);
    if (has_value)
    {
        ev->type  = CPARAMS__VALUE;
        ev->idx   = idx;
        ev->value = cparams_get(p, idx);
        return true;
    }

    while (cparams__pop(p->host_end, num_words, &idx))
    {
        const uint64_t mask         = cparams__mask(idx);
        const bool     is_open      = (p->host_open[idx >> 6] & mask) != 0;
        const bool     is_gesturing = cparams__test_bit(p->gesture_active, idx);
        if (!is_open || is_gesturing)
            continue;
        p->host_open[idx >> 6] &= ~mask;
        ev->type                = CPARAMS__GESTURE_END;
        ev->idx                 = idx;
        ev->value               = 0;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// CPLUG helpers

// Sorts id_lookup entries by ID. IDs are unique, so the index in the low bits never decides the order
static int cparams__compare_u64(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

void cparams_init_cplug(CParams* p, void* userPlugin)
{
    const uint32_t num_params = cplug_getNumParameters(userPlugin);
    cparams__init(p, num_params, NULL);

    for (uint32_t i = 0; i < p->num_params; i++)
    {
        const uint32_t id   = cplug_getParameterID(userPlugin, i);
        const double   v    = cplug_getDefaultParameterValue(userPlugin, id);
        const uint64_t bits = cparams__bits(v);
        p->id[i]            = id;
        p->id_lookup[i]     = ((uint64_t)id << 32) | i;
        p->value[i]         = bits;
        p->audio_value[i]   = v;
        p->audio_synced[i]  = bits;
    }
    qsort(p->id_lookup, p->num_params, sizeof(p->id_lookup[0]), cparams__compare_u64);

#ifndef NDEBUG
    for (uint32_t i = 1; i < p->num_params; i++)
    {
        const uint32_t prev_id = (uint32_t)(p->id_lookup[i - 1] >> 32);
        const uint32_t id      = (uint32_t)(p->id_lookup[i] >> 32);
        CPARAMS_ASSERT(prev_id != id); // Two parameters share an ID
    }
#endif
}

// Binary search of id_lookup
bool cparams_index_of(const CParams* p, uint32_t param_id, uint32_t* idx)
{
    uint32_t lo = 0;
    uint32_t hi = p->num_params;
    while (lo < hi)
    {
        const uint32_t mid   = lo + (hi - lo) / 2;
        const uint64_t entry = p->id_lookup[mid];
        const uint32_t id    = (uint32_t)(entry >> 32);
        if (id == param_id)
        {
            *idx = (uint32_t)(entry & 0xffffffff);
            return true;
        }
        if (id < param_id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

uint32_t cparams_id_of(const CParams* p, uint32_t idx)
{
    CPARAMS_ASSERT(idx < p->num_params);
    return p->id[idx];
}

bool cparams_cplug_event(CParams* p, const CplugEvent* event, uint32_t* idx)
{
    // Hosts don't send gestures (BEGIN/END) to plugins, but if one does, there's nothing to apply
    if (event->type != CPLUG_EVENT_PARAM_CHANGE_UPDATE)
        return false;

    uint32_t   found = 0;
    const bool known = cparams_index_of(p, event->parameter.id, &found);
    if (!known)
        return false;

    const bool accepted = cparams__audio_host_event(p, found, event->parameter.value);
    if (accepted)
        *idx = found;
    return accepted;
}

#endif // CPLUG_PARAMETERS_IMPL

#ifdef CPLUG_PARAMETERS_TEST
#undef CPLUG_PARAMETERS_TEST

// Unit and stress tests. Call cparams_test_run() from a program of its own (see "Tests" at the top). Test-only names
// are prefixed cpt_.
//
// Part 1 checks each rule single-threaded. Part 2 runs threads playing the GUI, host writers, a state loader, the audio
// callback and the host pump, then checks:
// - Torn values: every value any consumer sees is one some writer actually wrote, for that parameter. Each value
//   encodes (writer, param index, sequence number), and a sequence number may not be ahead of what that writer has
//   published
// - Lost updates: a write that races the audio thread's drain still reaches the audio thread
// - Convergence: once writers stop and every consumer drains once more, the audio snapshot and main's view equal the
//   store
// - Gestures: the host is never sent a begin for an open gesture or an end for a closed one, and none are left open
// - Torn presets: the audio snapshot never holds parts of two different loads
// - GUI wins: while the GUI holds a gesture, automation on the audio thread never replaces the GUI's value

#ifndef CPARAMS__IMPL_INCLUDED
#error "Define CPLUG_PARAMETERS_IMPL in the same file as CPLUG_PARAMETERS_TEST"
#endif
#if CPARAMS_MAX_PARAMS < 300
#error "The tests need CPARAMS_MAX_PARAMS >= 300"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int cparams_test_run(void); // Returns 0 if everything passed. Prints progress and failures to stderr

// ---------------------------------------------------------------------------------------------------------------------
// Threads, just enough for the stress test

typedef struct CPTThread
{
    bool started;
    int (*fn)(void*);
    void* arg;
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
} CPTThread;

#ifdef _WIN32
static DWORD WINAPI cpt_thread_entry(LPVOID arg)
{
    CPTThread* t = (CPTThread*)arg;
    t->fn(t->arg);
    return 0;
}
#else
static void* cpt_thread_entry(void* arg)
{
    CPTThread* t = (CPTThread*)arg;
    t->fn(t->arg);
    return NULL;
}
#endif

static void cpt_thread_start(CPTThread* t, int (*fn)(void*), void* arg)
{
    t->fn      = fn;
    t->arg     = arg;
    t->started = true;
#ifdef _WIN32
    t->handle = CreateThread(NULL, 0, cpt_thread_entry, t, 0, NULL);
#else
    pthread_create(&t->handle, NULL, cpt_thread_entry, t);
#endif
}

static void cpt_thread_join(CPTThread* t)
{
    if (!t->started)
        return;
#ifdef _WIN32
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
#else
    pthread_join(t->handle, NULL);
#endif
    t->started = false;
}

static void cpt_yield(void)
{
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

// ---------------------------------------------------------------------------------------------------------------------

#define CPT_ASSERT(cond)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "FAILED: %s\n    in %s() at %s:%d\n\n", #cond, __func__, __FILE__, __LINE__);              \
            return 1;                                                                                                  \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

#define CPT_RUN(func)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (func() != 0)                                                                                               \
            return 1;                                                                                                  \
        fprintf(stderr, "ok %s\n", #func);                                                                             \
    }                                                                                                                  \
    while (0)

// 300 isn't a multiple of 64, so the last bitset word is partly used
enum
{
    CPT_NUM_PARAMS = 300,
};

// =====================================================================================================================
// Part 1: single-threaded rules

static double cpt_defaults[CPT_NUM_PARAMS];

static CParams* cpt_make_params(void)
{
    CParams* p = calloc(1, sizeof(CParams));
    for (int i = 0; i < CPT_NUM_PARAMS; i++)
        cpt_defaults[i] = i * 0.5;
    cparams__init(p, CPT_NUM_PARAMS, cpt_defaults);
    return p;
}

// Runs one host pump and copies its events out
static int cpt_pump_host(CParams* p, CParams__HostEvent* out, int cap)
{
    int n = 0;
    cparams__host_begin(p);
    CParams__HostEvent ev;
    while (true)
    {
        const bool has_event = cparams__host_next(p, &ev);
        if (!has_event)
            break;
        if (n < cap)
            out[n] = ev;
        n++;
    }
    return n;
}

static int cpt_count_audio_changes(CParams* p)
{
    int      n = 0;
    uint32_t idx;
    while (cparams_audio_events_next(p, &idx))
        n++;
    return n;
}

static int cpt_count_main_changes(CParams* p)
{
    int      n = 0;
    uint32_t idx;
    cparams_main_events_begin(p, NULL);
    while (cparams_main_events_next(p, &idx))
        n++;
    return n;
}

static int cpt_test_init(void)
{
    CParams* p = cpt_make_params();
    for (int i = 0; i < CPT_NUM_PARAMS; i++)
    {
        CPT_ASSERT(cparams_get(p, i) == cpt_defaults[i]);
        CPT_ASSERT(cparams_audio_get(p, i) == cpt_defaults[i]);
    }
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    CPT_ASSERT(cpt_count_main_changes(p) == 0);
    CPT_ASSERT(!cparams__host_pending(p));
    free(p);
    return 0;
}

static int cpt_test_gui_one_shot(void)
{
    CParams* p = cpt_make_params();
    cparams_gui_set(p, 299, 1.5);
    CPT_ASSERT(cparams_get(p, 299) == 1.5);
    CPT_ASSERT(cparams_audio_get(p, 299) == cpt_defaults[299]); // Not until the next block

    uint32_t idx = 0;
    cparams_audio_events_begin(p);
    CPT_ASSERT(cparams_audio_events_next(p, &idx) && idx == 299);
    CPT_ASSERT(cparams_audio_get(p, 299) == 1.5);
    CPT_ASSERT(!cparams_audio_events_next(p, &idx));
    cparams_audio_end(p);

    cparams_main_events_begin(p, NULL);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 299);
    CPT_ASSERT(!cparams_main_events_next(p, &idx));

    // A one-off edit gets its own begin and end
    CPT_ASSERT(cparams__host_pending(p));
    CParams__HostEvent ev[8];
    int                n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 3);
    CPT_ASSERT(ev[0].type == CPARAMS__GESTURE_BEGIN && ev[0].idx == 299);
    CPT_ASSERT(ev[1].type == CPARAMS__VALUE && ev[1].idx == 299 && ev[1].value == 1.5);
    CPT_ASSERT(ev[2].type == CPARAMS__GESTURE_END && ev[2].idx == 299);
    CPT_ASSERT(!cparams__host_pending(p));
    CPT_ASSERT(cpt_pump_host(p, ev, 8) == 0);
    free(p);
    return 0;
}

static int cpt_test_gui_gesture_coalesces(void)
{
    CParams*           p = cpt_make_params();
    CParams__HostEvent ev[8];

    cparams_gui_begin(p, 2);
    cparams_gui_set(p, 2, 10);
    cparams_gui_set(p, 2, 11);
    int n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 2);
    CPT_ASSERT(ev[0].type == CPARAMS__GESTURE_BEGIN && ev[0].idx == 2);
    CPT_ASSERT(ev[1].type == CPARAMS__VALUE && ev[1].value == 11); // Only the newest

    cparams_gui_set(p, 2, 12);
    n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 1);
    CPT_ASSERT(ev[0].type == CPARAMS__VALUE && ev[0].value == 12);

    cparams_gui_end(p, 2);
    n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 1);
    CPT_ASSERT(ev[0].type == CPARAMS__GESTURE_END && ev[0].idx == 2);

    // Two whole clicks between pumps merge into one begin, value, end
    cparams_gui_set(p, 5, 1);
    cparams_gui_set(p, 5, 2);
    n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 3);
    CPT_ASSERT(ev[0].type == CPARAMS__GESTURE_BEGIN);
    CPT_ASSERT(ev[1].type == CPARAMS__VALUE && ev[1].value == 2);
    CPT_ASSERT(ev[2].type == CPARAMS__GESTURE_END);
    free(p);
    return 0;
}

static int cpt_test_end_held_while_new_gesture(void)
{
    // Gesture 1 ends and gesture 2 begins between the pump's capture and its events. The host must see one open
    // gesture that ends when gesture 2 ends
    CParams*           p = cpt_make_params();
    CParams__HostEvent ev[8];

    cparams_gui_begin(p, 7);
    cparams_gui_set(p, 7, 1);
    cparams_gui_end(p, 7);
    cparams__host_begin(p); // Captures begin, value, end
    cparams_gui_begin(p, 7);

    int n = 0;
    while (cparams__host_next(p, &ev[n]))
        n++;
    CPT_ASSERT(n == 2);
    CPT_ASSERT(ev[0].type == CPARAMS__GESTURE_BEGIN);
    CPT_ASSERT(ev[1].type == CPARAMS__VALUE);

    cparams_gui_set(p, 7, 2);
    cparams_gui_end(p, 7);
    n = cpt_pump_host(p, ev, 8);
    CPT_ASSERT(n == 2); // No second begin: the host's gesture is still open
    CPT_ASSERT(ev[0].type == CPARAMS__VALUE && ev[0].value == 2);
    CPT_ASSERT(ev[1].type == CPARAMS__GESTURE_END);
    CPT_ASSERT(cpt_pump_host(p, ev, 8) == 0);
    free(p);
    return 0;
}

static int cpt_test_unchanged_values_are_skipped(void)
{
    CParams* p = cpt_make_params();
    cparams_gui_set(p, 3, cpt_defaults[3]);
    CPT_ASSERT(!cparams__host_pending(p));
    bool ok = cparams_host_set(p, 3, cpt_defaults[3]);
    CPT_ASSERT(ok);
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    CPT_ASSERT(cpt_count_main_changes(p) == 0);
    free(p);
    return 0;
}

static int cpt_test_host_set(void)
{
    CParams* p = cpt_make_params();

    bool ok = cparams_host_set(p, 4, 100);
    CPT_ASSERT(ok);
    CPT_ASSERT(cparams_get(p, 4) == 100);
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 1);
    CPT_ASSERT(cparams_audio_get(p, 4) == 100);
    cparams_audio_end(p);
    CPT_ASSERT(cpt_count_main_changes(p) == 1);
    CPT_ASSERT(!cparams__host_pending(p)); // Never echoed back to the host

    // Ignored while the GUI is editing it: the GUI wins
    cparams_gui_begin(p, 4);
    CPT_ASSERT(cparams__gui_is_editing(p, 4));
    ok = cparams_host_set(p, 4, 200);
    CPT_ASSERT(!ok);
    CPT_ASSERT(cparams_get(p, 4) == 100);
    cparams_gui_end(p, 4);
    CPT_ASSERT(!cparams__gui_is_editing(p, 4));
    ok = cparams_host_set(p, 4, 200);
    CPT_ASSERT(ok);
    CPT_ASSERT(cparams_get(p, 4) == 200);
    free(p);
    return 0;
}

static int cpt_test_audio_host_event_write_back(void)
{
    CParams* p = cpt_make_params();

    cparams_audio_events_begin(p);
    bool ok = cparams__audio_host_event(p, 1, 5);
    CPT_ASSERT(ok);
    CPT_ASSERT(cparams_audio_get(p, 1) == 5);
    CPT_ASSERT(cparams_get(p, 1) == cpt_defaults[1]); // Not until the block ends
    cparams_audio_end(p);
    CPT_ASSERT(cparams_get(p, 1) == 5);
    CPT_ASSERT(cpt_count_main_changes(p) == 1);
    CPT_ASSERT(!cparams__host_pending(p));

    // The audio thread already has it, so it isn't reported to audio again
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    cparams_audio_end(p);

    // The same value every block writes nothing
    cparams_audio_events_begin(p);
    ok = cparams__audio_host_event(p, 1, 5);
    CPT_ASSERT(ok);
    cparams_audio_end(p);
    CPT_ASSERT(cpt_count_main_changes(p) == 0);

    // Ignored during a GUI gesture
    cparams_gui_begin(p, 1);
    cparams_audio_events_begin(p);
    ok = cparams__audio_host_event(p, 1, 6);
    CPT_ASSERT(!ok);
    CPT_ASSERT(cparams_audio_get(p, 1) == 5);
    cparams_audio_end(p);
    CPT_ASSERT(cparams_get(p, 1) == 5);
    cparams_gui_end(p, 1);
    free(p);
    return 0;
}

static int cpt_test_gui_wins_within_block(void)
{
    // The GUI writes while the audio thread is applying automation to the same parameter. The block-end write-back
    // must not replace the GUI's value
    CParams* p = cpt_make_params();

    cparams_audio_events_begin(p);
    bool ok = cparams__audio_host_event(p, 1, 5);
    CPT_ASSERT(ok);
    cparams_gui_set(p, 1, 7); // "Another thread", mid-block
    cparams_audio_end(p);
    CPT_ASSERT(cparams_get(p, 1) == 7);

    uint32_t idx = 0;
    cparams_audio_events_begin(p);
    CPT_ASSERT(cparams_audio_events_next(p, &idx) && idx == 1);
    CPT_ASSERT(cparams_audio_get(p, 1) == 7);
    cparams_audio_end(p);
    free(p);
    return 0;
}

static int cpt_test_audio_set(void)
{
    CParams* p = cpt_make_params();
    cparams_audio_events_begin(p);
    cparams_audio_set(p, 8, 42);
    CPT_ASSERT(cparams_audio_get(p, 8) == 42);
    CPT_ASSERT(cparams_get(p, 8) == 42);
    cparams_audio_end(p);

    CPT_ASSERT(cpt_count_main_changes(p) == 1);
    CParams__HostEvent ev[4];
    int                n = cpt_pump_host(p, ev, 4);
    CPT_ASSERT(n == 1);
    CPT_ASSERT(ev[0].type == CPARAMS__VALUE && ev[0].idx == 8 && ev[0].value == 42);
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    free(p);
    return 0;
}

static int cpt_test_load(void)
{
    CParams* p = cpt_make_params();

    cparams_load_begin(p);
    for (int i = 0; i < CPT_NUM_PARAMS; i++)
        cparams_load_set(p, i, 9);
    CPT_ASSERT(cparams_get(p, 0) == 9);

    // While a load is in progress the audio thread keeps the old snapshot
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    CPT_ASSERT(cparams_audio_get(p, 0) == cpt_defaults[0]);
    cparams_audio_end(p);

    // Loads may overlap. Nothing is pulled until both end
    cparams_load_begin(p);
    cparams_load_end(p);
    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == 0);
    cparams_audio_end(p);

    cparams_load_end(p);
    bool rescan = cparams__take_rescan(p);
    CPT_ASSERT(rescan);
    rescan = cparams__take_rescan(p);
    CPT_ASSERT(!rescan);

    cparams_audio_events_begin(p);
    CPT_ASSERT(cpt_count_audio_changes(p) == CPT_NUM_PARAMS);
    for (int i = 0; i < CPT_NUM_PARAMS; i++)
        CPT_ASSERT(cparams_audio_get(p, i) == 9);
    cparams_audio_end(p);
    CPT_ASSERT(cpt_count_main_changes(p) == CPT_NUM_PARAMS);
    CPT_ASSERT(!cparams__host_pending(p)); // The host is told through the rescan, not per value
    free(p);
    return 0;
}

static int cpt_test_load_beats_automation(void)
{
    // A load writes a parameter the host is automating in the same block. The load's value must win
    CParams* p = cpt_make_params();
    cparams_audio_events_begin(p);
    bool ok = cparams__audio_host_event(p, 0, 5);
    CPT_ASSERT(ok);
    cparams_load_begin(p);
    cparams_load_set(p, 0, 9);
    cparams_load_end(p);
    cparams_audio_end(p);
    CPT_ASSERT(cparams_get(p, 0) == 9);
    cparams_audio_events_begin(p);
    CPT_ASSERT(cparams_audio_get(p, 0) == 9);
    free(p);
    return 0;
}

static int cpt_test_unconsumed_changes_are_kept(void)
{
    CParams* p = cpt_make_params();
    cparams_gui_set(p, 1, 1);
    cparams_gui_set(p, 2, 2);
    cparams_audio_events_begin(p);
    uint32_t idx = 0;
    CPT_ASSERT(cparams_audio_events_next(p, &idx) && idx == 1);
    cparams_audio_end(p);

    cparams_audio_events_begin(p);
    CPT_ASSERT(cparams_audio_events_next(p, &idx) && idx == 2);
    CPT_ASSERT(!cparams_audio_events_next(p, &idx));
    free(p);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// CPLUG helpers, against a fake plugin and a fake host

enum
{
    CPT_FAKE_NUM_PARAMS = 200,
};

static int cpt_fake_plugin; // Its address is the fake plugin's pointer

// IDs count down, so the ID -> index table really has to be sorted
static uint32_t cpt_fake_id(uint32_t idx) { return 5000 - idx * 3; }

CPLUG_API uint32_t cplug_getNumParameters(void* ptr)
{
    (void)ptr;
    return CPT_FAKE_NUM_PARAMS;
}
CPLUG_API uint32_t cplug_getParameterID(void* ptr, uint32_t idx)
{
    (void)ptr;
    return cpt_fake_id(idx);
}
CPLUG_API double cplug_getDefaultParameterValue(void* ptr, uint32_t id)
{
    (void)ptr;
    return id * 0.25;
}

static CplugEvent cpt_sent[16];
static int        cpt_num_sent;
static uint32_t   cpt_rescan_flags;
static int        cpt_num_rescans;

static void cpt_host_send(CplugHostContext* ctx, const CplugEvent* event)
{
    (void)ctx;
    if (cpt_num_sent < 16)
        cpt_sent[cpt_num_sent] = *event;
    cpt_num_sent++;
}

static void cpt_host_rescan(CplugHostContext* ctx, uint32_t flags)
{
    (void)ctx;
    cpt_rescan_flags |= flags;
    cpt_num_rescans++;
}

static CplugHostContext cpt_make_host(void)
{
    CplugHostContext host;
    memset(&host, 0, sizeof(host));
    host.type           = CPLUG_PLUGIN_IS_VST3;
    host.sendParamEvent = cpt_host_send;
    host.rescan         = cpt_host_rescan;
    cpt_num_sent        = 0;
    cpt_rescan_flags    = 0;
    cpt_num_rescans     = 0;
    return host;
}

static int cpt_test_cplug_init(void)
{
    CParams* p = calloc(1, sizeof(CParams));
    cparams_init_cplug(p, &cpt_fake_plugin);
    CPT_ASSERT(p->num_params == CPT_FAKE_NUM_PARAMS);
    for (uint32_t i = 0; i < CPT_FAKE_NUM_PARAMS; i++)
    {
        const uint32_t id    = cpt_fake_id(i);
        uint32_t       idx   = 0;
        const bool     known = cparams_index_of(p, id, &idx);
        CPT_ASSERT(known && idx == i);
        CPT_ASSERT(cparams_id_of(p, i) == id);
        CPT_ASSERT(cparams_get(p, i) == id * 0.25);
        CPT_ASSERT(cparams_audio_get(p, i) == id * 0.25);
    }

    // Unknown IDs: between two real ones, below the lowest, above the highest
    uint32_t idx = 0;
    CPT_ASSERT(!cparams_index_of(p, 4999, &idx));
    CPT_ASSERT(!cparams_index_of(p, 0, &idx));
    CPT_ASSERT(!cparams_index_of(p, 5001, &idx));

    // Plain cparams__init: IDs are indices
    cparams__init(p, 10, NULL);
    for (uint32_t i = 0; i < 10; i++)
    {
        const bool known = cparams_index_of(p, i, &idx);
        CPT_ASSERT(known && idx == i);
        CPT_ASSERT(cparams_id_of(p, i) == i);
    }
    CPT_ASSERT(!cparams_index_of(p, 10, &idx));
    free(p);
    return 0;
}

static int cpt_test_cplug_event(void)
{
    CParams* p = calloc(1, sizeof(CParams));
    cparams_init_cplug(p, &cpt_fake_plugin);

    CplugEvent event;
    uint32_t   idx = 0;
    cparams_audio_events_begin(p);

    memset(&event, 0, sizeof(event));
    event.parameter.type  = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
    event.parameter.id    = cpt_fake_id(5);
    event.parameter.value = 9;
    bool changed          = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(changed && idx == 5);
    CPT_ASSERT(cparams_audio_get(p, 5) == 9);

    // Unknown ID
    event.parameter.id = 1;
    changed            = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(!changed);

    // The GUI is editing it
    const double before_7 = cparams_audio_get(p, 7);
    cparams_gui_begin(p, 7);
    event.parameter.id = cpt_fake_id(7);
    changed            = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(!changed && cparams_audio_get(p, 7) == before_7);
    cparams_gui_end(p, 7);

    // A gesture sent to the plugin
    event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
    changed              = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(!changed);

    // Not parameter events
    memset(&event, 0, sizeof(event));
    event.type = CPLUG_EVENT_MIDI;
    changed    = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(!changed);
    event.type = CPLUG_EVENT_PROCESS_AUDIO;
    changed    = cparams_cplug_event(p, &event, &idx);
    CPT_ASSERT(!changed);

    cparams_audio_end(p);
    CPT_ASSERT(cparams_get(p, 5) == 9);
    free(p);
    return 0;
}

static int cpt_test_main_events_host(void)
{
    CParams* p = calloc(1, sizeof(CParams));
    cparams_init_cplug(p, &cpt_fake_plugin);
    CplugHostContext host = cpt_make_host();

    uint32_t idx = 0;

    // A one-off GUI edit reaches the host as begin, update, end, by ID. The same call reports it to main
    cparams_gui_set(p, 3, 1.5);
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 3);
    CPT_ASSERT(!cparams_main_events_next(p, &idx));
    CPT_ASSERT(cpt_num_sent == 3);
    CPT_ASSERT(cpt_sent[0].parameter.type == CPLUG_EVENT_PARAM_CHANGE_BEGIN);
    CPT_ASSERT(cpt_sent[1].parameter.type == CPLUG_EVENT_PARAM_CHANGE_UPDATE);
    CPT_ASSERT(cpt_sent[1].parameter.value == 1.5);
    CPT_ASSERT(cpt_sent[2].parameter.type == CPLUG_EVENT_PARAM_CHANGE_END);
    for (int i = 0; i < 3; i++)
        CPT_ASSERT(cpt_sent[i].parameter.id == cpt_fake_id(3));
    CPT_ASSERT(cpt_num_rescans == 0);

    // Host writes are never sent back
    host = cpt_make_host();
    cparams_host_set(p, 4, 2.5);
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cpt_num_sent == 0);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 4);

    // A load becomes one rescan, and only once
    cparams_load_begin(p);
    cparams_load_set(p, 0, 7);
    cparams_load_end(p);
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cpt_num_sent == 0);
    CPT_ASSERT(cpt_num_rescans == 1 && cpt_rescan_flags == CPLUG_FLAG_RESCAN_PARAM_VALUES);
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cpt_num_rescans == 1);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 0);

    // A host without sendParamEvent or rescan
    host                = cpt_make_host();
    host.sendParamEvent = NULL;
    host.rescan         = NULL;
    cparams_gui_set(p, 3, 2);
    cparams_load_begin(p);
    cparams_load_end(p);
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cpt_num_sent == 0 && cpt_num_rescans == 0);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 3);

    // No host context: changes still reach main, and what the host would get stays pending until there is one
    host = cpt_make_host();
    cparams_gui_set(p, 5, 3);
    cparams_load_begin(p);
    cparams_load_end(p);
    cparams_main_events_begin(p, NULL);
    CPT_ASSERT(cparams_main_events_next(p, &idx) && idx == 5);
    CPT_ASSERT(cparams__host_pending(p));
    cparams_main_events_begin(p, &host);
    CPT_ASSERT(cpt_num_rescans == 1);
    CPT_ASSERT(!cparams__host_pending(p));
    free(p);
    return 0;
}

// =====================================================================================================================
// Part 2: threads

enum
{
    CPT_W_DEFAULT,
    CPT_W_GUI,
    CPT_W_HOST_0,
    CPT_W_HOST_1,
    CPT_W_LOADER,
    CPT_W_AUDIO_HOST,
    CPT_W_AUDIO_SET,
    CPT_NUM_WRITERS,
};

typedef struct CPTStressConfig
{
    const char* name;
    int         gui_ops;            // GUI one-shots and gestures
    bool        host_writers;       // two threads calling cparams_host_set
    bool        loader;             // a thread running back-to-back loads
    bool        audio_host_events;  // automation on the audio thread
    bool        audio_set;          // the DSP sets its own params
    bool        host_pump_on_audio; // CLAP style. Otherwise on main, VST3/AU/AAX style
    bool        check_torn_presets;
    bool        check_gui_wins;
    bool        check_host_view;
    bool        check_lost_updates; // main writes twice, then checks audio gets the second value
    bool        loader_skips_hot;   // loads leave params [0, hot_params) alone
    bool        loader_gaps;        // leave a few audio blocks between loads. Otherwise loads run back to back, which
                                    // makes a load starting mid-drain common, but starves audio of all changes
    uint32_t hot_params;            // writers pick from params [0, hot_params), to make conflicts common
} CPTStressConfig;

typedef struct CPTStress
{
    const CPTStressConfig* cfg;
    CParams*               p;
    uint64_t               published[CPT_NUM_WRITERS]; // highest sequence number each writer has started writing
    uint32_t               stop;
    uint64_t               audio_blocks;
    uint64_t audio_seen[CPT_NUM_PARAMS]; // the audio snapshot, published after each block (hot params only)
    uint32_t failures;
    uint32_t first_failure_taken;
    char     first_failure[512];

    // Owned by whichever thread runs the host pump
    bool   host_open[CPT_NUM_PARAMS];
    double host_view[CPT_NUM_PARAMS];
    // Owned by the main (GUI) thread
    double main_view[CPT_NUM_PARAMS];
} CPTStress;

static void cpt_stress_fail(CPTStress* s, const char* what, uint32_t idx, double v, int line)
{
    cparams__fetch_add32(&s->failures, 1);
    const uint32_t was_taken = cparams__exchange32(&s->first_failure_taken, 1);
    if (!was_taken)
    {
        snprintf(
            s->first_failure,
            sizeof(s->first_failure),
            "%s: %s (param %u, value %.17g) at line %d",
            s->cfg->name,
            what,
            idx,
            v,
            line);
    }
}

#define CPT_CHECK(s, cond, idx, v)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
            cpt_stress_fail((s), #cond, (idx), (v), __LINE__);                                                         \
    }                                                                                                                  \
    while (0)

static uint32_t cpt_rng_next(uint32_t* state)
{
    uint32_t x  = *state;
    x          ^= x << 13;
    x          ^= x >> 17;
    x          ^= x << 5;
    *state      = x;
    return x;
}

// Every value is a whole number below 2^53, so it is exact as a double
static double cpt_encode(uint32_t writer, uint32_t idx, uint32_t seq)
{
    const uint64_t u = ((uint64_t)writer << 48) | ((uint64_t)idx << 32) | seq;
    return (double)u;
}

static uint32_t cpt_decode_writer(double v) { return (uint32_t)((uint64_t)v >> 48); }
static uint32_t cpt_decode_seq(double v) { return (uint32_t)((uint64_t)v & 0xffffffff); }

static bool cpt_is_valid(CPTStress* s, uint32_t idx, double v)
{
    const uint64_t u = (uint64_t)v;
    if ((double)u != v)
        return false;
    const uint32_t writer = (uint32_t)(u >> 48);
    const uint32_t pidx   = (uint32_t)((u >> 32) & 0xffff);
    const uint32_t seq    = (uint32_t)(u & 0xffffffff);
    if (writer >= CPT_NUM_WRITERS || pidx != idx)
        return false;
    // Writers publish the sequence number before writing it, so this is read after the value
    const uint64_t published = cparams__load64(&s->published[writer]);
    return seq <= published;
}

static uint32_t cpt_next_seq(CPTStress* s, uint32_t writer)
{
    const uint64_t seq = cparams__fetch_add64(&s->published[writer], 1) + 1;
    return (uint32_t)seq;
}

static uint32_t cpt_pick_param(CPTStress* s, uint32_t* rng)
{
    const uint32_t r = cpt_rng_next(rng);
    return r % s->cfg->hot_params;
}

static void cpt_run_host_pump(CPTStress* s)
{
    CParams* p = s->p;
    cparams__host_begin(p);
    CParams__HostEvent ev;
    while (true)
    {
        const bool has_event = cparams__host_next(p, &ev);
        if (!has_event)
            break;
        switch (ev.type)
        {
        case CPARAMS__GESTURE_BEGIN:
            CPT_CHECK(s, !s->host_open[ev.idx], ev.idx, 0);
            s->host_open[ev.idx] = true;
            break;
        case CPARAMS__VALUE:
        {
            const bool valid = cpt_is_valid(s, ev.idx, ev.value);
            CPT_CHECK(s, valid, ev.idx, ev.value);
            s->host_view[ev.idx] = ev.value;
            break;
        }
        case CPARAMS__GESTURE_END:
            CPT_CHECK(s, s->host_open[ev.idx], ev.idx, 0);
            s->host_open[ev.idx] = false;
            break;
        }
    }
}

static void cpt_run_main_pump(CPTStress* s)
{
    CParams* p = s->p;
    uint32_t idx;
    cparams_main_events_begin(p, NULL);
    while (cparams_main_events_next(p, &idx))
    {
        const double v     = cparams_get(p, idx);
        const bool   valid = cpt_is_valid(s, idx, v);
        CPT_CHECK(s, valid, idx, v);
        s->main_view[idx] = v;
    }
    const bool rescan = cparams__take_rescan(p);
    (void)rescan; // A real plugin would ask the host to re-read every value here
    if (!s->cfg->host_pump_on_audio)
        cpt_run_host_pump(s);
}

// Waits until the audio thread has finished at least two whole blocks since now, or the test is stopping (the audio
// thread may already have exited)
static void cpt_wait_audio_blocks(CPTStress* s)
{
    const uint64_t start = cparams__load64(&s->audio_blocks);
    while (true)
    {
        const uint64_t now  = cparams__load64(&s->audio_blocks);
        const uint32_t stop = cparams__load32(&s->stop);
        if (now >= start + 3 || stop)
            break;
        cpt_yield();
    }
}

static int cpt_thread_gui(void* user)
{
    CPTStress* s   = user;
    CParams*   p   = s->p;
    uint32_t   rng = 0x12345678;

    for (int op = 0; op < s->cfg->gui_ops; op++)
    {
        const uint32_t idx    = cpt_pick_param(s, &rng);
        const uint32_t action = cpt_rng_next(&rng) % 10;
        if (action < 4)
        {
            // One-off edit
            const uint32_t seq = cpt_next_seq(s, CPT_W_GUI);
            cparams_gui_set(p, idx, cpt_encode(CPT_W_GUI, idx, seq));
        }
        else
        {
            // A drag of a few steps, pumping in between like a GUI frame would
            cparams_gui_begin(p, idx);
            const uint32_t steps = 1 + cpt_rng_next(&rng) % 8;
            for (uint32_t i = 0; i < steps; i++)
            {
                const uint32_t seq = cpt_next_seq(s, CPT_W_GUI);
                const double   v   = cpt_encode(CPT_W_GUI, idx, seq);
                cparams_gui_set(p, idx, v);
                if (s->cfg->check_gui_wins)
                {
                    // The audio thread is automating this param too. Its events must be ignored, and its block-end
                    // write-backs must not replace this value
                    cpt_wait_audio_blocks(s);
                    const double now = cparams_get(p, idx);
                    CPT_CHECK(s, now == v, idx, now);
                }
                const bool pump = (cpt_rng_next(&rng) & 3) == 0;
                if (pump)
                    cpt_run_main_pump(s);
            }
            cparams_gui_end(p, idx);
        }

        if ((op & 15) == 0)
            cpt_run_main_pump(s);
        if ((op & 255) == 0)
            cpt_yield();
    }
    return 0;
}

static int cpt_thread_host_writer(void* user)
{
    CPTStress* s = user;
    CParams*   p = s->p;
    // Which of the two host writers this is: the first one to claim CPT_W_HOST_0 gets it
    static uint32_t next_writer = 0;
    const uint32_t  writer      = CPT_W_HOST_0 + (cparams__fetch_add32(&next_writer, 1) & 1);
    uint32_t        rng         = 0x9e3779b9u * writer;

    while (true)
    {
        const uint32_t stop = cparams__load32(&s->stop);
        if (stop)
            break;
        const uint32_t idx = cpt_pick_param(s, &rng);
        const uint32_t seq = cpt_next_seq(s, writer);
        cparams_host_set(p, idx, cpt_encode(writer, idx, seq));

        // Host getters run on these threads too
        const uint32_t other = cpt_rng_next(&rng) % CPT_NUM_PARAMS;
        const double   v     = cparams_get(p, other);
        const bool     valid = cpt_is_valid(s, other, v);
        CPT_CHECK(s, valid, other, v);

        if ((seq & 63) == 0)
            cpt_yield();
    }
    return 0;
}

static int cpt_thread_loader(void* user)
{
    CPTStress* s = user;
    CParams*   p = s->p;
    while (true)
    {
        const uint32_t stop = cparams__load32(&s->stop);
        if (stop)
            break;
        const uint32_t preset = cpt_next_seq(s, CPT_W_LOADER);
        cparams_load_begin(p);
        const uint32_t first = s->cfg->loader_skips_hot ? s->cfg->hot_params : 0;
        for (uint32_t i = first; i < CPT_NUM_PARAMS; i++)
        {
            cparams_load_set(p, i, cpt_encode(CPT_W_LOADER, i, preset));
            if ((i & 31) == 0)
                cpt_yield(); // Stretch the load out, so audio blocks land inside it
        }
        cparams_load_end(p);
        if (s->cfg->loader_gaps)
            cpt_wait_audio_blocks(s);
        else
            cpt_yield();
    }
    return 0;
}

static int cpt_thread_audio(void* user)
{
    CPTStress* s   = user;
    CParams*   p   = s->p;
    uint32_t   rng = 0xdeadbeef;

    for (uint64_t block = 0;; block++)
    {
        const uint32_t stop = cparams__load32(&s->stop);
        if (stop)
            break;

        cparams_audio_events_begin(p);
        {
            uint32_t idx;
            while (cparams_audio_events_next(p, &idx))
            {
                const double v     = cparams_audio_get(p, idx);
                const bool   valid = cpt_is_valid(s, idx, v);
                CPT_CHECK(s, valid, idx, v);
            }
        }

        if (s->cfg->check_torn_presets)
        {
            // Every param must come from the same load, or all still be defaults
            const double   first        = cparams_audio_get(p, 0);
            const uint32_t first_writer = cpt_decode_writer(first);
            const uint32_t first_seq    = cpt_decode_seq(first);
            for (uint32_t i = 1; i < CPT_NUM_PARAMS; i++)
            {
                const double   v      = cparams_audio_get(p, i);
                const uint32_t writer = cpt_decode_writer(v);
                const uint32_t seq    = cpt_decode_seq(v);
                const bool     same   = writer == first_writer && seq == first_seq;
                CPT_CHECK(s, same, i, v);
                if (!same)
                    break;
            }
        }

        if (s->cfg->audio_host_events)
        {
            const uint32_t num_events = cpt_rng_next(&rng) % 4;
            for (uint32_t e = 0; e < num_events; e++)
            {
                const uint32_t idx      = cpt_pick_param(s, &rng);
                const uint32_t seq      = cpt_next_seq(s, CPT_W_AUDIO_HOST);
                const double   v        = cpt_encode(CPT_W_AUDIO_HOST, idx, seq);
                const bool     accepted = cparams__audio_host_event(p, idx, v);
                if (accepted)
                {
                    const double now = cparams_audio_get(p, idx);
                    CPT_CHECK(s, now == v, idx, now);
                }
            }
        }

        if (s->cfg->audio_set && (cpt_rng_next(&rng) & 15) == 0)
        {
            const uint32_t idx = cpt_pick_param(s, &rng);
            const uint32_t seq = cpt_next_seq(s, CPT_W_AUDIO_SET);
            cparams_audio_set(p, idx, cpt_encode(CPT_W_AUDIO_SET, idx, seq));
        }

        if (s->cfg->host_pump_on_audio)
            cpt_run_host_pump(s);

        cparams_audio_end(p);
        if (s->cfg->check_lost_updates)
        {
            for (uint32_t i = 0; i < s->cfg->hot_params; i++)
            {
                const uint64_t seen = (uint64_t)cparams_audio_get(p, i);
                cparams__store64(&s->audio_seen[i], seen);
            }
        }
        cparams__fetch_add64(&s->audio_blocks, 1);

        if ((block & 63) == 0)
        {
            for (uint32_t i = 0; i < CPT_NUM_PARAMS; i++)
            {
                const double v     = cparams_audio_get(p, i);
                const bool   valid = cpt_is_valid(s, i, v);
                CPT_CHECK(s, valid, i, v);
            }
        }
    }
    return 0;
}

static int cpt_run_stress(const CPTStressConfig* cfg)
{
    CPTStress* s = calloc(1, sizeof(CPTStress));
    s->cfg       = cfg;
    s->p         = calloc(1, sizeof(CParams));

    double defaults[CPT_NUM_PARAMS];
    for (uint32_t i = 0; i < CPT_NUM_PARAMS; i++)
    {
        defaults[i]     = cpt_encode(CPT_W_DEFAULT, i, 0);
        s->host_view[i] = defaults[i];
        s->main_view[i] = defaults[i];
    }
    cparams__init(s->p, CPT_NUM_PARAMS, defaults);

    // Threads
    {
        CPTThread audio  = {0};
        CPTThread host_a = {0};
        CPTThread host_b = {0};
        CPTThread loader = {0};
        cpt_thread_start(&audio, cpt_thread_audio, s);
        if (cfg->host_writers)
        {
            cpt_thread_start(&host_a, cpt_thread_host_writer, s);
            cpt_thread_start(&host_b, cpt_thread_host_writer, s);
        }
        if (cfg->loader)
            cpt_thread_start(&loader, cpt_thread_loader, s);

        // This thread plays main
        if (cfg->check_lost_updates)
        {
            // Two writes back to back, so the second often lands while audio is draining the first. Then nobody writes
            // that param again, so if the second write's dirty bit is lost, audio never gets it. Ways to lose it: the
            // drain reads the value before clearing the bit, or throws a drain away (a load started mid-read) without
            // giving the bits back. Loads stall drains for a while, so wait for the value rather than a block count
            uint32_t rng = 0x2545f491;
            for (int i = 0; i < 100000; i++)
            {
                const uint32_t idx = cpt_pick_param(s, &rng);
                const uint32_t a   = cpt_next_seq(s, CPT_W_HOST_0);
                cparams_host_set(s->p, idx, cpt_encode(CPT_W_HOST_0, idx, a));
                const uint32_t b = cpt_next_seq(s, CPT_W_HOST_0);
                const double   v = cpt_encode(CPT_W_HOST_0, idx, b);
                cparams_host_set(s->p, idx, v);

                const uint64_t start = cparams__load64(&s->audio_blocks);
                while (true)
                {
                    const uint64_t seen    = cparams__load64(&s->audio_seen[idx]);
                    const uint64_t blocks  = cparams__load64(&s->audio_blocks);
                    const bool     arrived = (double)seen == v;
                    const bool     timeout = blocks > start + 200000;
                    if (arrived)
                        break;
                    if (timeout)
                    {
                        CPT_CHECK(s, arrived, idx, (double)seen);
                        break;
                    }
                    cpt_yield();
                }
                if (cparams__load32(&s->failures))
                    break;
            }
        }
        else if (cfg->gui_ops)
        {
            cpt_thread_gui(s);
        }
        else
        {
            // No GUI: just keep main's consumers drained for a while
            for (int i = 0; i < 20000; i++)
            {
                cpt_run_main_pump(s);
                cpt_yield();
            }
        }

        cparams__store32(&s->stop, 1);
        cpt_thread_join(&audio);
        cpt_thread_join(&host_a);
        cpt_thread_join(&host_b);
        cpt_thread_join(&loader);
    }

    // Quiet now. One more drain per consumer, then everyone must agree with the store
    {
        CParams* p = s->p;
        cparams_audio_events_begin(p);
        uint32_t idx;
        while (cparams_audio_events_next(p, &idx))
        {
        }
        cparams_audio_end(p);
        cpt_run_main_pump(s);
        cpt_run_host_pump(s);

        for (uint32_t i = 0; i < CPT_NUM_PARAMS; i++)
        {
            const double store = cparams_get(p, i);
            const double audio = cparams_audio_get(p, i);
            CPT_CHECK(s, audio == store, i, audio);
            CPT_CHECK(s, s->main_view[i] == store, i, s->main_view[i]);
            CPT_CHECK(s, !s->host_open[i], i, 0);
            if (cfg->check_host_view)
                CPT_CHECK(s, s->host_view[i] == store, i, s->host_view[i]);
        }
    }

    const uint32_t failures = cparams__load32(&s->failures);
    const uint64_t blocks   = cparams__load64(&s->audio_blocks);
    if (failures)
    {
        fprintf(stderr, "FAILED: %u failures. First: %s\n\n", failures, s->first_failure);
    }
    else
    {
        fprintf(
            stderr,
            "ok %s (%llu audio blocks, %llu gui writes, %llu host writes, %llu loads, %llu automation events)\n",
            cfg->name,
            (unsigned long long)blocks,
            (unsigned long long)cparams__load64(&s->published[CPT_W_GUI]),
            (unsigned long long)(cparams__load64(&s->published[CPT_W_HOST_0]) +
                                 cparams__load64(&s->published[CPT_W_HOST_1])),
            (unsigned long long)cparams__load64(&s->published[CPT_W_LOADER]),
            (unsigned long long)cparams__load64(&s->published[CPT_W_AUDIO_HOST]));
    }

    free(s->p);
    free(s);
    return failures ? 1 : 0;
}

int cparams_test_run(void)
{
    fprintf(stderr, "Part 1: rules\n");
    CPT_RUN(cpt_test_init);
    CPT_RUN(cpt_test_gui_one_shot);
    CPT_RUN(cpt_test_gui_gesture_coalesces);
    CPT_RUN(cpt_test_end_held_while_new_gesture);
    CPT_RUN(cpt_test_unchanged_values_are_skipped);
    CPT_RUN(cpt_test_host_set);
    CPT_RUN(cpt_test_audio_host_event_write_back);
    CPT_RUN(cpt_test_gui_wins_within_block);
    CPT_RUN(cpt_test_audio_set);
    CPT_RUN(cpt_test_load);
    CPT_RUN(cpt_test_load_beats_automation);
    CPT_RUN(cpt_test_unconsumed_changes_are_kept);
    CPT_RUN(cpt_test_cplug_init);
    CPT_RUN(cpt_test_cplug_event);
    CPT_RUN(cpt_test_main_events_host);

    fprintf(stderr, "Part 2: threads\n");
    const CPTStressConfig configs[] = {
        {
            .name               = "everything, host pump on main",
            .gui_ops            = 200000,
            .host_writers       = true,
            .loader             = true,
            .audio_host_events  = true,
            .audio_set          = true,
            .host_pump_on_audio = false,
            .hot_params         = CPT_NUM_PARAMS,
        },
        {
            .name               = "everything, host pump on audio",
            .gui_ops            = 200000,
            .host_writers       = true,
            .loader             = true,
            .audio_host_events  = true,
            .audio_set          = true,
            .host_pump_on_audio = true,
            .hot_params         = CPT_NUM_PARAMS,
        },
        {
            .name               = "everything, 4 hot params",
            .gui_ops            = 200000,
            .host_writers       = true,
            .loader             = true,
            .audio_host_events  = true,
            .audio_set          = true,
            .host_pump_on_audio = true,
            .hot_params         = 4,
        },
        {
            .name               = "torn presets",
            .gui_ops            = 0,
            .loader             = true,
            .check_torn_presets = true,
            .hot_params         = CPT_NUM_PARAMS,
        },
        {
            .name               = "gui wins over automation",
            .gui_ops            = 3000,
            .audio_host_events  = true,
            .host_pump_on_audio = true,
            .check_gui_wins     = true,
            .hot_params         = 4,
        },
        {
            .name               = "lost updates",
            .check_lost_updates = true,
            .hot_params         = 1,
        },
        {
            .name               = "lost updates during loads",
            .check_lost_updates = true,
            .loader             = true,
            .loader_skips_hot   = true,
            .loader_gaps        = true,
            .hot_params         = 1,
        },
        {
            .name               = "host sees every gui edit, pump on audio",
            .gui_ops            = 200000,
            .host_pump_on_audio = true,
            .check_host_view    = true,
            .hot_params         = 16,
        },
    };
    for (int i = 0; i < (int)(sizeof(configs) / sizeof(configs[0])); i++)
    {
        const int result = cpt_run_stress(&configs[i]);
        if (result)
            return 1;
    }

    fprintf(stderr, "All passed\n");
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // CPLUG_PARAMETERS_TEST
