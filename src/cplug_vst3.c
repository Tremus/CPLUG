/* This file is subject to the terms of the VST3 SDK License. See here https://www.steinberg.net/sdklicenses
 * Originally authored by Filipe Coelho as a part of DPF https://github.com/DISTRHO/DPF
 * A special thanks goes to him for allowing the use of his code here.
 * Edited and ported to CPLUG by Tré Dudman */
#include <cplug.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vst3_c_api.h>
#include <wchar.h>

#define tuid_match(a, b) memcmp(a, b, sizeof(Steinberg_TUID)) == 0
#define CPLUG_ARRLEN(a)  (sizeof(a) / sizeof(a[0]))

// Parameter automtion in some hosts can be too highly frequent. eg. Ableton sends a new automation points every 8
// samples. In order to effeciently process parameter & audio events in order, parameter events are coalesced and
// quantized using the value below.
#ifndef CPLUG_EVENT_FRAME_QUANTISE
#define CPLUG_EVENT_FRAME_QUANTISE 64
#endif

// VST3 Param IDs are 32bit unsigned integers, but some DAWs such as FL Studio are suspect of misinterpreting them as
// signed integers and reject any negative integer.
enum
{
    CPLUG_MIDI_PARAMID_COUNT = (16 * Steinberg_Vst_ControllerNumbers_kCountCtrlNumber),
    CPLUG_MIDI_PARAMID_END   = 0x7fffffff,
    CPLUG_MIDI_PARAMID_START = CPLUG_MIDI_PARAMID_END - CPLUG_MIDI_PARAMID_COUNT,
};

static inline bool cplug_is_midi_param(Steinberg_Vst_ParamID id)
{
    return id >= CPLUG_MIDI_PARAMID_START && id < CPLUG_MIDI_PARAMID_END;
}

#define CALL_SMTG_INLINE_UID(args) SMTG_INLINE_UID args
static const Steinberg_TUID cplug_tuid_component  = CALL_SMTG_INLINE_UID((CPLUG_VST3_TUID_COMPONENT));
static const Steinberg_TUID cplug_tuid_controller = CALL_SMTG_INLINE_UID((CPLUG_VST3_TUID_CONTROLLER));

const char* _cplug_tuid2str(const Steinberg_TUID iid)
{
    // https://github.com/justinfrankel/reaper-sdk
    static const Steinberg_TUID IReaperUIEmbedInterface_iid =
        SMTG_INLINE_UID(0x049BF9E7, 0xBC74EAD0, 0xC4101E86, 0x7F725981);

    // https://github.com/fenderdigital/presonus-plugin-extensions
    static const Steinberg_TUID IContextInfoProvider  = SMTG_INLINE_UID(0x483e61ea, 0x17994494, 0x8199a35a, 0xebb35e3c);
    static const Steinberg_TUID IContextInfoProvider2 = SMTG_INLINE_UID(0x61e45968, 0x3d364f39, 0xb15e1733, 0x4944172b);
    static const Steinberg_TUID IContextInfoProvider3 = SMTG_INLINE_UID(0x4e31fdf8, 0x6f4448d4, 0xb4ec1461, 0x68a4150f);
    static const Steinberg_TUID IContextInfoHandler_iid =
        SMTG_INLINE_UID(0xc3b17bc0, 0x2c174494, 0x80293402, 0xfbc4bbf8);
    static const Steinberg_TUID IContextInfoHandler2_iid =
        SMTG_INLINE_UID(0x31e29a7a, 0xe55043ad, 0x8b95b9b8, 0xda1fbe1e);
    static const Steinberg_TUID IEditControllerExtra = SMTG_INLINE_UID(0x50553fd9, 0x1d2c4c24, 0xb410f484, 0xc5fb9f3f);
    static const Steinberg_TUID ISlaveControllerHandler =
        SMTG_INLINE_UID(0xd93894bd, 0x67454c29, 0x977ae2f5, 0xdb380434);
    static const Steinberg_TUID IGainReductionInfo    = SMTG_INLINE_UID(0x8e3c292c, 0x95924f9d, 0xb2590b1e, 0x100e4198);
    static const Steinberg_TUID IHostCommandHandler   = SMTG_INLINE_UID(0xF92032CD, 0x7A84407C, 0xABE6F863, 0x058EA6C2);
    static const Steinberg_TUID ICommandList          = SMTG_INLINE_UID(0xC5A687DB, 0x82F344E9, 0xB378254A, 0x47C4D712);
    static const Steinberg_TUID IInstrumentController = SMTG_INLINE_UID(0xd2ce9317, 0xf24942c9, 0x9742e82d, 0xb10ccc52);
    static const Steinberg_TUID ISoundVariationInfo   = SMTG_INLINE_UID(0xe59066c0, 0x41d940bc, 0x8f88cbb9, 0xa337e20a);
    static const Steinberg_TUID ISoundVariationController =
        SMTG_INLINE_UID(0x3abdfc3e, 0x4b964a66, 0xafcd86f10, 0x0d554023);
    static const Steinberg_TUID ISpeakerSupportInfo = SMTG_INLINE_UID(0x7342e0eb, 0x8f5641de, 0xa5f7c503, 0x8e2ec3ef);
    static const Steinberg_TUID ISpeakerSupportHostInfo =
        SMTG_INLINE_UID(0x3327e14a, 0x055e4d27, 0x9a0f6b4a, 0x36316e7b);
    static const Steinberg_TUID IPlugInViewEmbedding = SMTG_INLINE_UID(0xda57e6d1, 0x1f3242d1, 0xad9c1a82, 0xfdb95695);
    static const Steinberg_TUID IBitmapAccessor      = SMTG_INLINE_UID(0x1c4b3ab0, 0x76384cb2, 0x8adafd1b, 0xdd198055);
    static const Steinberg_TUID IPlugViewCoordinateUnitSupport =
        SMTG_INLINE_UID(0xeae3ebb, 0xb301468a, 0xa127bd34, 0x8fab0824);
    static const Steinberg_TUID IPlugViewRendering  = SMTG_INLINE_UID(0x215519ce, 0xb4de449f, 0x9572b7f2, 0x4a004a8f);
    static const Steinberg_TUID IPlugRenderingFrame = SMTG_INLINE_UID(0x68956019, 0x4b964921, 0x9c249f6a, 0xbcff47c6);
    static const Steinberg_TUID IPlugViewMouseInput = SMTG_INLINE_UID(0xc13c4ea4, 0x868e4af7, 0x9614d52c, 0x7cd07b47);
    static const Steinberg_TUID IPlugInViewScaling  = SMTG_INLINE_UID(0x65ed9690, 0x8ac44525, 0x8aadef7a, 0x72ea703f);
    static const Steinberg_TUID IPlugInViewSystemScalingSupport =
        SMTG_INLINE_UID(0xde9817bf, 0xe9684b03, 0x91b80816, 0xc2a1ca5);
    static const Steinberg_TUID IWaylandHost  = SMTG_INLINE_UID(0x5E9582EE, 0x86594652, 0xB213678E, 0x7F1A705E);
    static const Steinberg_TUID IWaylandFrame = SMTG_INLINE_UID(0x809FAEC6, 0x231C4FFA, 0x98ED046C, 0x6E9E2003);

    static const struct
    {
        const Steinberg_TUID* iid;
        const char*           name;
    } _known_iids[] = {
        // Supported
        {&cplug_tuid_component, "{cplug_tuid_component}"},
        {&cplug_tuid_controller, "{cplug_tuid_controller}"},
        {&Steinberg_Vst_IAudioProcessor_iid, "{Steinberg_Vst_IAudioProcessor_iid}"},
        {&Steinberg_Vst_IAttributeList_iid, "{Steinberg_Vst_IComponent_iid}"},
        {&Steinberg_IBStream_iid, "{Steinberg_IBStream_iid}"},
        {&Steinberg_Vst_IComponent_iid, "{Steinberg_Vst_IComponent_iid}"},
        {&Steinberg_Vst_IComponentHandler_iid, "{Steinberg_Vst_IComponentHandler_iid}"},
        {&Steinberg_Vst_IConnectionPoint_iid, "{Steinberg_Vst_IConnectionPoint_iid}"},
        {&Steinberg_Vst_IEditController_iid, "{Steinberg_Vst_IEditController_iid}"},
        {&Steinberg_Vst_IEventList_iid, "{Steinberg_Vst_IEventList_iid}"},
        {&Steinberg_FUnknown_iid, "{Steinberg_FUnknown_iid}"},
        {&Steinberg_Vst_IHostApplication_iid, "{Steinberg_Vst_IHostApplication_iid}"},
        {&Steinberg_IPluginBase_iid, "{Steinberg_IPluginBase_iid}"},
        {&Steinberg_IPluginFactory_iid, "{Steinberg_IPluginFactory_iid}"},
        {&Steinberg_IPluginFactory2_iid, "{Steinberg_IPluginFactory2_iid}"},
        {&Steinberg_IPluginFactory3_iid, "{Steinberg_IPluginFactory3_iid}"},
        {&Steinberg_IPlugView_iid, "{Steinberg_IPlugView_iid}"},
        {&Steinberg_IPlugViewContentScaleSupport_iid, "{Steinberg_IPlugViewContentScaleSupport_iid}"},
        {&Steinberg_Vst_IProcessContextRequirements_iid, "{Steinberg_Vst_IProcessContextRequirements_iid}"},
        {&Steinberg_Vst_IMidiMapping_iid, "{Steinberg_Vst_IMidiMapping_iid}"},
        {&Steinberg_IPlugFrame_iid, "{Steinberg_IPlugFrame_iid}"},
        // edit-controller
        {&Steinberg_Vst_IComponentHandler2_iid, "{Steinberg_Vst_IComponentHandler2_iid}"},
        {&Steinberg_Vst_IEditController2_iid, "{Steinberg_Vst_IEditController2_iid}"},
        {&Steinberg_Vst_IComponentHandlerBusActivation_iid, "{Steinberg_Vst_IComponentHandlerBusActivation_iid}"},
        {&Steinberg_Vst_IEditControllerHostEditing_iid, "{Steinberg_Vst_IEditControllerHostEditing_iid}"},
        {&Steinberg_Vst_INoteExpressionController_iid, "{Steinberg_Vst_INoteExpressionController_iid}"},
        {&Steinberg_Vst_IKeyswitchController_iid, "{Steinberg_Vst_IKeyswitchController_iid}"},
        {&Steinberg_Vst_IMidiLearn_iid, "{Steinberg_Vst_IMidiLearn_iid}"},
        // misc
        {&Steinberg_Vst_IProgramListData_iid, "{Steinberg_Vst_IProgramListData_iid}"},
        {&Steinberg_Vst_IUnitData_iid, "{Steinberg_Vst_IUnitData_iid}"},
        {&Steinberg_Vst_IUnitHandler_iid, "{Steinberg_Vst_IUnitHandler_iid}"},
        {&Steinberg_Vst_IUnitHandler2_iid, "{Steinberg_Vst_IUnitHandler2_iid}"},
        {&Steinberg_Vst_IUnitInfo_iid, "{Steinberg_Vst_IUnitInfo_iid}"},
        {&Steinberg_Vst_IAudioPresentationLatency_iid, "{Steinberg_Vst_IAudioPresentationLatency_iid}"},
        {&Steinberg_Vst_IAutomationState_iid, "{Steinberg_Vst_IAutomationState_iid}"},
        {&Steinberg_Vst_ChannelContext_IInfoListener_iid, "{Steinberg_Vst_ChannelContext_IInfoListener_iid}"},
        {&Steinberg_Vst_IParameterFunctionName_iid, "{Steinberg_Vst_IParameterFunctionName_iid}"},
        {&Steinberg_Vst_IPrefetchableSupport_iid, "{Steinberg_Vst_IPrefetchableSupport_iid}"},
        {&Steinberg_Vst_IXmlRepresentationController_iid, "{Steinberg_Vst_IXmlRepresentationController_iid}"},
        {&Steinberg_Vst_IMessage_iid, "{Steinberg_Vst_IMessage_iid}"},
        {&Steinberg_Vst_IParamValueQueue_iid, "{Steinberg_Vst_IParamValueQueue_iid}"},
        {&Steinberg_Vst_IParameterChanges_iid, "{Steinberg_Vst_IParameterChanges_iid}"},
        {&Steinberg_Vst_IParameterFinder_iid, "{Steinberg_Vst_IParameterFinder_iid}"},
        // 3rd party
        {&IReaperUIEmbedInterface_iid, "{IReaperUIEmbedInterface_iid}"},
        {&IContextInfoProvider, "{IContextInfoProvider}"},
        {&IContextInfoProvider2, "{IContextInfoProvider2}"},
        {&IContextInfoProvider3, "{IContextInfoProvider3}"},
        {&IContextInfoHandler_iid, "{IContextInfoHandler_iid}"},
        {&IContextInfoHandler2_iid, "{IContextInfoHandler2_iid}"},
        {&IEditControllerExtra, "{IEditControllerExtra}"},
        {&ISlaveControllerHandler, "{ISlaveControllerHandler}"},
        {&IGainReductionInfo, "{IGainReductionInfo}"},
        {&IHostCommandHandler, "{IHostCommandHandler}"},
        {&ICommandList, "{ICommandList}"},
        {&IInstrumentController, "{IInstrumentController}"},
        {&ISoundVariationInfo, "{ISoundVariationInfo}"},
        {&ISoundVariationController, "{ISoundVariationController}"},
        {&ISpeakerSupportInfo, "{ISpeakerSupportInfo}"},
        {&ISpeakerSupportHostInfo, "{ISpeakerSupportHostInfo}"},
        {&IPlugInViewEmbedding, "{IPlugInViewEmbedding}"},
        {&IBitmapAccessor, "{IBitmapAccessor}"},
        {&IPlugViewCoordinateUnitSupport, "{IPlugViewCoordinateUnitSupport}"},
        {&IPlugViewRendering, "{IPlugViewRendering}"},
        {&IPlugRenderingFrame, "{IPlugRenderingFrame}"},
        {&IPlugViewMouseInput, "{IPlugViewMouseInput}"},
        {&IPlugInViewScaling, "{IPlugInViewScaling}"},
        {&IPlugInViewSystemScalingSupport, "{IPlugInViewSystemScalingSupport}"},
        {&IWaylandHost, "{IWaylandHost}"},
        {&IWaylandFrame, "{IWaylandFrame}"}};
    // More:
    // https://github.com/justinfrankel/reaper-sdk
    // https://github.com/fenderdigital/presonus-plugin-extensions

    for (size_t i = 0; i < CPLUG_ARRLEN(_known_iids); ++i)
    {
        if (tuid_match(iid, _known_iids[i].iid))
            return _known_iids[i].name;
    }

    // Steinberg swizzle their UIDs outside of Windows. Here we unswizzle it so we can read the same IDs
    unsigned* idu32 = (unsigned*)&iid[0];
#ifndef _WIN32
    Steinberg_TUID unswizzled = SMTG_INLINE_UID(idu32[0], idu32[1], idu32[2], idu32[3]);
    idu32                     = (unsigned*)&unswizzled[0];
#endif

    static char buf[46];
    snprintf(buf, sizeof(buf), "{0x%08X,0x%08X,0x%08X,0x%08X}", idu32[0], idu32[1], idu32[2], idu32[3]);
    return buf;
}

// someone please tell me what is up with these..
static inline Steinberg_Vst_Speaker _cplug_channelCountToVST3Speaker(const uint32_t channelCount)
{
    switch (channelCount)
    {
    // regular mono
    case 1:
        return Steinberg_Vst_kSpeakerM;
    // regular stereo
    case 2:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR;
    // stereo with center channel
    case 3:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerC;
    // stereo with surround (quadro)
    case 4:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs;
    // regular 5.0
    case 5:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
               Steinberg_Vst_kSpeakerC;
    // regular 6.0
    case 6:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
               Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr;
    // regular 7.0
    case 7:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
               Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr | Steinberg_Vst_kSpeakerC;
    // regular 8.0
    case 8:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
               Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr | Steinberg_Vst_kSpeakerC | Steinberg_Vst_kSpeakerCs;
    // regular 8.1
    case 9:
        return Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
               Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr | Steinberg_Vst_kSpeakerC |
               Steinberg_Vst_kSpeakerCs | Steinberg_Vst_kSpeakerLfe;
    // cinema 10.0
    case 10:
        return (
            Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
            Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr | Steinberg_Vst_kSpeakerLc | Steinberg_Vst_kSpeakerRc |
            Steinberg_Vst_kSpeakerC | Steinberg_Vst_kSpeakerCs);
    // cinema 10.1
    case 11:
        return (
            Steinberg_Vst_kSpeakerL | Steinberg_Vst_kSpeakerR | Steinberg_Vst_kSpeakerLs | Steinberg_Vst_kSpeakerRs |
            Steinberg_Vst_kSpeakerSl | Steinberg_Vst_kSpeakerSr | Steinberg_Vst_kSpeakerLc | Steinberg_Vst_kSpeakerRc |
            Steinberg_Vst_kSpeakerC | Steinberg_Vst_kSpeakerCs | Steinberg_Vst_kSpeakerLfe);
    default:
        cplug_log("[WARNING]: _cplug_channelCountToVST3Speaker: Unsupported number of channels %u", channelCount);
        return 0;
    }
}

#ifndef NDEBUG
static inline const char* _cplug_getMediaTypeStr(int32_t type)
{
    if (type == Steinberg_Vst_MediaTypes_kAudio)
        return "MediaTypes_kAudio";
    if (type == Steinberg_Vst_MediaTypes_kEvent)
        return "MediaTypes_kEvent";
    return "[unknown]";
}
static inline const char* _cplug_getBusDirectionStr(int32_t type)
{
    if (type == Steinberg_Vst_BusDirections_kInput)
        return "BusDirections_kInput";
    if (type == Steinberg_Vst_BusDirections_kOutput)
        return "BusDirections_kOutput";
    return "[unknown]";
}
#endif

// clang-format off
// Taken from Richard Mitton & Randy Gaul (public domain)
// https://github.com/RandyGaul/cute_headers_deprecated/blob/master/cute_utf.h
const char* _cplug_decode8(const char* text, int* cp)
{
	unsigned char c = *text++;
	int extra = 0, min = 0;
	*cp = 0;
    if      (c >= 0xF0) { *cp = c & 0x07; extra = 3; min = 0x10000; }
	else if (c >= 0xE0) { *cp = c & 0x0F; extra = 2; min = 0x800; }
	else if (c >= 0xC0) { *cp = c & 0x1F; extra = 1; min = 0x80; }
	else if (c >= 0x80) { *cp = 0xFFFD; }
	else                { *cp = c; }
	while (extra--)
	{
		c = *text++;
		if ((c & 0xC0) != 0x80) { *cp = 0xFFFD; break; }
		(*cp) = ((*cp) << 6) | (c & 0x3F);
	}
	if (*cp < min) *cp = 0xFFFD;
	return text;
}
char* _cplug_encode8(char *text, int cp)
{
	if (cp < 0 || cp > 0x10FFFF) cp = 0xFFFD;

#define CU_EMIT(X, Y, Z) *text++ = X | ((cp >> Y) & Z)
    if      (cp <    0x80) { CU_EMIT(0x00,0,0x7F); }
	else if (cp <   0x800) { CU_EMIT(0xC0,6,0x1F); CU_EMIT(0x80, 0,  0x3F); }
	else if (cp < 0x10000) { CU_EMIT(0xE0,12,0xF); CU_EMIT(0x80, 6,  0x3F); CU_EMIT(0x80, 0, 0x3F); }
	else                   { CU_EMIT(0xF0,18,0x7); CU_EMIT(0x80, 12, 0x3F); CU_EMIT(0x80, 6, 0x3F); CU_EMIT(0x80, 0, 0x3F); }
#undef CU_EMIT
	return text;
}
char16_t* _cplug_encode16(char16_t* text, int cp)
{
	if (cp < 0x10000) *text++ = cp;
	else
	{
		cp -= 0x10000;
		*text++ = 0xD800 | ((cp >> 10) & 0x03FF);
		*text++ = 0xDC00 | (cp & 0x03FF);
	}
	return text;
}
const char16_t* _cplug_decode16(const char16_t* text, int* cp)
{
	int in = *text++;
	if (in < 0xD800 || in > 0xDFFF) *cp = in;
	else if (in > 0xD800 && in < 0xDBFF) *cp = ((in & 0x03FF) << 10) | (*text++ & 0x03FF);
	else *cp = 0xFFFD;
	return text;
}
void _cplug_utf8To16(char16_t* dst, const char* src, int len)
{
    char16_t* it = dst;
    int cp;
    while (*src && (it < dst + len - 1))
    {
        src = _cplug_decode8(src, &cp);
        it  = _cplug_encode16(it, cp);
    }
    *it = 0;
}
void _cplug_utf16To8(char* dst, const char16_t* src, int len)
{
	char* it = dst;
	int cp;
	while (*src && (it < dst + len - 1))
	{
		src = _cplug_decode16(src, &cp);
		it  = _cplug_encode8(it, cp);
	}
    *it = 0;
}
// clang-format on

/*----------------------------------------------------------------------------------------------------------------------
Structs */

struct VST3ProcessContextRequirements
{
    Steinberg_Vst_IProcessContextRequirementsVtbl* lpVtbl;
    Steinberg_Vst_IProcessContextRequirementsVtbl  base;
} g_vst3ProcessContext;

typedef struct VST3Factory
{
    Steinberg_IPluginFactory3Vtbl* lpVtbl;
    Steinberg_IPluginFactory3Vtbl  base;
    cplug_atomic_i32               refcounter;
    // We don't use this, but it's here in case you need it...
    Steinberg_Vst_IHostApplication* host;
} VST3Factory;

typedef struct VST3Plugin
{
    CplugHostContext hostContext;
    void*            userPlugin; // Pointer to your plugin lives here

    struct
    {
        Steinberg_Vst_IComponentVtbl* lpVtbl;
        Steinberg_Vst_IComponentVtbl  base;
        cplug_atomic_i32              refcounter;
    } component;

    struct
    {
        Steinberg_Vst_IEditControllerVtbl* lpVtbl;
        Steinberg_Vst_IEditControllerVtbl  base;
        cplug_atomic_i32                   refcounter;
        // TODO: support changing param count & other cool things
        Steinberg_Vst_IComponentHandler* componentHandler;
    } controller;

    // NOTE: You're not allowed to simply receive MIDI data
    // https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IMidiMapping.html
    struct
    {
        Steinberg_Vst_IMidiMappingVtbl* lpVtbl;
        Steinberg_Vst_IMidiMappingVtbl  base;
        cplug_atomic_i32                refcounter;
    } midiMapping;

    // At the time of writing, the only DAWs that properly support INoteExpressionController are Cubase & Bitwig.
    // Cubase are a little more othodox to their own plugin format. The noteId they send in their
    // NoteExpressionValueEvent is a full 4-byte signed integer, and the plugin is meant to use this noteId to lookup
    // the voice triggered by kNoteOnEvent, which was sent tagged with a kNoteOnEvent. Theoretically this can support
    // simultaneous note down events of the same key, but ironically Cubases own MIDI interface does not support writing
    // MIDI in this way, making the 4 byte ID redundant for plugins ie. all MIDI notes in Cubase are mapped to the key.
    // Bitwig simply sets the noteId to the MIDI note number, meaning you only need 1 byte.
    // As a quality of life feature for CPLUG, we convert noteIds to midi note numbers using 'id_to_pitch_map' below.
    // If NoteExpressionValueEvent had been designed a pitch/midi note number field, we wouldn't have to do this...
    struct
    {
        Steinberg_Vst_INoteExpressionControllerVtbl* lpVtbl;
        Steinberg_Vst_INoteExpressionControllerVtbl  base;
        cplug_atomic_i32                             refcounter;
    } noteExpression;

    struct
    {
        Steinberg_Vst_IAudioProcessorVtbl* lpVtbl;
        Steinberg_Vst_IAudioProcessorVtbl  base;
        cplug_atomic_i32                   refcounter;
    } processor;

    // We don't use this, but it's here in case you need it...
    Steinberg_Vst_IHostApplication* host;

    // Structure of arrays format. The index of the ID (key) matches the midi note (value)
    struct
    {
        size_t          size;
        Steinberg_int32 ids[128];
        uint8_t         pitch[128];
    } noteidmap;

    // Not all hosts (Ableton) pass MIDI controller events through the process callback. In Steinberg logic, MIDI
    // controller messages are parameters, and hosts will call 'setParamNormalized' to send these messages
    // NOTE: We only assume that hosts aren't doubly stupid and only send these messages on the audio thread.
    size_t   midiContollerQueueSize;
    uint32_t midiContollerQueue[CPLUG_EVENT_QUEUE_SIZE];
} VST3Plugin;

// Naughty pointer shifting for VST3 classes
static inline VST3Plugin* _cplug_pointerShiftComponent(void* const ptr)
{
    return (VST3Plugin*)((char*)(ptr)-offsetof(VST3Plugin, component));
}
static inline VST3Plugin* _cplug_pointerShiftController(void* const ptr)
{
    return (VST3Plugin*)((char*)(ptr)-offsetof(VST3Plugin, controller));
}
static inline VST3Plugin* _cplug_pointerShiftMidiMapping(void* const ptr)
{
    return (VST3Plugin*)((char*)(ptr)-offsetof(VST3Plugin, midiMapping));
}
static inline VST3Plugin* _cplug_pointerShiftNoteExpression(void* const ptr)
{
    return (VST3Plugin*)((char*)(ptr)-offsetof(VST3Plugin, noteExpression));
}
static inline VST3Plugin* _cplug_pointerShiftProcessor(void* const ptr)
{
    return (VST3Plugin*)((char*)(ptr)-offsetof(VST3Plugin, processor));
}

// General notes on syncing parameters with a host:
// If your plugin has hundreds of parameters, Ableton 10 likely won't show them in their interface.
// Instead they suggest you use their 'Configure' mode and start changing parameters within your plugins interface. The
// parameters you changed should be detected and new controls will be added to their interface. The catch here (which I
// haven't seen documented anywhere else) is that you must update your parameters using IComponentHandler which is UI
// thread only. Ableton doesn't share this problem with in their Audio Unit v2 implementation...?
// Sending parameter updates through the audio thread doesn't sync with FL Studio.
// In Reaper & Bitwig, only sending param updates over the audio thread won't produce syncing problems.
// This method is the most reliable way to send param changes and sync them with the DAW
static void _cplug_sendParamEvent(CplugHostContext* ctx, const CplugEvent* event)
{
    CPLUG_LOG_ASSERT(
        event->type == CPLUG_EVENT_PARAM_CHANGE_BEGIN || event->type == CPLUG_EVENT_PARAM_CHANGE_UPDATE ||
        event->type == CPLUG_EVENT_PARAM_CHANGE_END);
    static_assert(offsetof(VST3Plugin, hostContext) == 0, "Required offset for c-cast below");
    VST3Plugin* vst3 = (VST3Plugin*)ctx;

    Steinberg_Vst_IComponentHandler* handler = vst3->controller.componentHandler;
    CPLUG_LOG_ASSERT(handler != NULL);
    if (handler)
    {
        if (event->type == CPLUG_EVENT_PARAM_CHANGE_BEGIN)
            handler->lpVtbl->beginEdit(handler, event->parameter.id);
        else if (event->type == CPLUG_EVENT_PARAM_CHANGE_UPDATE)
        {
            double norm = cplug_normaliseParameterValue(vst3->userPlugin, event->parameter.id, event->parameter.value);
            handler->lpVtbl->performEdit(handler, event->parameter.id, norm);
        }
        else if (event->type == CPLUG_EVENT_PARAM_CHANGE_END)
            handler->lpVtbl->endEdit(handler, event->parameter.id);
    }
}

static void _cplug_tryDeleteVST3(VST3Plugin* vst3)
{
    int ref_component  = cplug_atomic_load_i32(&vst3->component.refcounter);
    int ref_controller = cplug_atomic_load_i32(&vst3->controller.refcounter);
    int ref_midimap    = cplug_atomic_load_i32(&vst3->midiMapping.refcounter);
    int ref_noteexp    = cplug_atomic_load_i32(&vst3->noteExpression.refcounter);
    int ref_processor  = cplug_atomic_load_i32(&vst3->processor.refcounter);
    cplug_log(
        "_cplug_tryDeleteVST3 %p | component: %d, controller: %d, midimapping: %d, processor: %d",
        vst3,
        ref_component,
        ref_controller,
        ref_midimap,
        ref_noteexp,
        ref_processor);

    int total = ref_component + ref_controller + ref_midimap + ref_noteexp + ref_processor;
    if (total == 0)
    {
        cplug_log("_cplug_tryDeleteVST3 %p | all refcounts are zero, deleting everything!", vst3);
        free(vst3);
    }
}

#if CPLUG_WANT_GUI
typedef struct VST3View
{
    Steinberg_IPlugViewVtbl* lpVtbl;
    Steinberg_IPlugViewVtbl  base;
    cplug_atomic_i32         refcounter;

#ifdef _WIN32
    // Windows only. MacOS is able to detect scale changes using 'viewDidChangeBackingProperties' in NSView
    struct
    {
        Steinberg_IPlugViewContentScaleSupportVtbl* lpVtbl;
        Steinberg_IPlugViewContentScaleSupportVtbl  base;
        cplug_atomic_i32                            refcounter;
    } contentScaleSupport;
#endif

    void* userGUI;
} VST3View;

#ifdef _WIN32
/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/gui/iplugviewcontentscalesupport.h", line 57 */
// Steinberg_FUnknown
static inline VST3View* _cplug_pointerShiftContentScaleSupport(void* const ptr)
{
    return (VST3View*)((char*)(ptr)-offsetof(VST3View, contentScaleSupport));
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3ViewContentScale_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    VST3View* const view = _cplug_pointerShiftContentScaleSupport(self);

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_IPlugViewContentScaleSupport_iid))
    {
        cplug_log("VST3ViewContentScale_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&view->contentScaleSupport.refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }

    cplug_log(
        "VST3ViewContentScale_queryInterface => %p %s %p | WARNING UNSUPPORTED",
        self,
        _cplug_tuid2str(iid),
        iface);

    *iface = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3ViewContentScale_addRef(void* const self)
{
    VST3View* const view = _cplug_pointerShiftContentScaleSupport(self);
    return cplug_atomic_fetch_add_i32(&view->contentScaleSupport.refcounter, 1) + 1;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3ViewContentScale_release(void* const self)
{
    VST3View* const view     = _cplug_pointerShiftContentScaleSupport(self);
    int             refcount = cplug_atomic_fetch_add_i32(&view->contentScaleSupport.refcounter, -1) - 1;
    cplug_log("VST3ViewContentScale_release => %p | refcount %d", self, refcount);

    if (refcount == 0 && cplug_atomic_load_i32(&view->refcounter) == 0)
    {
        cplug_log("VST3ViewContentScale_setContentScaleFactor | Freeing VST3View");
        free(view);
    }
    return refcount;
}
// Steinberg_IPlugViewContentScaleSupport
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3ViewContentScale_setContentScaleFactor(void* const self, const float factor)
{
    cplug_log("VST3ViewContentScale_setContentScaleFactor => %p %f", self, factor);
    VST3View* const view = _cplug_pointerShiftContentScaleSupport(self);

    cplug_setScaleFactor(view->userGUI, factor);

    return Steinberg_kResultOk;
}
#endif // _WIN32

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/gui/iplugview.h", line 122 */
// Steinberg_FUnknown
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_queryInterface(void* self, const Steinberg_TUID iid, void** iface)
{
    VST3View* const view = (VST3View*)self;

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_IPlugView_iid))
    {
        cplug_log("VST3View_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&view->refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }

#ifdef _WIN32
    if (tuid_match(Steinberg_IPlugViewContentScaleSupport_iid, iid))
    {
        cplug_log("VST3View_queryInterface => %p %s %p | OK convert", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&view->contentScaleSupport.refcounter, 1);
        *iface = &view->contentScaleSupport;
        return Steinberg_kResultOk;
    }
#endif

    cplug_log("VST3View_queryInterface => %p %s %p | WARNING UNSUPPORTED", self, _cplug_tuid2str(iid), iface);

    *iface = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3View_addRef(void* self)
{
    VST3View* const view     = (VST3View*)self;
    int             refcount = cplug_atomic_fetch_add_i32(&view->refcounter, 1) + 1;
    cplug_log("VST3View_addRef => %p | refcount %i", self, refcount);
    return refcount;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3View_release(void* self)
{
    VST3View* const view = (VST3View*)self;

    const int refcount = cplug_atomic_fetch_add_i32(&view->refcounter, -1) - 1;
    cplug_log("VST3View_release => %p | refcount %d", self, refcount);
    if (refcount == 0)
    {
        cplug_setVisible(view->userGUI, false);
        // Some hosts (Ableton) don't call removed() before destroying your GUI, others (Bitwig) do.
        cplug_setParent(view->userGUI, NULL);
        cplug_destroyGUI(view->userGUI);
#ifndef _WIN32
        free(view);
#else
        cplug_log("VST3View_release | should call free from IPlugViewContentScaleSupport extension");
        view->contentScaleSupport.lpVtbl->release(&view->contentScaleSupport);
#endif
    }
    return refcount;
}

// ----------------------------------------------------------------------------------------------------------------
// Steinberg_IPlugView

#if defined(_WIN32)
#define CPLUG_VST3_GUI_API Steinberg_kPlatformTypeHWND
#elif defined(__APPLE__)
#define CPLUG_VST3_GUI_API Steinberg_kPlatformTypeNSView
#else
#define CPLUG_VST3_GUI_API Steinberg_kPlatformTypeX11EmbedWindowID
#endif
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_isPlatformTypeSupported(void* const self, const char* const platform_type)
{
    cplug_log("VST3View_isPlatformTypeSupported => %p %s", self, platform_type);

    if (strcmp(platform_type, CPLUG_VST3_GUI_API) == 0)
        return Steinberg_kResultOk;

    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_attached(void* const self, void* const parent, const char* const platform_type)
{
    cplug_log("VST3View_attached => %p %p %s", self, parent, platform_type);
    VST3View* const view = (VST3View*)self;
    cplug_setParent(view->userGUI, parent);
    cplug_setVisible(view->userGUI, parent != NULL);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_removed(void* const self)
{
    cplug_log("VST3View_removed => %p", self);
    VST3View* const view = (VST3View*)self;
    cplug_setVisible(view->userGUI, false);
    cplug_setParent(view->userGUI, NULL);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_onWheel(void* const self, const float distance)
{
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_onKeyDown(void* const self, const char16_t key_char, const int16_t key_code, const int16_t modifiers)
{
    cplug_log("VST3View_onKeyDown => %p %hu %hu %hu", self, key_char, key_code, modifiers);
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_onKeyUp(void* const self, const char16_t key_char, const int16_t key_code, const int16_t modifiers)
{
    cplug_log("VST3View_onKeyUp => %p %hu %hu %hu", self, key_char, key_code, modifiers);
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_getSize(void* const self, struct Steinberg_ViewRect* const rect)
{
    cplug_log("VST3View_getSize %p", rect);

    uint32_t width, height;
    cplug_getSize(((VST3View*)self)->userGUI, &width, &height);

    rect->right  = rect->left + width;
    rect->bottom = rect->top + height;

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_onSize(void* const self, struct Steinberg_ViewRect* const rect)
{
    cplug_log("VST3View_onSize => %p {%d,%d,%d,%d}", self, rect->top, rect->left, rect->right, rect->bottom);
    int width  = rect->right - rect->left;
    int height = rect->bottom - rect->top;
    CPLUG_LOG_ASSERT_RETURN(width >= 0, Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT_RETURN(height >= 0, Steinberg_kInvalidArgument);
    return !cplug_setSize(((VST3View*)self)->userGUI, width, height);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_onFocus(void* const self, const Steinberg_TBool state)
{
    cplug_log("VST3View_onFocus => %p %hhu", state);
    // TODO: Ableton seems to lose track of who has focus. Not sure if this is an Ableton bug or our fault.
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_setFrame(void* const self, Steinberg_IPlugFrame* const frame)
{
    cplug_log("VST3View_setFrame => %p %p", self, frame);
    return Steinberg_kResultTrue;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3View_canResize(void* const self) { return !CPLUG_GUI_RESIZABLE; }

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3View_checkSizeConstraint(void* const self, struct Steinberg_ViewRect* const rect)
{
    cplug_log("VST3View_checkSizeConstraint => %p %d %d %d %d", self, rect->left, rect->top, rect->right, rect->bottom);
    uint32_t width  = rect->right - rect->left;
    uint32_t height = rect->bottom - rect->top;
    cplug_checkSize(((VST3View*)self)->userGUI, &width, &height);
    rect->right  = rect->left + width;
    rect->bottom = rect->top + height;
    // We always return Ok here because Ableton 10 won't change their behaviour if we return anything else
    return Steinberg_kResultOk;
}
#endif // CPLUG_WANT_GUI

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivsteditcontroller.h", line 557 */

// Steinberg_FUnknown
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3MidiMapping_queryInterface(void* thisInterface, const Steinberg_TUID iid, void** obj)
{
    if (tuid_match(iid, Steinberg_Vst_IMidiMapping_iid))
    {
        cplug_log("VST3MidiMapping_queryInterface => %p %s %p | OK", thisInterface, _cplug_tuid2str(iid), obj);
        *obj = thisInterface;
        return Steinberg_kResultOk;
    }

    cplug_log(
        "VST3MidiMapping_queryInterface => %p %s %p | WARNING UNSUPPORTED",
        thisInterface,
        _cplug_tuid2str(iid),
        obj);
    *obj = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3MidiMapping_addRef(void* const thisInterface)
{
    VST3Plugin* const vst3     = _cplug_pointerShiftMidiMapping(thisInterface);
    const int         refcount = cplug_atomic_fetch_add_i32(&vst3->midiMapping.refcounter, 1) + 1;
    cplug_log("VST3MidiMapping_addRef => %p | refcount %i", thisInterface, refcount);
    return refcount;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3MidiMapping_release(void* const thisInterface)
{
    VST3Plugin* vst3     = _cplug_pointerShiftMidiMapping(thisInterface);
    const int   refcount = cplug_atomic_fetch_add_i32(&vst3->midiMapping.refcounter, -1) - 1;
    cplug_log("VST3MidiMapping_release => %p | refcount %d", thisInterface, refcount);

    if (refcount == 0)
        _cplug_tryDeleteVST3(vst3);

    return refcount;
}

// Steinberg_Vst_IMidiMapping
Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3MidiMapping_getMidiControllerAssignment(
    void*                    self,
    Steinberg_int32          busIdx,
    Steinberg_int16          channel,
    Steinberg_Vst_CtrlNumber ctrlNum,
    Steinberg_Vst_ParamID*   id)
{
    // This gets hammered at startup
    // cplug_log("VST3MidiMapping_getMidiControllerAssignment => %p %d %hd %u %p", self, busIdx, channel, ctrlNum, id);
    CPLUG_LOG_ASSERT_RETURN(busIdx == 0, Steinberg_kResultFalse);
    CPLUG_LOG_ASSERT_RETURN(
        ctrlNum >= 0 && ctrlNum < Steinberg_Vst_ControllerNumbers_kCountCtrlNumber,
        Steinberg_kResultFalse);
    *id = CPLUG_MIDI_PARAMID_START + (Steinberg_Vst_ParamID)channel * Steinberg_Vst_ControllerNumbers_kCountCtrlNumber +
          (Steinberg_Vst_ParamID)ctrlNum;
    return Steinberg_kResultTrue;
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivstnoteexpression.h", line 165 */
// Implementation largely copy pasted from here:
// https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Change+History/3.5.0/INoteExpressionController.html

// Steinberg_FUnknown
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3NoteExpression_queryInterface(void* thisInterface, const Steinberg_TUID iid, void** obj)
{
    if (tuid_match(iid, Steinberg_Vst_INoteExpressionController_iid))
    {
        cplug_log("VST3NoteExpression_queryInterface => %p %s %p | OK", thisInterface, _cplug_tuid2str(iid), obj);
        *obj = thisInterface;
        return Steinberg_kResultOk;
    }

    cplug_log(
        "VST3NoteExpression_queryInterface => %p %s %p | WARNING UNSUPPORTED",
        thisInterface,
        _cplug_tuid2str(iid),
        obj);
    *obj = NULL;
    return Steinberg_kNoInterface;
}

Steinberg_uint32 SMTG_STDMETHODCALLTYPE VST3NoteExpression_addRef(void* thisInterface)
{
    VST3Plugin* const vst3     = _cplug_pointerShiftNoteExpression(thisInterface);
    const int         refcount = cplug_atomic_fetch_add_i32(&vst3->noteExpression.refcounter, 1) + 1;
    cplug_log("VST3NoteExpression_addRef => %p | refcount %i", thisInterface, refcount);
    return refcount;
}

Steinberg_uint32 SMTG_STDMETHODCALLTYPE VST3NoteExpression_release(void* thisInterface)
{
    VST3Plugin* vst3     = _cplug_pointerShiftNoteExpression(thisInterface);
    const int   refcount = cplug_atomic_fetch_add_i32(&vst3->noteExpression.refcounter, -1) - 1;
    cplug_log("VST3NoteExpression_release => %p | refcount %d", thisInterface, refcount);

    if (refcount == 0)
        _cplug_tryDeleteVST3(vst3);

    return refcount;
}
// Steinberg_Vst_INoteExpressionController
Steinberg_int32 SMTG_STDMETHODCALLTYPE
VST3NoteExpression_getNoteExpressionCount(void* thisInterface, Steinberg_int32 busIndex, Steinberg_int16 channel)
{
    cplug_log("VST3NoteExpression_getNoteExpressionCount => %p %d %hd", thisInterface, busIndex, channel);
    // we accept only the first bus and 1 channel
    if (busIndex == 0 && channel == 0)
        return 1;
    return 0;
}
Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3NoteExpression_getNoteExpressionInfo(
    void*                                        thisInterface,
    Steinberg_int32                              busIndex,
    Steinberg_int16                              channel,
    Steinberg_int32                              noteExpressionIndex,
    struct Steinberg_Vst_NoteExpressionTypeInfo* info)
{
    cplug_log(
        "VST3NoteExpression_getNoteExpressionInfo => %p %d %hd %d %p",
        thisInterface,
        busIndex,
        channel,
        noteExpressionIndex,
        info);
    // we accept only the first bus and 1 channel and only 1 Note Expression (tuning)
    if (busIndex == 0 && channel == 0 && noteExpressionIndex == 0)
    {
        memset(info, 0, sizeof(*info));

        // set the tuning type
        info->typeId = Steinberg_Vst_NoteExpressionTypeIDs_kTuningTypeID;

        // set some strings
        swprintf((wchar_t*)info->title, 128, L"%ls", L"Tuning");
        swprintf((wchar_t*)info->shortTitle, 128, L"%ls", L"Tun");
        swprintf((wchar_t*)info->units, 128, L"%ls", L"Half Tone");

        info->unitId                = -1; // no unit wanted
        info->associatedParameterId = -1; // no associated parameter wanted

        info->flags =
            Steinberg_Vst_NoteExpressionTypeInfo_NoteExpressionTypeFlags_kIsBipolar; // event is bipolar (centered)

        // for Tuning the convert functions are: plain = 240 * (norm - 0.5); norm = plain / 240 + 0.5;
        // we want to support only +/- one octave
        const double kNormTuningOneOctave = 12.0 / 240.0;

        info->valueDesc.minimum      = 0.5 - kNormTuningOneOctave;
        info->valueDesc.maximum      = 0.5 + kNormTuningOneOctave;
        info->valueDesc.defaultValue = 0.5; // middle of [0, 1] => no detune (240 * (0.5 - 0.5) = 0)
        info->valueDesc.stepCount    = 0;   // we want continuous (no step)

        return Steinberg_kResultTrue;
    }

    return Steinberg_kResultFalse;
}

Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3NoteExpression_getNoteExpressionStringByValue(
    void*                              thisInterface,
    Steinberg_int32                    busIndex,
    Steinberg_int16                    channel,
    Steinberg_Vst_NoteExpressionTypeID id,
    Steinberg_Vst_NoteExpressionValue  valueNormalized,
    Steinberg_Vst_String128            string)
{
    cplug_log(
        "VST3NoteExpression_getNoteExpressionStringByValue => %p %d %hd %u %f %p",
        thisInterface,
        busIndex,
        channel,
        id,
        valueNormalized,
        string);
    // here we use the id (not the index)
    if (busIndex == 0 && channel == 0 && id == Steinberg_Vst_NoteExpressionTypeIDs_kTuningTypeID)
    {
        // here we have to convert a normalized value to a Tuning string representation
        valueNormalized = (240 * valueNormalized) - 120; // compute half Tones
        swprintf((wchar_t*)string, 128, L"%.2f", valueNormalized);

        return Steinberg_kResultTrue;
    }

    return Steinberg_kResultFalse;
}

Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3NoteExpression_getNoteExpressionValueByString(
    void*                              thisInterface,
    Steinberg_int32                    busIndex,
    Steinberg_int16                    channel,
    Steinberg_Vst_NoteExpressionTypeID id,
    const Steinberg_Vst_TChar*         string,
    Steinberg_Vst_NoteExpressionValue* valueNormalized)
{
    cplug_log(
        "VST3NoteExpression_getNoteExpressionValueByString => %p %d %hd %u %p %p",
        thisInterface,
        busIndex,
        channel,
        id,
        string,
        valueNormalized);
    // here we use the id (not the index)
    if (busIndex == 0 && channel == 0 && id == Steinberg_Vst_NoteExpressionTypeIDs_kTuningTypeID)
    {
        // here we have to convert a given tuning string (half Tone) to a normalized value
        double tmp   = 0;
        int    count = swscanf((const wchar_t*)string, L"%f", &tmp);
        if (count)
        {
            *valueNormalized = (tmp + 120) / 240;
            return Steinberg_kResultTrue;
        }
    }

    return Steinberg_kResultOk;
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivsteditcontroller.h", line 398 */
// Steinberg_FUnknown
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    VST3Plugin* vst3 = _cplug_pointerShiftController(self);

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_IPluginBase_iid) ||
        tuid_match(iid, Steinberg_Vst_IEditController_iid))
    {
        cplug_log("VST3Controller_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&vst3->controller.refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }
    if (tuid_match(iid, Steinberg_Vst_IMidiMapping_iid))
    {
        cplug_log("VST3Controller_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&vst3->midiMapping.refcounter, 1);
        *iface = &vst3->midiMapping;
        return Steinberg_kResultOk;
    }
    if (tuid_match(iid, Steinberg_Vst_INoteExpressionController_iid))
    {
        cplug_log("VST3Controller_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&vst3->noteExpression.refcounter, 1);
        *iface = &vst3->noteExpression;
        return Steinberg_kResultOk;
    }

    cplug_log("VST3Controller_queryInterface => %p %s %p | WARNING UNSUPPORTED", self, _cplug_tuid2str(iid), iface);
    *iface = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Controller_addRef(void* const self)
{
    VST3Plugin* const vst3     = _cplug_pointerShiftController(self);
    const int         refcount = cplug_atomic_fetch_add_i32(&vst3->controller.refcounter, 1) + 1;
    cplug_log("VST3Controller_addRef => %p | refcount %i", self, refcount);
    return refcount;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Controller_release(void* const self)
{
    VST3Plugin* vst3     = _cplug_pointerShiftController(self);
    const int   refcount = cplug_atomic_fetch_add_i32(&vst3->controller.refcounter, -1) - 1;
    cplug_log("VST3Controller_release => %p | refcount %i", self, refcount);

    if (refcount == 0)
    {
        if (vst3->controller.componentHandler)
            vst3->controller.componentHandler->lpVtbl->release(vst3->controller.componentHandler);

        cplug_log("VST3Controller_release | should call _cplug_tryDeleteVST3 from IMidiMapping extension");
        vst3->midiMapping.lpVtbl->release(&vst3->midiMapping);
        cplug_log("VST3Controller_release | should call _cplug_tryDeleteVST3 from "
                  "Steinberg_Vst_INoteExpressionController extension");
        vst3->noteExpression.lpVtbl->release(&vst3->noteExpression);
    }

    return refcount;
}
// Steinberg_IPluginBase
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_initialize(void* const self, Steinberg_FUnknown* const context)
{
    cplug_log("VST3Controller_initialize => %p %p", self, context);
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Controller_terminate(void* self)
{
    cplug_log("VST3Controller_terminate => %p", self);
    return Steinberg_kResultOk;
}
// Steinberg_Vst_IEditController
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_setComponentState(void* const self, Steinberg_IBStream* const stream)
{
    cplug_log("VST3Controller_setComponentState => %p %p", self, stream);
    return Steinberg_kNotImplemented;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_setState(void* const self, Steinberg_IBStream* const stream)
{
    cplug_log("VST3Controller_setState => %p %p", self, stream);
    return Steinberg_kNotImplemented;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_getState(void* const self, Steinberg_IBStream* const stream)
{
    cplug_log("VST3Controller_getState => %p %p", self, stream);
    return Steinberg_kNotImplemented;
}

static int32_t SMTG_STDMETHODCALLTYPE VST3Controller_getParameterCount(void* self)
{
    // cplug_log("VST3Controller_getParameterCount => %p", self);
    VST3Plugin* const vst3      = _cplug_pointerShiftController(self);
    uint32_t          numParams = cplug_getNumParameters(vst3->userPlugin);
#if CPLUG_WANT_MIDI_INPUT
    // We have to lie to VST3 compliant hosts like Cubase & Reaper that we have additional MidiCC params.
    // These hosts will call getParameterInfo() for us to set our special param iDs. These hosts then send us
    // 'parameter' updates which we convert into MIDI. If we don't lie about the parameter count and give the hosts
    // param IDs, they seemingly fail to make a param idx <-> param id mapping, and won't send the plugin (MIDI)
    // parameter updates. For example, this means the pitch wheel won't work, because in VST3, the pitch wheel is a
    // parameter. Other hosts like Ableton, FLStudio, and Bitwig are much more lenient, and will send (MIDI) param
    // updates without requiring us to create fake params in getParameterCount() & getParameterInfo().
    numParams += CPLUG_MIDI_PARAMID_COUNT;
#endif
    return numParams;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_getParameterInfo(void* self, int32_t index, struct Steinberg_Vst_ParameterInfo* info)
{
    // cplug_log("VST3Controller_getParameterInfo => %p %i", self, index);
    VST3Plugin* const vst3         = _cplug_pointerShiftController(self);
    uint32_t          pluginParams = cplug_getNumParameters(vst3->userPlugin);

    CPLUG_LOG_ASSERT(index >= 0 && index < pluginParams + CPLUG_MIDI_PARAMID_COUNT);

    memset(info, 0, sizeof(*info));

    if (index >= 0 && index < pluginParams)
    {
        char buf[sizeof(info->title)] = {0};

        uint32_t paramId = cplug_getParameterID(vst3->userPlugin, index);
        CPLUG_LOG_ASSERT(!cplug_is_midi_param(paramId));
        info->id = paramId;

        // set up flags
        double min, max;
        cplug_getParameterRange(vst3->userPlugin, paramId, &min, &max);
        const uint32_t hints = cplug_getParameterFlags(vst3->userPlugin, paramId);

        if (hints & CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE)
            info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate;
        if (hints & CPLUG_FLAG_PARAMETER_IS_READ_ONLY)
            info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly;
        if (hints & CPLUG_FLAG_PARAMETER_IS_HIDDEN)
            info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsHidden;
        if (hints & CPLUG_FLAG_PARAMETER_IS_BYPASS)
            info->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsBypass;

        if (hints & CPLUG_FLAG_PARAMETER_IS_BOOL)
            info->stepCount = 1;
        else if (hints & CPLUG_FLAG_PARAMETER_IS_INTEGER)
            info->stepCount = (int)(max - min);

        double defaultValue          = cplug_getDefaultParameterValue(vst3->userPlugin, paramId);
        info->defaultNormalizedValue = cplug_normaliseParameterValue(vst3->userPlugin, paramId, defaultValue);
        cplug_getParameterName(vst3->userPlugin, paramId, buf, sizeof(buf));
        _cplug_utf8To16(info->title, buf, 128);
        // Who cares?
        memcpy(info->shortTitle, info->title, sizeof(info->shortTitle));
        return Steinberg_kResultOk;
    }

#if CPLUG_WANT_MIDI_INPUT
    if (index >= pluginParams && index < pluginParams + CPLUG_MIDI_PARAMID_COUNT)
    {
        // Fake MidiCC param
        uint32_t rel_idx = index - pluginParams;
        info->id         = CPLUG_MIDI_PARAMID_START + rel_idx;
        return Steinberg_kResultOk;
    }
#endif
    return Steinberg_kInvalidArgument;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Controller_getParamStringByValue(
    void*                   self,
    Steinberg_Vst_ParamID   paramId,
    double                  normalised,
    Steinberg_Vst_String128 output)
{
    // NOTE very noisy, called many times
    // cplug_log("VST3Controller_getParamStringByValue => %p %u %f %p", self, paramId, normalised, output);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    // Bitwig 5 has been spotted failing this assertion
    CPLUG_LOG_ASSERT_RETURN(normalised >= 0.0 && normalised <= 1.0, Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT(!cplug_is_midi_param(paramId));

    char   buf[128];
    double denormalised = cplug_denormaliseParameterValue(vst3->userPlugin, paramId, normalised);
    cplug_parameterValueToString(vst3->userPlugin, paramId, buf, 128, denormalised);
    _cplug_utf8To16(output, buf, sizeof(buf));

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_getParamValueByString(void* self, Steinberg_Vst_ParamID paramId, char16_t* input, double* output)
{
    // cplug_log("VST3Controller_getParamValueByString => %p %u %p %p", self, paramId, input, output);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    CPLUG_LOG_ASSERT(!cplug_is_midi_param(paramId));

    char as_utf8[128];
    _cplug_utf16To8(as_utf8, input, 128);

    double denormalised = cplug_parameterStringToValue(vst3->userPlugin, paramId, as_utf8);
    *output             = cplug_normaliseParameterValue(vst3->userPlugin, paramId, denormalised);

    return Steinberg_kResultOk;
}

static double SMTG_STDMETHODCALLTYPE
VST3Controller_normalizedParamToPlain(void* self, Steinberg_Vst_ParamID paramId, double normalised)
{
    // Gets called a lot in ableton, even when you aren't touching parameters
    // cplug_log("VST3Controller_normalizedParamToPlain => %p %u %f", self, paramId, normalised);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    CPLUG_LOG_ASSERT_RETURN(normalised >= 0.0 && normalised <= 1.0, 0.0);
    CPLUG_LOG_ASSERT(!cplug_is_midi_param(paramId));
    return cplug_denormaliseParameterValue(vst3->userPlugin, paramId, normalised);
}

static double SMTG_STDMETHODCALLTYPE
VST3Controller_plainParamToNormalised(void* self, Steinberg_Vst_ParamID paramId, double plain)
{
    // Gets called a lot in ableton, even when you aren't touching parameters
    // cplug_log("VST3Controller_plainParamToNormalised => %p %u %f", self, paramId, plain);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    CPLUG_LOG_ASSERT(!cplug_is_midi_param(paramId));
    return cplug_normaliseParameterValue(vst3->userPlugin, paramId, plain);
}

static double SMTG_STDMETHODCALLTYPE VST3Controller_getParamNormalized(void* self, Steinberg_Vst_ParamID paramId)
{
    // cplug_log("VST3Controller_getParamNormalized => %p %u", self, paramId);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);

    // Reaper & Ableton will ask you for MIDI control values. So far, returning 0 here hasn't caused any problems...
    if (cplug_is_midi_param(paramId))
        return 0.0;

    double val = cplug_getParameterValue(vst3->userPlugin, paramId);
    return cplug_normaliseParameterValue(vst3->userPlugin, paramId, val);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_setParamNormalized(void* const self, const Steinberg_Vst_ParamID paramId, const double normalised)
{
    // Gets called a lot in ableton, even when you aren't touching parameters
    // cplug_log("VST3Controller_setParamNormalized => %p %u %f", self, paramId, normalised);
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    CPLUG_LOG_ASSERT_RETURN(normalised >= 0.0 && normalised <= 1.0, Steinberg_kInvalidArgument);

    if (cplug_is_midi_param(paramId))
    {
        uint8_t channel = (paramId - CPLUG_MIDI_PARAMID_START) / Steinberg_Vst_ControllerNumbers_kCountCtrlNumber;
        uint8_t control = (paramId - CPLUG_MIDI_PARAMID_START) % Steinberg_Vst_ControllerNumbers_kCountCtrlNumber;

        if (vst3->midiContollerQueueSize < CPLUG_ARRLEN(vst3->midiContollerQueue))
        {
            uint8_t* midi = (uint8_t*)&vst3->midiContollerQueue[vst3->midiContollerQueueSize];

            switch (control)
            {
            case Steinberg_Vst_ControllerNumbers_kAfterTouch:
                midi[0] = 0xd0 | channel;
                midi[1] = (uint8_t)(normalised * 127.0);
                break;
            case Steinberg_Vst_ControllerNumbers_kPitchBend:
            {
                uint16_t pb = (uint16_t)(normalised * 16383);
                midi[0]     = 0xe0 | channel;
                midi[1]     = pb & 127;
                midi[2]     = (pb >> 7) & 127;
                break;
            }
            default:
                midi[0] = 0xb0;
                midi[1] = control;
                midi[2] = (uint8_t)(normalised * 127.0);
                break;
            }

            vst3->midiContollerQueueSize++;
        }

        return Steinberg_kResultOk;
    }

    double denormalisedVal = cplug_denormaliseParameterValue(vst3->userPlugin, paramId, normalised);
    cplug_setParameterValue(vst3->userPlugin, paramId, denormalisedVal);

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Controller_setComponentHandler(void* self, Steinberg_Vst_IComponentHandler* handler)
{
    cplug_log("VST3Controller_setComponentHandler => %p %p", self, handler);
    // NOTE: Ableton 10, FL Studio & Cubase have been spotted trying to pass NULL here.
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);

    if (vst3->controller.componentHandler)
        vst3->controller.componentHandler->lpVtbl->release(vst3->controller.componentHandler);

    if (handler != NULL)
        handler->lpVtbl->addRef(handler);

    vst3->controller.componentHandler = handler;

    return Steinberg_kResultOk;
}

static Steinberg_IPlugView* SMTG_STDMETHODCALLTYPE VST3Controller_createView(void* self, const char* name)
{
    cplug_log("VST3Controller_createView => %p %s", self, name);

    // NOTE: VST3 does not appear to have any kind of hide feature.
    // This means windows need to constantly be created & destroyed.
    // Create must be followed by show, destroy must be preceded by hide

#if CPLUG_WANT_GUI
    VST3Plugin* const vst3 = _cplug_pointerShiftController(self);
    // plugin must be initialized
    CPLUG_LOG_ASSERT_RETURN(vst3->userPlugin != NULL, NULL);

    VST3View* const view = (VST3View*)malloc(sizeof(VST3View));

    view->lpVtbl = &view->base;
    // Steinberg_FUnknown
    view->base.queryInterface = VST3View_queryInterface;
    view->base.addRef         = VST3View_addRef;
    view->base.release        = VST3View_release;
    // Steinberg_IPlugView
    view->base.isPlatformTypeSupported = VST3View_isPlatformTypeSupported;
    view->base.attached                = VST3View_attached;
    view->base.removed                 = VST3View_removed;
    view->base.onWheel                 = VST3View_onWheel;
    view->base.onKeyDown               = VST3View_onKeyDown;
    view->base.onKeyUp                 = VST3View_onKeyUp;
    view->base.getSize                 = VST3View_getSize;
    view->base.onSize                  = VST3View_onSize;
    view->base.onFocus                 = VST3View_onFocus;
    view->base.setFrame                = VST3View_setFrame;
    view->base.canResize               = VST3View_canResize;
    view->base.checkSizeConstraint     = VST3View_checkSizeConstraint;
    view->refcounter                   = 1;

#ifdef _WIN32
    view->contentScaleSupport.lpVtbl = &view->contentScaleSupport.base;
    // Steinberg_FUnknown
    view->contentScaleSupport.base.queryInterface = VST3ViewContentScale_queryInterface;
    view->contentScaleSupport.base.addRef         = VST3ViewContentScale_addRef;
    view->contentScaleSupport.base.release        = VST3ViewContentScale_release;
    // Steinberg_IPlugViewContentScaleSupport
    view->contentScaleSupport.base.setContentScaleFactor = VST3ViewContentScale_setContentScaleFactor;
    view->contentScaleSupport.refcounter                 = 1;
#endif // _WIN32

    void* userGUI = cplug_createGUI(vst3->userPlugin);
    CPLUG_LOG_ASSERT(userGUI != NULL);
    view->userGUI = userGUI;

    return (Steinberg_IPlugView*)view;
#else  // !CPLUG_WANT_GUI
    return NULL;
#endif // CPLUG_WANT_GUI
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivstaudioprocessor.h", line 399 */
// Steinberg_FUnknown
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3ProcessContextRequirements_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_Vst_IProcessContextRequirements_iid))
    {
        cplug_log("query_interface_process_context_requirements => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        *iface = self;
        return Steinberg_kResultOk;
    }

    cplug_log(
        "query_interface_process_context_requirements => %p %s %p | WARNING UNSUPPORTED",
        self,
        _cplug_tuid2str(iid),
        iface);

    *iface = NULL;
    return Steinberg_kNoInterface;
}
static uint32_t SMTG_STDMETHODCALLTYPE VST3ProcessContextRequirements_addRef(void* self) { return 1; }
static uint32_t SMTG_STDMETHODCALLTYPE VST3ProcessContextRequirements_release(void* self) { return 0; }
// Steinberg_Vst_IProcessContextRequirements
static uint32_t SMTG_STDMETHODCALLTYPE VST3ProcessContextRequirements_getProcessContextRequirements(void* self)
{
    // clang-format off
    return 0
        // | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedSystemTime
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedContinousTimeSamples
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedProjectTimeMusic
        // | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedBarPositionMusic
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedCycleMusic
        // | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedSamplesToNextClock
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedTempo
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedTimeSignature
        // | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedChord
        // | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedFrameRate
        | Steinberg_Vst_IProcessContextRequirements_Flags_kNeedTransportState;
    // clang-format on
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivstaudioprocessor.h", line 258 */
// Steinberg_FUnknown
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Processor_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_Vst_IAudioProcessor_iid))
    {
        cplug_log("VST3Processor_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&vst3->processor.refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }

    if (tuid_match(iid, Steinberg_Vst_IProcessContextRequirements_iid))
    {
        cplug_log("VST3Processor_queryInterface => %p %s %p | OK convert static", self, _cplug_tuid2str(iid), iface);
        *iface = &g_vst3ProcessContext;
        return Steinberg_kResultOk;
    }

    cplug_log("VST3Processor_queryInterface => %p %s %p | WARNING UNSUPPORTED", self, _cplug_tuid2str(iid), iface);

    *iface = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Processor_addRef(void* const self)
{
    VST3Plugin* const vst3     = _cplug_pointerShiftProcessor(self);
    const int         refcount = cplug_atomic_fetch_add_i32(&vst3->processor.refcounter, 1) + 1;
    cplug_log("VST3Processor_addRef => %p | refcount %i", self, refcount);
    return refcount;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Processor_release(void* const self)
{
    VST3Plugin* const vst3     = _cplug_pointerShiftProcessor(self);
    const int         refcount = cplug_atomic_fetch_add_i32(&vst3->processor.refcounter, -1) - 1;
    cplug_log("VST3Processor_release => %p | refcount %i", self, refcount);
    if (refcount == 0)
        _cplug_tryDeleteVST3(vst3);

    return refcount;
}
// Steinberg_Vst_IAudioProcessor
static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Processor_setBusArrangements(
    void* const                  self,
    Steinberg_Vst_Speaker* const inputs,
    const int32_t                num_inputs,
    Steinberg_Vst_Speaker* const outputs,
    const int32_t                num_outputs)
{
    // NOTE this is called a bunch of times in JUCE hosts
    cplug_log("VST3Processor_setBusArrangements => %p %p %i %p %i", self, inputs, num_inputs, outputs, num_outputs);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    bool input_ok = true;
#if CPLUG_NUM_INPUT_BUSSES
    CPLUG_LOG_ASSERT_RETURN(num_inputs >= 0, Steinberg_kInvalidArgument);
    for (int i = 0; i < num_inputs && i < CPLUG_NUM_INPUT_BUSSES; i++)
    {
        uint32_t              num_channels      = cplug_getInputBusChannelCount(vst3->userPlugin, i);
        Steinberg_Vst_Speaker requested_speaker = inputs[i];
        Steinberg_Vst_Speaker accepted_speaker  = _cplug_channelCountToVST3Speaker(num_channels);
        // cplug_log("bus input(%d) requesting %d accepting %d", i, requested_speaker, accepted_speaker);

        input_ok = input_ok && (requested_speaker == 0 || requested_speaker == accepted_speaker);
    }
#endif

    bool output_ok = true;
#if CPLUG_NUM_OUTPUT_BUSSES
    CPLUG_LOG_ASSERT_RETURN(num_outputs >= 0, Steinberg_kInvalidArgument);
    for (int i = 0; i < num_outputs && i < CPLUG_NUM_OUTPUT_BUSSES; i++)
    {
        uint32_t              num_channels      = cplug_getOutputBusChannelCount(vst3->userPlugin, i);
        Steinberg_Vst_Speaker requested_speaker = outputs[i];
        Steinberg_Vst_Speaker accepted_speaker  = _cplug_channelCountToVST3Speaker(num_channels);
        // cplug_log("bus output(%d) requesting %d accepting %d", i, requested_speaker, accepted_speaker);

        output_ok = output_ok && (requested_speaker == 0 || requested_speaker == accepted_speaker);
    }
#endif

    return (input_ok && output_ok) ? Steinberg_kResultTrue : Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Processor_getBusArrangement(
    void* const                  self,
    const int32_t                busDirection,
    const int32_t                busIndex,
    Steinberg_Vst_Speaker* const speaker)
{
    cplug_log(
        "VST3Processor_getBusArrangement => %p %s %i %p",
        self,
        _cplug_getBusDirectionStr(busDirection),
        busIndex,
        speaker);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    CPLUG_LOG_ASSERT_RETURN(
        busDirection == Steinberg_Vst_BusDirections_kInput || busDirection == Steinberg_Vst_BusDirections_kOutput,
        Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT_RETURN(speaker != NULL, Steinberg_kInvalidArgument);

    uint32_t num_channels = 0;
    if (busDirection == Steinberg_Vst_BusDirections_kInput)
        num_channels = cplug_getInputBusChannelCount(vst3->userPlugin, busIndex);
    else // if (busDirection == Steinberg_Vst_BusDirections_kOutput)
        num_channels = cplug_getOutputBusChannelCount(vst3->userPlugin, busIndex);
    *speaker = _cplug_channelCountToVST3Speaker(num_channels);
    return *speaker == 0 ? Steinberg_kResultFalse : Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Processor_canProcessSampleSize(void* self, const int32_t symbolic_sample_size)
{
    // NOTE runs during RT
    // cplug_log("VST3Processor_canProcessSampleSize => %i", symbolic_sample_size);
    return symbolic_sample_size == Steinberg_Vst_SymbolicSampleSizes_kSample32 ? Steinberg_kResultOk
                                                                               : Steinberg_kNotImplemented;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Processor_getLatencySamples(void* const self)
{
    cplug_log("VST3Processor_getLatencySamples => %p", self);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    return cplug_getLatencyInSamples(vst3->userPlugin);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Processor_setupProcessing(void* const self, struct Steinberg_Vst_ProcessSetup* const setup)
{
    cplug_log("VST3Processor_setupProcessing => %p %p", self, setup);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    cplug_log(
        "VST3Processor_setupProcessing => %p %p | %d %f",
        self,
        setup,
        setup->maxSamplesPerBlock,
        setup->sampleRate);

    CPLUG_LOG_ASSERT_RETURN(
        setup->symbolicSampleSize == Steinberg_Vst_SymbolicSampleSizes_kSample32,
        Steinberg_kInvalidArgument);

    // TODO processMode can be:
    // Steinberg_Vst_ProcessModes_kRealtime
    // Steinberg_Vst_ProcessModes_kPrefetch,
    // Steinberg_Vst_ProcessModes_kOffline

    CPLUG_LOG_ASSERT(setup->sampleRate > 0.0);
    CPLUG_LOG_ASSERT(setup->maxSamplesPerBlock >= 2);

    cplug_setSampleRateAndBlockSize(vst3->userPlugin, setup->sampleRate, setup->maxSamplesPerBlock);

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Processor_setProcessing(void* const self, const Steinberg_TBool processing)
{
    cplug_log("VST3Processor_setProcessing => %p %u", self, processing);
    // do we care about this function?

    return Steinberg_kResultOk;
}

// NOTE: Event in VST3 speak means MIDI note on/off. Parameter in VST3 speak = an actual parameter or 'Midi control'
typedef struct VST3ProcessContextTranslator
{
    CplugProcessContext               cplugContext;
    VST3Plugin*                       vst3;
    struct Steinberg_Vst_ProcessData* data;

    uint32_t midiControlQueueIdx;
    uint32_t midiEventIdx;
    uint32_t paramIdx;
    uint32_t nextEventFrame;
} VST3ProcessContextTranslator;

bool VST3ProcessContextTranslator_enqueueEvent(CplugProcessContext* ctx, const CplugEvent* event, uint32_t frameIdx)
{
    // cplug_log("VST3ProcessContextTranslator_enqueueEvent => %p %p %u", ctx, event, frameIdx);
    VST3ProcessContextTranslator* vst3ctx = (VST3ProcessContextTranslator*)ctx;

    switch (event->type)
    {
    case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
    {
        CPLUG_LOG_ASSERT_RETURN(vst3ctx->data->outputParameterChanges != NULL, false);

        Steinberg_int32                        idx   = 0;
        struct Steinberg_Vst_IParamValueQueue* queue = vst3ctx->data->outputParameterChanges->lpVtbl->addParameterData(
            vst3ctx->data->outputParameterChanges,
            &event->parameter.id,
            &idx);
        // In Cubase 13, outputParameterChanges exists but the queue doesn't...
        if (queue == NULL)
            return false;

        double normalised =
            cplug_normaliseParameterValue(vst3ctx->vst3->userPlugin, event->parameter.id, event->parameter.value);
        Steinberg_tresult result = queue->lpVtbl->addPoint(queue, frameIdx, normalised, &idx);
        return result == Steinberg_kResultOk;
    }
    default:
        break;
    }

    return false;
}

bool VST3ProcessContextTranslator_dequeueEvent(CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    // cplug_log("VST3ProcessContextTranslator_dequeueEvent => %p %p %u", ctx, event, frameIdx);
    VST3ProcessContextTranslator* translator = (VST3ProcessContextTranslator*)ctx;

    if (frameIdx >= translator->cplugContext.numFrames)
        return false;

    if (translator->midiControlQueueIdx < translator->vst3->midiContollerQueueSize)
    {
        event->type            = CPLUG_EVENT_MIDI;
        event->midi.frame      = frameIdx;
        event->midi.bytesAsInt = translator->vst3->midiContollerQueue[translator->midiControlQueueIdx];

        translator->midiControlQueueIdx++;
        return true;
    }

    // Studio One has been spotted sending a NULL IEventList
    Steinberg_Vst_IEventList* const inEvents = translator->data->inputEvents;
    if (inEvents)
    {
        int numMidiEvents = inEvents->lpVtbl->getEventCount(inEvents);
        if (translator->midiEventIdx < numMidiEvents)
        {
            struct Steinberg_Vst_Event vst3Midi;
            inEvents->lpVtbl->getEvent(inEvents, translator->midiEventIdx, &vst3Midi);

            if (vst3Midi.sampleOffset == frameIdx)
            {
                translator->midiEventIdx++;

                event->type       = CPLUG_EVENT_MIDI;
                event->midi.frame = vst3Midi.sampleOffset;
                switch (vst3Midi.type)
                {
                case Steinberg_Vst_Event_EventTypes_kNoteOnEvent:
                {
                    event->midi.status = 0x90 | (uint8_t)vst3Midi.Steinberg_Vst_Event_noteOn.channel;
                    event->midi.data1  = (uint8_t)vst3Midi.Steinberg_Vst_Event_noteOn.pitch;
                    event->midi.data2  = (uint8_t)(vst3Midi.Steinberg_Vst_Event_noteOn.velocity * 127.0f);

                    // Add to noteId > pitch map
                    struct VST3Plugin* vst3 = translator->vst3;

                    if (vst3->noteidmap.size < CPLUG_ARRLEN(vst3->noteidmap.ids))
                    {
                        const size_t idx           = vst3->noteidmap.size;
                        vst3->noteidmap.ids[idx]   = vst3Midi.Steinberg_Vst_Event_noteOn.noteId;
                        vst3->noteidmap.pitch[idx] = (uint8_t)vst3Midi.Steinberg_Vst_Event_noteOn.pitch;
                        vst3->noteidmap.size++;
                    }
                    break;
                }
                case Steinberg_Vst_Event_EventTypes_kNoteOffEvent:
                {
                    event->midi.status = 0x80 | (uint8_t)vst3Midi.Steinberg_Vst_Event_noteOff.channel;
                    event->midi.data1  = (uint8_t)vst3Midi.Steinberg_Vst_Event_noteOff.pitch;
                    event->midi.data2  = (uint8_t)(vst3Midi.Steinberg_Vst_Event_noteOff.velocity * 127.0f);

                    // Remove from noteId > pitch map
                    struct VST3Plugin* vst3 = translator->vst3;
                    if (vst3->noteidmap.size > 0)
                    {
                        bool   do_copy = false;
                        size_t idx     = 0;
                        for (; idx < vst3->noteidmap.size; idx++)
                        {
                            if (vst3Midi.Steinberg_Vst_Event_noteOff.noteId == vst3->noteidmap.ids[idx])
                                do_copy = true;
                            if (do_copy)
                            {
                                size_t next_idx = idx + 1;
                                if (next_idx < vst3->noteidmap.size)
                                {
                                    vst3->noteidmap.ids[idx]   = vst3->noteidmap.ids[next_idx];
                                    vst3->noteidmap.pitch[idx] = vst3->noteidmap.pitch[next_idx];
                                }
                            }
                        }
                    }
                    break;
                }
                case Steinberg_Vst_Event_EventTypes_kPolyPressureEvent:
                    event->midi.status = 0xA0 | (uint8_t)vst3Midi.Steinberg_Vst_Event_polyPressure.channel;
                    event->midi.data1  = (uint8_t)vst3Midi.Steinberg_Vst_Event_polyPressure.pitch;
                    event->midi.data2  = (uint8_t)(vst3Midi.Steinberg_Vst_Event_polyPressure.pressure * 127.0f);
                    break;
                case Steinberg_Vst_Event_EventTypes_kNoteExpressionValueEvent:
                {
                    // Find pitch in map
                    const VST3Plugin*                                    vst3 = translator->vst3;
                    const struct Steinberg_Vst_NoteExpressionValueEvent* noteExp =
                        &vst3Midi.Steinberg_Vst_Event_noteExpressionValue;

                    int32_t key = -1;
                    size_t  idx = 0;
                    for (; idx < vst3->noteidmap.size; idx++)
                    {
                        if (noteExp->noteId == vst3->noteidmap.ids[idx])
                        {
                            key = vst3->noteidmap.pitch[idx];
                            break;
                        }
                    }

                    if (key != -1 && noteExp->typeId == Steinberg_Vst_NoteExpressionTypeIDs_kTuningTypeID)
                    {
                        // Denormalise value to range -120 - 120 semitones
                        const double norm            = noteExp->value;
                        event->note_expression.type  = CPLUG_EVENT_NOTE_EXPRESSION_TUNING;
                        event->note_expression.key   = key;
                        event->note_expression.value = -120.0 + norm * 240;
                    }
                    else
                    {
                        event->type = CPLUG_EVENT_UNHANDLED_EVENT;
                    }
                    break;
                }
                case Steinberg_Vst_Event_EventTypes_kDataEvent: // TODO: support SYSEX
                case Steinberg_Vst_Event_EventTypes_kNoteExpressionTextEvent:
                case Steinberg_Vst_Event_EventTypes_kChordEvent:
                case Steinberg_Vst_Event_EventTypes_kScaleEvent:
                case Steinberg_Vst_Event_EventTypes_kLegacyMIDICCOutEvent:
                    cplug_log("Unhandled MIDI event: %hu", vst3Midi.type);
                    break;
                }
                return true;
            }

            if ((uint32_t)vst3Midi.sampleOffset < translator->nextEventFrame)
                translator->nextEventFrame = (uint32_t)vst3Midi.sampleOffset;
        }
    }

    Steinberg_Vst_IParameterChanges* inParams = translator->data->inputParameterChanges;
    CPLUG_LOG_ASSERT(inParams != NULL);

    uint32_t numParams = inParams->lpVtbl->getParameterCount(inParams);
    while (translator->paramIdx < numParams)
    {
        Steinberg_Vst_IParamValueQueue* queue = inParams->lpVtbl->getParameterData(inParams, translator->paramIdx);
        translator->paramIdx++;

        int pointIdx  = 0;
        int numPoints = queue->lpVtbl->getPointCount(queue);

        int eventFrame  = 0;
        int endQuantize = frameIdx + CPLUG_EVENT_FRAME_QUANTISE;
        if (endQuantize > translator->nextEventFrame)
            endQuantize = translator->nextEventFrame;

        Steinberg_Vst_ParamValue value = 0;
        queue->lpVtbl->getPoint(queue, pointIdx, &eventFrame, &value);
        // Skip to next frame within our quantize region
        pointIdx++;
        while (pointIdx < numPoints && eventFrame < endQuantize)
        {
            queue->lpVtbl->getPoint(queue, pointIdx, &eventFrame, &value);
            pointIdx++;
        }
        pointIdx--;

        queue->lpVtbl->getPoint(queue, pointIdx, &eventFrame, &value);

        if (eventFrame >= frameIdx && eventFrame < endQuantize)
        {
            Steinberg_Vst_ParamID paramId = queue->lpVtbl->getParameterId(queue);
            if (cplug_is_midi_param(paramId))
            {
                event->midi.type  = CPLUG_EVENT_MIDI;
                event->midi.frame = frameIdx;

                uint32_t diff    = paramId - CPLUG_MIDI_PARAMID_START;
                uint8_t  channel = diff / Steinberg_Vst_ControllerNumbers_kCountCtrlNumber;
                uint8_t  control = diff % Steinberg_Vst_ControllerNumbers_kCountCtrlNumber;

                switch (control)
                {
                case Steinberg_Vst_ControllerNumbers_kAfterTouch:
                    event->midi.status = 0xd0 | channel;
                    event->midi.data1  = (uint8_t)(value * 127.0);
                    break;
                case Steinberg_Vst_ControllerNumbers_kPitchBend:
                {
                    uint16_t pb        = (uint16_t)(value * 16383);
                    event->midi.status = 0xe0 | channel;
                    event->midi.data1  = pb & 127;
                    event->midi.data2  = (pb >> 7) & 127;
                    break;
                }
                default:
                    event->midi.status = 0xb0 | channel;
                    event->midi.data1  = control;
                    event->midi.data2  = (uint8_t)(value * 127.0);
                    break;
                }
            }
            else
            {
                event->parameter.type  = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                event->parameter.id    = paramId;
                event->parameter.value = cplug_denormaliseParameterValue(translator->vst3->userPlugin, paramId, value);
            }

            return true;
        }

        if (eventFrame > frameIdx && eventFrame < translator->nextEventFrame)
            translator->nextEventFrame = eventFrame;
    }

    CPLUG_LOG_ASSERT(translator->nextEventFrame > 0);
    translator->paramIdx = 0;

    event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
    event->processAudio.endFrame = translator->nextEventFrame;

    translator->nextEventFrame = translator->cplugContext.numFrames;
    return true;
}

float** VST3ProcessContextTranslator_getAudioInput(const CplugProcessContext* ctx, uint32_t busIdx)
{
    // cplug_log("VST3ProcessContextTranslator_getAudioInput => %p %u", ctx, busIdx);
    VST3ProcessContextTranslator* vst3ctx = (VST3ProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT(busIdx < vst3ctx->data->numInputs);
    return vst3ctx->data->inputs[busIdx].Steinberg_Vst_AudioBusBuffers_channelBuffers32;
}

float** VST3ProcessContextTranslator_getAudioOutput(const CplugProcessContext* ctx, uint32_t busIdx)
{
    // cplug_log("VST3ProcessContextTranslator_getAudioOutput => %p %u", ctx, busIdx);
    VST3ProcessContextTranslator* vst3ctx = (VST3ProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT_RETURN(busIdx < vst3ctx->data->numOutputs, NULL);
    return vst3ctx->data->outputs[busIdx].Steinberg_Vst_AudioBusBuffers_channelBuffers32;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Processor_process(void* const self, struct Steinberg_Vst_ProcessData* const data)
{
    // cplug_log("VST3Processor_process => %p", self);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    CPLUG_LOG_ASSERT_RETURN(
        data->symbolicSampleSize == Steinberg_Vst_SymbolicSampleSizes_kSample32,
        Steinberg_kInvalidArgument);

    VST3ProcessContextTranslator translator;
    memset(&translator, 0, sizeof(translator));
    translator.cplugContext.numFrames = data->numSamples;

    if (data->processContext != NULL)
    {
        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kPlaying)
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_IS_PLAYING;
        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kRecording)
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_IS_RECORDING;

        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kProjectTimeMusicValid)
        {
            translator.cplugContext.flags         |= CPLUG_FLAG_TRANSPORT_HAS_PLAYHEAD_BEATS;
            translator.cplugContext.playheadBeats  = data->processContext->projectTimeMusic;
        }
        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kCycleActive)
        {
            translator.cplugContext.flags          |= CPLUG_FLAG_TRANSPORT_IS_LOOPING;
            translator.cplugContext.loopStartBeats  = data->processContext->cycleStartMusic;
            translator.cplugContext.loopEndBeats    = data->processContext->cycleEndMusic;
        }
        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kTempoValid)
        {
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_HAS_BPM;
            translator.cplugContext.bpm    = data->processContext->tempo;
        }
        if (data->processContext->state & Steinberg_Vst_ProcessContext_StatesAndFlags_kTimeSigValid)
        {
            translator.cplugContext.flags              |= CPLUG_FLAG_TRANSPORT_HAS_TIME_SIGNATURE;
            translator.cplugContext.timeSigNumerator    = data->processContext->timeSigNumerator;
            translator.cplugContext.timeSigDenominator  = data->processContext->timeSigDenominator;
        }
    }

    translator.cplugContext.enqueueEvent   = VST3ProcessContextTranslator_enqueueEvent;
    translator.cplugContext.dequeueEvent   = VST3ProcessContextTranslator_dequeueEvent;
    translator.cplugContext.getAudioInput  = VST3ProcessContextTranslator_getAudioInput;
    translator.cplugContext.getAudioOutput = VST3ProcessContextTranslator_getAudioOutput;
    translator.vst3                        = vst3;
    translator.data                        = data;
    translator.midiControlQueueIdx         = 0;
    translator.midiEventIdx                = 0;
    translator.paramIdx                    = 0;
    translator.nextEventFrame              = data->numSamples;

    cplug_process(vst3->userPlugin, &translator.cplugContext);

    vst3->midiContollerQueueSize = 0;

    return Steinberg_kResultOk;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Processor_getTailSamples(void* const self)
{
    // cplug_log("VST3Processor_getTailSamples => %p", self);
    VST3Plugin* const vst3 = _cplug_pointerShiftProcessor(self);

    return cplug_getTailInSamples(vst3->userPlugin);
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/vst/ivstcomponent.h", line 157 */
// Steinberg_FUnknown
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Component_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_IPluginBase_iid) ||
        tuid_match(iid, Steinberg_Vst_IComponent_iid))
    {
        cplug_log("VST3Component_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&vst3->component.refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }

    if (tuid_match(iid, Steinberg_Vst_IAudioProcessor_iid))
    {
        cplug_log("VST3Component_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);

        cplug_atomic_fetch_add_i32(&vst3->processor.refcounter, 1);
        *iface = &vst3->processor;
        return Steinberg_kResultOk;
    }

    if (tuid_match(iid, Steinberg_Vst_IEditController_iid))
    {
        cplug_log("VST3Component_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);

        cplug_atomic_fetch_add_i32(&vst3->controller.refcounter, 1);
        *iface = &vst3->controller;
        return Steinberg_kResultOk;
    }

    cplug_log("VST3Component_queryInterface => %p %s %p | WARNING UNSUPPORTED", self, _cplug_tuid2str(iid), iface);
    *iface = NULL;
    return Steinberg_kNoInterface;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Component_addRef(void* const self)
{
    VST3Plugin* vst3     = _cplug_pointerShiftComponent(self);
    const int   refcount = cplug_atomic_fetch_add_i32(&vst3->component.refcounter, 1) + 1;
    cplug_log("VST3Component_addRef => %p | refcount %i", self, refcount);
    return refcount;
}

static uint32_t SMTG_STDMETHODCALLTYPE VST3Component_release(void* const self)
{
    VST3Plugin* vst3     = _cplug_pointerShiftComponent(self);
    const int   refcount = cplug_atomic_fetch_add_i32(&vst3->component.refcounter, -1) - 1;
    cplug_log("VST3Component_release => %p | refcount %i", self, refcount);

    if (refcount == 0)
    {
        // The expected lifecycle in this library is that IComponent is created first and destroyed last
        // Bitwig 5 & FL Studio 21 follow this lifecycle
        // Ableton 10 & Reaper 7 create IComponent first, but destroys the IAudioProcessor last (huh?)
        // pluginval will test both destroying IEditController & IComponent last

        // Because we aggregate all the VST3 objects, we must check that all references are 0 before deleting this

        int ec_refcount = vst3->controller.lpVtbl->release(&vst3->controller);
        if (ec_refcount)
        {
            cplug_log("[WARNING] VST3Component_release: IEditController is still active (refcount %d)", ec_refcount);
        }

        int ap_refcount = vst3->processor.lpVtbl->release(&vst3->processor);
        if (ap_refcount)
        {
            cplug_log("[WARNING] VST3Component_release: IAudioProcessor is still active (refcount %d)", ap_refcount);
        }
    }

    return refcount;
}
// Steinberg_IPluginBase
static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Component_initialize(void* const self, Steinberg_FUnknown* const context)
{
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    // check if already initialized
    CPLUG_LOG_ASSERT_RETURN(vst3->host == NULL, Steinberg_kInvalidArgument);

    // query for host application
    if (vst3->host == NULL && context != NULL)
        ((Steinberg_FUnknown*)context)
            ->lpVtbl
            ->queryInterface((Steinberg_FUnknown*)context, Steinberg_Vst_IHostApplication_iid, (void**)&vst3->host);

    cplug_log("VST3Component_initialize => %p %p | hostApplication %p", self, context, vst3->host);

    vst3->userPlugin = cplug_createPlugin(&vst3->hostContext);

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_terminate(void* const self)
{
    cplug_log("VST3Component_terminate => %p", self);
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    cplug_destroyPlugin(vst3->userPlugin);
    vst3->userPlugin = NULL;

    if (vst3->host)
        vst3->host->lpVtbl->release(vst3->host);

    return Steinberg_kResultOk;
}
// Steinberg_Vst_IComponent
static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_getControllerClassId(void* self, Steinberg_TUID class_id)
{
    cplug_log("VST3Component_getControllerClassId => %p", class_id);

    memcpy(class_id, cplug_tuid_controller, sizeof(Steinberg_TUID));
    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_setIoMode(void* const self, const int32_t io_mode)
{
    cplug_log("VST3Component_setIoMode => %p %i", self, io_mode);
    return Steinberg_kNotImplemented;
}

static int32_t SMTG_STDMETHODCALLTYPE
VST3Component_getBusCount(void* const self, const int32_t media_type, const int32_t bus_direction)
{
    // NOTE runs on audio thread
    cplug_log(
        "VST3Component_getBusCount => %p %s %s",
        self,
        _cplug_getMediaTypeStr(media_type),
        _cplug_getBusDirectionStr(bus_direction));
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    const bool isAudio  = media_type == Steinberg_Vst_MediaTypes_kAudio;
    const bool isMidi   = media_type == Steinberg_Vst_MediaTypes_kEvent;
    const bool isInput  = bus_direction == Steinberg_Vst_BusDirections_kInput;
    const bool isOutput = bus_direction == Steinberg_Vst_BusDirections_kOutput;

    if (isAudio && isInput)
        return cplug_getNumInputBusses(vst3->userPlugin);
    if (isAudio && isOutput)
        return cplug_getNumOutputBusses(vst3->userPlugin);
    if (isMidi && isInput)
        return CPLUG_WANT_MIDI_INPUT ? 1 : 0;
    if (isMidi && isOutput)
        return CPLUG_WANT_MIDI_OUTPUT ? 1 : 0;

    return 0;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_getBusInfo(
    void* const                         self,
    const Steinberg_Vst_MediaType       media_type,
    const Steinberg_Vst_BusDirection    bus_direction,
    const int32_t                       bus_idx,
    struct Steinberg_Vst_BusInfo* const info)
{
    cplug_log(
        "VST3Component_getBusInfo => %p %s %s %i %p",
        self,
        _cplug_getMediaTypeStr(media_type),
        _cplug_getBusDirectionStr(bus_direction),
        bus_idx,
        info);
    VST3Plugin* vst3                     = _cplug_pointerShiftComponent(self);
    char        name[sizeof(info->name)] = {0};

    CPLUG_LOG_ASSERT_RETURN(
        media_type == Steinberg_Vst_MediaTypes_kAudio || media_type == Steinberg_Vst_MediaTypes_kEvent,
        Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT_RETURN(
        bus_direction == Steinberg_Vst_BusDirections_kInput || bus_direction == Steinberg_Vst_BusDirections_kOutput,
        Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT_RETURN(bus_idx >= 0, Steinberg_kInvalidArgument);

    const bool isAudio  = media_type == Steinberg_Vst_MediaTypes_kAudio;
    const bool isMIDI   = media_type == Steinberg_Vst_MediaTypes_kEvent;
    const bool isInput  = bus_direction == Steinberg_Vst_BusDirections_kInput;
    const bool isOutput = bus_direction == Steinberg_Vst_BusDirections_kOutput;

    if (isAudio && isInput && bus_idx < cplug_getNumInputBusses(vst3->userPlugin))
    {
        info->mediaType    = media_type;
        info->direction    = bus_direction;
        info->channelCount = cplug_getInputBusChannelCount(vst3->userPlugin, bus_idx);

        cplug_getInputBusName(vst3->userPlugin, bus_idx, name, sizeof(name));
        _cplug_utf8To16(info->name, name, 128);

        const bool isInstrument = CPLUG_IS_INSTRUMENT;
        info->busType           = isInstrument ? Steinberg_Vst_BusTypes_kAux
                                               : (bus_idx == 0 ? Steinberg_Vst_BusTypes_kMain : Steinberg_Vst_BusTypes_kAux);
        info->flags             = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;

        return Steinberg_kResultOk;
    }

    if (isAudio && isOutput && bus_idx < cplug_getNumOutputBusses(vst3->userPlugin))
    {
        info->mediaType    = media_type;
        info->direction    = bus_direction;
        info->channelCount = cplug_getOutputBusChannelCount(vst3->userPlugin, bus_idx);

        cplug_getOutputBusName(vst3->userPlugin, bus_idx, name, sizeof(name));
        _cplug_utf8To16(info->name, name, CPLUG_ARRLEN(info->name));

        info->busType = bus_idx == 0 ? Steinberg_Vst_BusTypes_kMain : Steinberg_Vst_BusTypes_kAux;
        info->flags   = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;

        return Steinberg_kResultOk;
    }

#if CPLUG_WANT_MIDI_INPUT
    if (isMIDI && isInput)
    {
        CPLUG_LOG_ASSERT_RETURN(bus_idx == 0, Steinberg_kInvalidArgument);

        info->mediaType    = media_type;
        info->direction    = bus_direction;
        info->channelCount = 16;
        _cplug_utf8To16(info->name, "MIDI Input", 128);
        info->busType = Steinberg_Vst_BusTypes_kMain;
        info->flags   = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;
        return Steinberg_kResultOk;
    }
#endif // CPLUG_WANT_MIDI_INPUT
#if CPLUG_WANT_MIDI_OUTPUT
    if (isMIDI && isOutput)
    {
        CPLUG_LOG_ASSERT_RETURN(bus_idx == 0, Steinberg_kInvalidArgument);

        info->mediaType    = media_type;
        info->direction    = bus_direction;
        info->channelCount = 16;
        _cplug_utf8To16(info->name, "MIDI Output", 128);
        info->busType = Steinberg_Vst_BusTypes_kMain;
        info->flags   = Steinberg_Vst_BusInfo_BusFlags_kDefaultActive;
        return Steinberg_kResultOk;
    }
#endif // CPLUG_WANT_MIDI_OUTPUT
    return Steinberg_kResultFalse;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_getRoutingInfo(
    void* const                             self,
    struct Steinberg_Vst_RoutingInfo* const input,
    struct Steinberg_Vst_RoutingInfo* const output)
{
    cplug_log("VST3Component_getRoutingInfo => %p %p %p", self, input, output);
    return Steinberg_kNotImplemented;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_activateBus(
    void* const                      self,
    const Steinberg_Vst_MediaType    media_type,
    const Steinberg_Vst_BusDirection bus_direction,
    const Steinberg_int32            bus_idx,
    const Steinberg_TBool            state)
{
    // NOTE this is called a bunch of times
    cplug_log(
        "VST3Component_activateBus => %p %s %s %i %u",
        self,
        _cplug_getMediaTypeStr(media_type),
        _cplug_getBusDirectionStr(bus_direction),
        bus_idx,
        state);
    CPLUG_LOG_ASSERT_RETURN(
        bus_direction == Steinberg_Vst_BusDirections_kInput || bus_direction == Steinberg_Vst_BusDirections_kOutput,
        Steinberg_kInvalidArgument);
    CPLUG_LOG_ASSERT_RETURN(bus_idx >= 0, Steinberg_kInvalidArgument);

    return Steinberg_kResultOk;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Component_setActive(void* const self, const Steinberg_TBool active)
{
    cplug_log("VST3Component_setActive => %p %u", self, active);
    // Do we care about this?
    return Steinberg_kResultOk;
}

int64_t cplug_VST3ReadProcTranslator(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    Steinberg_IBStream* const stream    = (Steinberg_IBStream* const)stateCtx;
    Steinberg_int32           bytesRead = 0;
    Steinberg_tresult         result    = stream->lpVtbl->read(stream, readPos, (int)maxBytesToRead, &bytesRead);
    if (result != Steinberg_kResultOk)
        return -1;
    return bytesRead;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Component_setState(void* const self, Steinberg_IBStream* const stream)
{
    cplug_log("VST3Component_setState => %p", self);
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    cplug_loadState(vst3->userPlugin, stream, cplug_VST3ReadProcTranslator);
    return Steinberg_kResultOk;
}

int64_t cplug_VST3WriteProcTranslator(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    Steinberg_IBStream* const stream = (Steinberg_IBStream* const)stateCtx;

    Steinberg_int32   bytesWritten = 0;
    Steinberg_tresult result       = stream->lpVtbl->write(stream, writePos, (int)numBytesToWrite, &bytesWritten);
    if (result != Steinberg_kResultOk)
        return -1;
    return bytesWritten;
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Component_getState(void* const self, Steinberg_IBStream* const stream)
{
    cplug_log("VST3Component_getState => %p %p", self, stream);
    VST3Plugin* vst3 = _cplug_pointerShiftComponent(self);

    cplug_saveState(vst3->userPlugin, stream, cplug_VST3WriteProcTranslator);
    return Steinberg_kResultOk;
}

/*----------------------------------------------------------------------------------------------------------------------
Source: "pluginterfaces/base/ipluginbase.h", line 446 */
// Steinberg_FUnknown
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_queryInterface(void* const self, const Steinberg_TUID iid, void** const iface)
{
    VST3Factory* const factory = (VST3Factory*)(self);

    if (tuid_match(iid, Steinberg_FUnknown_iid) || tuid_match(iid, Steinberg_IPluginFactory_iid) ||
        tuid_match(iid, Steinberg_IPluginFactory2_iid) || tuid_match(iid, Steinberg_IPluginFactory3_iid))
    {
        cplug_log("VST3Factory_queryInterface => %p %s %p | OK", self, _cplug_tuid2str(iid), iface);
        cplug_atomic_fetch_add_i32(&factory->refcounter, 1);
        *iface = self;
        return Steinberg_kResultOk;
    }

    cplug_log("VST3Factory_queryInterface => %p %s %p | WARNING UNSUPPORTED", self, _cplug_tuid2str(iid), iface);

    *iface = NULL;
    return Steinberg_kNoInterface;
}

uint32_t SMTG_STDMETHODCALLTYPE VST3Factory_addRef(void* const self)
{
    VST3Factory* const factory  = (VST3Factory*)(self);
    const int          refcount = cplug_atomic_fetch_add_i32(&factory->refcounter, 1) + 1;
    cplug_log("VST3Factory_addRef => %p | refcount %i", self, refcount);
    return refcount;
}

uint32_t SMTG_STDMETHODCALLTYPE VST3Factory_release(void* const self)
{
    VST3Factory* const factory  = (VST3Factory*)(self);
    const int          refcount = cplug_atomic_fetch_add_i32(&factory->refcounter, -1) - 1;
    cplug_log("VST3Factory_release => %p | refcount %i", self, refcount);

    if (refcount == 0)
    {
        cplug_log("VST3Factory_release => %p | refcount is zero, deleting factory", self);

        // unref old context if there is one
        if (factory->host != NULL)
            factory->host->lpVtbl->release(factory->host);

        free(factory);
    }

    return refcount;
}
// Steinberg_IPluginFactory
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_getFactoryInfo(void* self, struct Steinberg_PFactoryInfo* const info)
{
    cplug_log("VST3Factory_getFactoryInfo => %p", info);
    memset(info, 0, sizeof(*info));

    info->flags = 0x10; // unicode
    strncpy(info->vendor, CPLUG_COMPANY_NAME, sizeof(info->vendor) - 1);
    strncpy(info->url, CPLUG_PLUGIN_URI, sizeof(info->url) - 1);
    strncpy(info->email, CPLUG_COMPANY_EMAIL, sizeof(info->email) - 1);
    return Steinberg_kResultOk;
}

int32_t SMTG_STDMETHODCALLTYPE VST3Factory_countClasses(void* self)
{
    cplug_log("VST3Factory_countClasses");
    return 1; // factory can only create component, edit-controller must be casted
}

Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_getClassInfo(void* self, const int32_t idx, struct Steinberg_PClassInfo* const info)
{
    cplug_log("VST3Factory_getClassInfo => %i %p", idx, info);
    memset(info, 0, sizeof(*info));
    CPLUG_LOG_ASSERT_RETURN(idx == 0, Steinberg_kInvalidArgument);

    memcpy(info->cid, cplug_tuid_component, 16);
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    // Setting this to anything other than "Audio Module Class" will fail Ableton 10s validation
    strncpy(info->category, "Audio Module Class", sizeof(info->category) - 1);
    strncpy(info->name, CPLUG_PLUGIN_NAME, sizeof(info->name) - 1);

    return Steinberg_kResultOk;
}

Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_createInstance(void* self, const Steinberg_TUID class_id, const Steinberg_TUID iid, void** const instance)
{
    cplug_log(
        "VST3Factory_createInstance => %p %s %s %p",
        self,
        _cplug_tuid2str(class_id),
        _cplug_tuid2str(iid),
        instance);
    if (tuid_match(class_id, cplug_tuid_component) &&
        (tuid_match(iid, Steinberg_Vst_IComponent_iid) || tuid_match(iid, Steinberg_FUnknown_iid)))
    {
        VST3Plugin* vst3                 = (VST3Plugin*)calloc(1, sizeof(VST3Plugin));
        vst3->hostContext.type           = CPLUG_PLUGIN_IS_VST3;
        vst3->hostContext.sendParamEvent = _cplug_sendParamEvent;
        vst3->component.lpVtbl           = &vst3->component.base;
        vst3->component.refcounter       = 1;
        // Steinberg_FUnknown
        vst3->component.base.queryInterface = VST3Component_queryInterface;
        vst3->component.base.addRef         = VST3Component_addRef;
        vst3->component.base.release        = VST3Component_release;
        // Steinberg_IPluginBase
        vst3->component.base.initialize = VST3Component_initialize;
        vst3->component.base.terminate  = VST3Component_terminate;
        // Steinberg_Vst_IComponent
        vst3->component.base.getControllerClassId = VST3Component_getControllerClassId;
        vst3->component.base.setIoMode            = VST3Component_setIoMode;
        vst3->component.base.getBusCount          = VST3Component_getBusCount;
        vst3->component.base.getBusInfo           = VST3Component_getBusInfo;
        vst3->component.base.getRoutingInfo       = VST3Component_getRoutingInfo;
        vst3->component.base.activateBus          = VST3Component_activateBus;
        vst3->component.base.setActive            = VST3Component_setActive;
        vst3->component.base.setState             = VST3Component_setState;
        vst3->component.base.getState             = VST3Component_getState;

        vst3->controller.lpVtbl     = &vst3->controller.base;
        vst3->controller.refcounter = 1;
        // Steinberg_FUnknown
        vst3->controller.base.queryInterface = VST3Controller_queryInterface;
        vst3->controller.base.addRef         = VST3Controller_addRef;
        vst3->controller.base.release        = VST3Controller_release;
        // Steinberg_IPluginBase
        vst3->controller.base.initialize = VST3Controller_initialize;
        vst3->controller.base.terminate  = VST3Controller_terminate;
        // Steinberg_Vst_IEditController
        vst3->controller.base.setComponentState      = VST3Controller_setComponentState;
        vst3->controller.base.setState               = VST3Controller_setState;
        vst3->controller.base.getState               = VST3Controller_getState;
        vst3->controller.base.getParameterCount      = VST3Controller_getParameterCount;
        vst3->controller.base.getParameterInfo       = VST3Controller_getParameterInfo;
        vst3->controller.base.getParamStringByValue  = VST3Controller_getParamStringByValue;
        vst3->controller.base.getParamValueByString  = VST3Controller_getParamValueByString;
        vst3->controller.base.normalizedParamToPlain = VST3Controller_normalizedParamToPlain;
        vst3->controller.base.plainParamToNormalized = VST3Controller_plainParamToNormalised;
        vst3->controller.base.getParamNormalized     = VST3Controller_getParamNormalized;
        vst3->controller.base.setParamNormalized     = VST3Controller_setParamNormalized;
        vst3->controller.base.setComponentHandler    = VST3Controller_setComponentHandler;
        vst3->controller.base.createView             = VST3Controller_createView;

        vst3->midiMapping.lpVtbl     = &vst3->midiMapping.base;
        vst3->midiMapping.refcounter = 1;
        // Steinberg_FUnknown
        vst3->midiMapping.base.queryInterface = VST3MidiMapping_queryInterface;
        vst3->midiMapping.base.addRef         = VST3MidiMapping_addRef;
        vst3->midiMapping.base.release        = VST3MidiMapping_release;
        // Steinberg_Vst_IMidiMapping
        vst3->midiMapping.base.getMidiControllerAssignment = VST3MidiMapping_getMidiControllerAssignment;

        vst3->noteExpression.lpVtbl     = &vst3->noteExpression.base;
        vst3->noteExpression.refcounter = 1;
        // Steinberg_FUnknown
        vst3->noteExpression.base.queryInterface = VST3NoteExpression_queryInterface;
        vst3->noteExpression.base.addRef         = VST3NoteExpression_addRef;
        vst3->noteExpression.base.release        = VST3NoteExpression_release;
        // Steinberg_Vst_INoteExpressionController
        vst3->noteExpression.base.getNoteExpressionCount         = VST3NoteExpression_getNoteExpressionCount;
        vst3->noteExpression.base.getNoteExpressionInfo          = VST3NoteExpression_getNoteExpressionInfo;
        vst3->noteExpression.base.getNoteExpressionStringByValue = VST3NoteExpression_getNoteExpressionStringByValue;
        vst3->noteExpression.base.getNoteExpressionValueByString = VST3NoteExpression_getNoteExpressionValueByString;

        vst3->processor.lpVtbl     = &vst3->processor.base;
        vst3->processor.refcounter = 1;
        // Steinberg_FUnknown
        vst3->processor.base.queryInterface = VST3Processor_queryInterface;
        vst3->processor.base.addRef         = VST3Processor_addRef;
        vst3->processor.base.release        = VST3Processor_release;
        // Steinberg_Vst_IAudioProcessor
        vst3->processor.base.setBusArrangements   = VST3Processor_setBusArrangements;
        vst3->processor.base.getBusArrangement    = VST3Processor_getBusArrangement;
        vst3->processor.base.canProcessSampleSize = VST3Processor_canProcessSampleSize;
        vst3->processor.base.getLatencySamples    = VST3Processor_getLatencySamples;
        vst3->processor.base.setupProcessing      = VST3Processor_setupProcessing;
        vst3->processor.base.setProcessing        = VST3Processor_setProcessing;
        vst3->processor.base.process              = VST3Processor_process;
        vst3->processor.base.getTailSamples       = VST3Processor_getTailSamples;

        *instance = &vst3->component;
        return Steinberg_kResultOk;
    }

    return Steinberg_kNoInterface;
}
// Steinberg_IPluginFactory2
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_getClassInfo2(void* self, const int32_t idx, struct Steinberg_PClassInfo2* const info)
{
    cplug_log("VST3Factory_getClassInfo2 => %i %p", idx, info);
    memset(info, 0, sizeof(*info));
    CPLUG_LOG_ASSERT_RETURN(idx == 0, Steinberg_kInvalidArgument);

    memcpy(info->cid, cplug_tuid_component, 16);
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    strncpy(info->category, "Audio Module Class", sizeof(info->category) - 1);
    strncpy(info->subCategories, CPLUG_VST3_CATEGORIES, sizeof(info->subCategories) - 1);
    strncpy(info->name, CPLUG_PLUGIN_NAME, sizeof(info->name) - 1);
    info->classFlags = Steinberg_Vst_ComponentFlags_kSimpleModeSupported;
    strncpy(info->vendor, CPLUG_COMPANY_NAME, sizeof(info->vendor) - 1);
    strncpy(info->version, CPLUG_PLUGIN_VERSION, sizeof(info->version) - 1);
    strncpy(info->sdkVersion, Steinberg_Vst_SDKVersionString, sizeof(info->sdkVersion) - 1);

    return Steinberg_kResultOk;
}
// Steinberg_IPluginFactory3
Steinberg_tresult SMTG_STDMETHODCALLTYPE
VST3Factory_getClassInfoUnicode(void* self, const int32_t idx, struct Steinberg_PClassInfoW* const info)
{
    cplug_log("VST3Factory_getClassInfoUnicode => %i %p", idx, info);
    memset(info, 0, sizeof(*info));
    CPLUG_LOG_ASSERT_RETURN(idx == 0, Steinberg_kInvalidArgument);

    memcpy(info->cid, cplug_tuid_component, 16);
    info->cardinality = Steinberg_PClassInfo_ClassCardinality_kManyInstances;
    strncpy(info->category, "Audio Module Class", sizeof(info->category) - 1);
    strncpy(info->subCategories, CPLUG_VST3_CATEGORIES, sizeof(info->subCategories) - 1);
    _cplug_utf8To16(info->name, CPLUG_PLUGIN_NAME, 64);
    info->classFlags = Steinberg_Vst_ComponentFlags_kSimpleModeSupported;
    _cplug_utf8To16(info->vendor, CPLUG_COMPANY_NAME, 64);
    _cplug_utf8To16(info->version, CPLUG_PLUGIN_VERSION, 64);
    _cplug_utf8To16(info->sdkVersion, Steinberg_Vst_SDKVersionString, 64);

    return Steinberg_kResultOk;
}

Steinberg_tresult SMTG_STDMETHODCALLTYPE VST3Factory_setHostContext(void* const self, Steinberg_FUnknown* const context)
{
    cplug_log("VST3Factory_setHostContext => %p %p", self, context);
    VST3Factory* const factory = (VST3Factory*)self;

    if (factory->host != NULL)
    {
        factory->host->lpVtbl->release(factory->host);
        factory->host = NULL;
    }

    if (factory->host == NULL && context != NULL)
        ((Steinberg_FUnknown*)context)
            ->lpVtbl
            ->queryInterface((Steinberg_FUnknown*)context, Steinberg_Vst_IHostApplication_iid, (void**)&factory->host);

    return Steinberg_kResultOk;
}

// --------------------------------------------------------------------------------------------------------------------
// Entry point

#if defined(_WIN32)
#define CPLUG_VST3_EXPORT __declspec(dllexport)
#define VST3_ENTRY        InitDll
#define VST3_ENTRY_ARGS   void
#define VST3_EXIT         ExitDll
#else
#define CPLUG_VST3_EXPORT __attribute__((visibility("default")))

#if defined(__APPLE__)
#define VST3_ENTRY      bundleEntry
#define VST3_ENTRY_ARGS void* _ptr
#define VST3_EXIT       bundleExit
#else
#define VST3_ENTRY      ModuleEntry
#define VST3_ENTRY_ARGS void* _ptr
#define VST3_EXIT       ModuleExit
#endif

#endif

CPLUG_VST3_EXPORT
const void* GetPluginFactory(void)
{
    VST3Factory* factory = (VST3Factory*)malloc(sizeof(VST3Factory));
    factory->lpVtbl      = &factory->base;
    // Steinberg_FUnknown
    factory->base.queryInterface = VST3Factory_queryInterface;
    factory->base.addRef         = VST3Factory_addRef;
    factory->base.release        = VST3Factory_release;
    // Steinberg_IPluginFactory
    factory->base.getFactoryInfo = VST3Factory_getFactoryInfo;
    factory->base.countClasses   = VST3Factory_countClasses;
    factory->base.getClassInfo   = VST3Factory_getClassInfo;
    factory->base.createInstance = VST3Factory_createInstance;
    // Steinberg_IPluginFactory2
    factory->base.getClassInfo2 = VST3Factory_getClassInfo2;
    // Steinberg_IPluginFactory3
    factory->base.getClassInfoUnicode = VST3Factory_getClassInfoUnicode;
    factory->base.setHostContext      = VST3Factory_setHostContext;

    factory->refcounter = 1;
    factory->host       = NULL;
    return factory;
}

CPLUG_VST3_EXPORT
bool VST3_ENTRY(VST3_ENTRY_ARGS)
{
    cplug_log("Bundle entry");
    cplug_libraryLoad();

    g_vst3ProcessContext.lpVtbl              = &g_vst3ProcessContext.base;
    g_vst3ProcessContext.base.queryInterface = VST3ProcessContextRequirements_queryInterface;
    g_vst3ProcessContext.base.addRef         = VST3ProcessContextRequirements_addRef;
    g_vst3ProcessContext.base.release        = VST3ProcessContextRequirements_release;
    g_vst3ProcessContext.base.getProcessContextRequirements =
        VST3ProcessContextRequirements_getProcessContextRequirements;

    return true;
}

CPLUG_VST3_EXPORT
bool VST3_EXIT(void)
{
    cplug_log("Bundle exit");
    cplug_libraryUnload();
    return true;
}