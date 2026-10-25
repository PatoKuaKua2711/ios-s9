/*
 * S9Audio: an audio output for the Galaxy S9 iOS port that sends what iOS
 * plays to the PC over the USB cable, before any of the phone's own audio
 * hardware works (that sits behind the Qualcomm ADSP: see s9port's study).
 *
 * iOS 16's audio server (mediaserverd) loads the CoreAudio HAL drivers in
 * /System/Library/Audio/Plug-Ins/HAL (NAME.driver bundles); every one is an
 * AudioServerPlugIn (Info.plist type 443ABAB8-E7B3-491A-B985-BEB9187030DB),
 * the same user space driver model as macOS. This one publishes one output
 * device, 48 kHz stereo, with the USB transport type, so iOS's router
 * (VirtualAudio) can treat it like a USB DAC. Every mixed IO buffer goes,
 * as 16-bit PCM, in UDP datagrams to 192.168.7.1:5141 (the PC end of the
 * USB Ethernet link); tools/s9-audio-recv.py plays or records them.
 *
 * Objects: 1 = plug-in, 2 = device, 3 = output stream. No controls: the
 * router's software volume applies. The clock is mach_absolute_time.
 * The ABI and the property selectors are those of CoreAudio's
 * AudioServerPlugIn.h / AudioHardwareBase.h, declared here (the iOS SDK
 * does not ship those headers).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ---- CoreAudio declarations (AudioHardwareBase.h, AudioServerPlugIn.h) ---- */

#define FCC(a, b, c, d) ((UInt32)(((UInt32)(a) << 24) | ((UInt32)(b) << 16) | ((UInt32)(c) << 8) | (UInt32)(d)))

typedef UInt32 AudioObjectID;
typedef struct {
	UInt32 mSelector, mScope, mElement;
} S9PropertyAddress;
typedef struct {
	Float64 mMinimum, mMaximum;
} S9ValueRange;
typedef struct {
	Float64 mSampleRate;
	UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame;
	UInt32 mChannelsPerFrame, mBitsPerChannel, mReserved;
} S9StreamDescription;
typedef struct {
	S9StreamDescription mFormat;
	S9ValueRange mSampleRateRange;
} S9RangedDescription;
typedef struct {
	UInt32 mChannelLabel, mChannelFlags;
	Float32 mCoordinates[3];
} S9ChannelDescription;
typedef struct {
	UInt32 mChannelLayoutTag, mChannelBitmap, mNumberChannelDescriptions;
	S9ChannelDescription mChannelDescriptions[2];
} S9StereoLayout;
typedef struct {
	UInt32 mClientID;
	pid_t mProcessID;
	Boolean mIsNativeEndian;
	CFStringRef mBundleID;
} S9ClientInfo;

typedef struct S9HostInterface {
	OSStatus (*PropertiesChanged)(const struct S9HostInterface *host, AudioObjectID obj, UInt32 n,
	    const S9PropertyAddress *addresses);
	OSStatus (*CopyFromStorage)(const struct S9HostInterface *host, CFStringRef key, CFPropertyListRef *out);
	OSStatus (*WriteToStorage)(const struct S9HostInterface *host, CFStringRef key, CFPropertyListRef data);
	OSStatus (*DeleteFromStorage)(const struct S9HostInterface *host, CFStringRef key);
	OSStatus (*RequestDeviceConfigurationChange)(const struct S9HostInterface *host, AudioObjectID dev,
	    UInt64 action, void *info);
} S9HostInterface;
typedef const S9HostInterface *S9HostRef;

struct S9DriverInterface;
typedef struct S9DriverInterface **S9DriverRef;
typedef struct S9DriverInterface {
	void *_reserved;
	HRESULT (*QueryInterface)(void *driver, REFIID uuid, LPVOID *out);
	ULONG (*AddRef)(void *driver);
	ULONG (*Release)(void *driver);
	OSStatus (*Initialize)(S9DriverRef driver, S9HostRef host);
	OSStatus (*CreateDevice)(S9DriverRef driver, CFDictionaryRef desc, const S9ClientInfo *client, AudioObjectID *out);
	OSStatus (*DestroyDevice)(S9DriverRef driver, AudioObjectID dev);
	OSStatus (*AddDeviceClient)(S9DriverRef driver, AudioObjectID dev, const S9ClientInfo *client);
	OSStatus (*RemoveDeviceClient)(S9DriverRef driver, AudioObjectID dev, const S9ClientInfo *client);
	OSStatus (*PerformDeviceConfigurationChange)(S9DriverRef driver, AudioObjectID dev, UInt64 action, void *info);
	OSStatus (*AbortDeviceConfigurationChange)(S9DriverRef driver, AudioObjectID dev, UInt64 action, void *info);
	Boolean (*HasProperty)(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a);
	OSStatus (*IsPropertySettable)(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a,
	    Boolean *out);
	OSStatus (*GetPropertyDataSize)(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a,
	    UInt32 qsize, const void *qdata, UInt32 *out);
	OSStatus (*GetPropertyData)(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a,
	    UInt32 qsize, const void *qdata, UInt32 size, UInt32 *outSize, void *out);
	OSStatus (*SetPropertyData)(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a,
	    UInt32 qsize, const void *qdata, UInt32 size, const void *data);
	OSStatus (*StartIO)(S9DriverRef driver, AudioObjectID dev, UInt32 client);
	OSStatus (*StopIO)(S9DriverRef driver, AudioObjectID dev, UInt32 client);
	OSStatus (*GetZeroTimeStamp)(S9DriverRef driver, AudioObjectID dev, UInt32 client, Float64 *sampleTime,
	    UInt64 *hostTime, UInt64 *seed);
	OSStatus (*WillDoIOOperation)(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op,
	    Boolean *willDo, Boolean *inPlace);
	OSStatus (*BeginIOOperation)(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 frames,
	    const void *cycle);
	OSStatus (*DoIOOperation)(S9DriverRef driver, AudioObjectID dev, AudioObjectID stream, UInt32 client,
	    UInt32 op, UInt32 frames, const void *cycle, void *mainBuffer, void *secondaryBuffer);
	OSStatus (*EndIOOperation)(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 frames,
	    const void *cycle);
} S9DriverInterface;

enum {
	kNoErr = 0,
	kErrUnspecified = FCC('w', 'h', 'a', 't'),
	kErrUnknownProperty = FCC('w', 'h', 'o', '?'),
	kErrBadSize = FCC('!', 's', 'i', 'z'),
	kErrIllegal = FCC('n', 'o', 'p', 'e'),
	kErrBadObject = FCC('!', 'o', 'b', 'j'),
	kErrBadDevice = FCC('!', 'd', 'e', 'v'),
	kErrUnsupported = FCC('u', 'n', 'o', 'p'),
	kErrFormat = FCC('!', 'd', 'a', 't'),
};

enum {
	kScopeGlobal = FCC('g', 'l', 'o', 'b'),
	kScopeInput = FCC('i', 'n', 'p', 't'),
	kScopeOutput = FCC('o', 'u', 't', 'p'),
	kClassObject = FCC('a', 'o', 'b', 'j'),
	kClassPlugIn = FCC('a', 'p', 'l', 'g'),
	kClassDevice = FCC('a', 'd', 'e', 'v'),
	kClassStream = FCC('a', 's', 't', 'r'),
	/* every object */
	kPropBaseClass = FCC('b', 'c', 'l', 's'),
	kPropClass = FCC('c', 'l', 'a', 's'),
	kPropOwner = FCC('s', 't', 'd', 'v'),
	kPropName = FCC('l', 'n', 'a', 'm'),
	kPropModelName = FCC('l', 'm', 'o', 'd'),
	kPropManufacturer = FCC('l', 'm', 'a', 'k'),
	kPropOwnedObjects = FCC('o', 'w', 'n', 'd'),
	kPropControlList = FCC('c', 't', 'r', 'l'),
	/* plug-in */
	kPropDeviceList = FCC('d', 'e', 'v', '#'),
	kPropTranslateUIDToDevice = FCC('u', 'i', 'd', 'd'),
	kPropResourceBundle = FCC('r', 's', 'r', 'c'),
	/* device */
	kPropDeviceUID = FCC('u', 'i', 'd', ' '),
	kPropModelUID = FCC('m', 'u', 'i', 'd'),
	kPropTransportType = FCC('t', 'r', 'a', 'n'),
	kPropRelatedDevices = FCC('a', 'k', 'i', 'n'),
	kPropClockDomain = FCC('c', 'l', 'k', 'd'),
	kPropIsAlive = FCC('l', 'i', 'v', 'n'),
	kPropIsRunning = FCC('g', 'o', 'i', 'n'),
	kPropCanBeDefault = FCC('d', 'f', 'l', 't'),
	kPropCanBeDefaultSystem = FCC('s', 'f', 'l', 't'),
	kPropLatency = FCC('l', 't', 'n', 'c'),
	kPropStreams = FCC('s', 't', 'm', '#'),
	kPropSafetyOffset = FCC('s', 'a', 'f', 't'),
	kPropNominalSampleRate = FCC('n', 's', 'r', 't'),
	kPropAvailableSampleRates = FCC('n', 's', 'r', '#'),
	kPropIsHidden = FCC('h', 'i', 'd', 'n'),
	kPropPreferredStereo = FCC('d', 'c', 'h', '2'),
	kPropPreferredLayout = FCC('s', 'r', 'n', 'd'),
	kPropZeroTimeStampPeriod = FCC('r', 'i', 'n', 'g'),
	/* stream */
	kPropIsActive = FCC('s', 'a', 'c', 't'),
	kPropDirection = FCC('s', 'd', 'i', 'r'),
	kPropTerminalType = FCC('t', 'e', 'r', 'm'),
	kPropStartingChannel = FCC('s', 'c', 'h', 'n'),
	kPropVirtualFormat = FCC('s', 'f', 'm', 't'),
	kPropAvailableVirtualFormats = FCC('s', 'f', 'm', 'a'),
	kPropPhysicalFormat = FCC('p', 'f', 't', ' '),
	kPropAvailablePhysicalFormats = FCC('p', 'f', 't', 'a'),
	/* values */
	kTransportUSB = FCC('u', 's', 'b', ' '),
	kTerminalSpeaker = FCC('s', 'p', 'k', 'r'),
	kFormatLinearPCM = FCC('l', 'p', 'c', 'm'),
	kOpWriteMix = FCC('w', 'm', 'i', 'x'),
};

#define kFormatFlagsFloat       (1u | 8u)       /* IsFloat | IsPacked, native endian */
#define kObjPlugIn              1
#define kObjDevice              2
#define kObjStream              3
#define kRate                   48000.0
#define kChannels               2
#define kRingFrames             16384           /* zero time stamp period */
#define kMaxPacketFrames        340             /* 16 + 340 * 4 = 1376 bytes: one Ethernet frame */
#define kDestPort               5141

static CFStringRef const kDeviceUID = CFSTR("S9Audio_Output");
static CFStringRef const kModelUID = CFSTR("S9Audio_Model");

/* ---- state ---- */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static UInt32 g_refs;
static S9HostRef g_host;
static UInt32 g_running;                /* StartIO calls without StopIO */
static UInt64 g_anchor;                 /* host time of sample 0 */
static Float64 g_ticks_per_frame;
static int g_sock = -1;
static UInt32 g_seq;
static struct sockaddr_in g_dest;

static S9DriverInterface g_interface;
static S9DriverInterface *g_interface_ptr = &g_interface;
static S9DriverRef const g_driver = &g_interface_ptr;

static const S9StreamDescription g_format = {
	kRate, kFormatLinearPCM, kFormatFlagsFloat, 8, 1, 8, kChannels, 32, 0,
};

static void
changed(AudioObjectID obj, UInt32 selector)
{
	if (g_host != NULL) {
		S9PropertyAddress a = { selector, kScopeGlobal, 0 };
		g_host->PropertiesChanged(g_host, obj, 1, &a);
	}
}

/* ---- IUnknown ---- */

static HRESULT
s9_query(void *driver, REFIID uuid, LPVOID *out)
{
	if (driver != (void *)g_driver || out == NULL) {
		return E_POINTER;
	}
	CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, uuid);
	if (requested == NULL) {
		return E_NOINTERFACE;
	}
	CFUUIDRef unknown = CFUUIDGetConstantUUIDWithBytes(NULL, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
	CFUUIDRef plugin = CFUUIDGetConstantUUIDWithBytes(NULL, 0xEE, 0xA5, 0x77, 0x3D, 0xCC, 0x43, 0x49, 0xF1,
	    0x8E, 0x00, 0x8F, 0x96, 0xE7, 0xD2, 0x3B, 0x17);     /* kAudioServerPlugInDriverInterfaceUUID */
	bool ok = CFEqual(requested, unknown) || CFEqual(requested, plugin);
	CFRelease(requested);
	if (!ok) {
		*out = NULL;
		return E_NOINTERFACE;
	}
	pthread_mutex_lock(&g_lock);
	g_refs++;
	pthread_mutex_unlock(&g_lock);
	*out = (LPVOID)g_driver;
	return S_OK;
}

static ULONG
s9_addref(void *driver)
{
	if (driver != (void *)g_driver) {
		return 0;
	}
	pthread_mutex_lock(&g_lock);
	ULONG n = ++g_refs;
	pthread_mutex_unlock(&g_lock);
	return n;
}

static ULONG
s9_release(void *driver)
{
	if (driver != (void *)g_driver) {
		return 0;
	}
	pthread_mutex_lock(&g_lock);
	ULONG n = g_refs ? --g_refs : 0;
	pthread_mutex_unlock(&g_lock);
	return n;   /* static driver: nothing to free */
}

/* ---- life cycle ---- */

static OSStatus
s9_initialize(S9DriverRef driver, S9HostRef host)
{
	if (driver != g_driver) {
		return kErrBadObject;
	}
	g_host = host;
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	g_ticks_per_frame = (1e9 * (Float64)tb.denom / (Float64)tb.numer) / kRate;
	memset(&g_dest, 0, sizeof(g_dest));
	g_dest.sin_len = sizeof(g_dest);
	g_dest.sin_family = AF_INET;
	g_dest.sin_port = htons(kDestPort);
	g_dest.sin_addr.s_addr = htonl(0xc0a80701);    /* 192.168.7.1 */
	return kNoErr;
}

static OSStatus
s9_create_device(S9DriverRef driver, CFDictionaryRef desc, const S9ClientInfo *client, AudioObjectID *out)
{
	(void)driver; (void)desc; (void)client; (void)out;
	return kErrUnsupported;
}

static OSStatus
s9_destroy_device(S9DriverRef driver, AudioObjectID dev)
{
	(void)driver; (void)dev;
	return kErrUnsupported;
}

static OSStatus
s9_client(S9DriverRef driver, AudioObjectID dev, const S9ClientInfo *client)
{
	(void)client;
	return driver != g_driver ? kErrBadObject : dev != kObjDevice ? kErrBadDevice : kNoErr;
}

static OSStatus
s9_config(S9DriverRef driver, AudioObjectID dev, UInt64 action, void *info)
{
	(void)action; (void)info;
	return driver != g_driver ? kErrBadObject : dev != kObjDevice ? kErrBadDevice : kNoErr;
}

/* ---- properties ---- */

static Boolean
s9_has(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a)
{
	(void)pid;
	if (driver != g_driver || a == NULL) {
		return false;
	}
	switch (obj) {
	case kObjPlugIn:
		switch (a->mSelector) {
		case kPropBaseClass: case kPropClass: case kPropOwner: case kPropManufacturer:
		case kPropOwnedObjects: case kPropDeviceList: case kPropTranslateUIDToDevice: case kPropResourceBundle:
			return true;
		}
		return false;
	case kObjDevice:
		switch (a->mSelector) {
		case kPropBaseClass: case kPropClass: case kPropOwner: case kPropName: case kPropModelName:
		case kPropManufacturer: case kPropOwnedObjects: case kPropDeviceUID: case kPropModelUID:
		case kPropTransportType: case kPropRelatedDevices: case kPropClockDomain: case kPropIsAlive:
		case kPropIsRunning: case kPropStreams: case kPropControlList: case kPropNominalSampleRate:
		case kPropAvailableSampleRates: case kPropIsHidden: case kPropZeroTimeStampPeriod:
			return true;
		case kPropCanBeDefault: case kPropCanBeDefaultSystem: case kPropLatency: case kPropSafetyOffset:
		case kPropPreferredStereo: case kPropPreferredLayout:
			return a->mScope == kScopeOutput || a->mScope == kScopeGlobal;
		}
		return false;
	case kObjStream:
		switch (a->mSelector) {
		case kPropBaseClass: case kPropClass: case kPropOwner: case kPropOwnedObjects: case kPropIsActive:
		case kPropDirection: case kPropTerminalType: case kPropStartingChannel: case kPropLatency:
		case kPropVirtualFormat: case kPropPhysicalFormat: case kPropAvailableVirtualFormats:
		case kPropAvailablePhysicalFormats:
			return true;
		}
		return false;
	}
	return false;
}

static OSStatus
s9_settable(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a, Boolean *out)
{
	if (!s9_has(driver, obj, pid, a) || out == NULL) {
		return kErrUnknownProperty;
	}
	/* The rate and the formats can be "set" to the only value they have. */
	*out = (obj == kObjDevice && a->mSelector == kPropNominalSampleRate) ||
	    (obj == kObjStream && (a->mSelector == kPropVirtualFormat || a->mSelector == kPropPhysicalFormat));
	return kNoErr;
}

/* Size of a property; 0 = unknown. */
static UInt32
s9_size(AudioObjectID obj, const S9PropertyAddress *a)
{
	switch (a->mSelector) {
	case kPropBaseClass: case kPropClass: case kPropOwner: case kPropTransportType: case kPropClockDomain:
	case kPropIsAlive: case kPropIsRunning: case kPropCanBeDefault: case kPropCanBeDefaultSystem:
	case kPropLatency: case kPropSafetyOffset: case kPropIsHidden: case kPropZeroTimeStampPeriod:
	case kPropIsActive: case kPropDirection: case kPropTerminalType: case kPropStartingChannel:
	case kPropTranslateUIDToDevice:
		return sizeof(UInt32);
	case kPropName: case kPropModelName: case kPropManufacturer: case kPropDeviceUID: case kPropModelUID:
	case kPropResourceBundle:
		return sizeof(CFStringRef);
	case kPropDeviceList: case kPropRelatedDevices:
		return sizeof(AudioObjectID);
	case kPropOwnedObjects:
		return obj == kObjStream ? 0xffffffffu : sizeof(AudioObjectID);   /* stream: none */
	case kPropStreams:
		return a->mScope == kScopeInput ? 0xffffffffu : sizeof(AudioObjectID);
	case kPropControlList:
		return 0xffffffffu;
	case kPropNominalSampleRate:
		return sizeof(Float64);
	case kPropAvailableSampleRates:
		return sizeof(S9ValueRange);
	case kPropPreferredStereo:
		return 2 * sizeof(UInt32);
	case kPropPreferredLayout:
		return sizeof(S9StereoLayout);
	case kPropVirtualFormat: case kPropPhysicalFormat:
		return sizeof(S9StreamDescription);
	case kPropAvailableVirtualFormats: case kPropAvailablePhysicalFormats:
		return sizeof(S9RangedDescription);
	}
	return 0;
}

static OSStatus
s9_get_size(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a, UInt32 qsize,
    const void *qdata, UInt32 *out)
{
	(void)qsize; (void)qdata;
	if (!s9_has(driver, obj, pid, a) || out == NULL) {
		return kErrUnknownProperty;
	}
	UInt32 n = s9_size(obj, a);
	*out = n == 0xffffffffu ? 0 : n;
	return n == 0 ? kErrUnknownProperty : kNoErr;
}

#define PUT(type, value) do { \
	if (size < sizeof(type)) { return kErrBadSize; } \
	*(type *)out = (value); \
	*outSize = sizeof(type); \
	return kNoErr; \
} while (0)

static OSStatus
s9_get(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a, UInt32 qsize,
    const void *qdata, UInt32 size, UInt32 *outSize, void *out)
{
	if (!s9_has(driver, obj, pid, a) || outSize == NULL || out == NULL) {
		return kErrUnknownProperty;
	}
	switch (a->mSelector) {
	case kPropBaseClass:
		PUT(UInt32, kClassObject);
	case kPropClass:
		PUT(UInt32, obj == kObjPlugIn ? kClassPlugIn : obj == kObjDevice ? kClassDevice : kClassStream);
	case kPropOwner:
		PUT(AudioObjectID, obj == kObjPlugIn ? 0 : obj == kObjDevice ? kObjPlugIn : kObjDevice);
	case kPropName:
		PUT(CFStringRef, CFSTR("S9 Audio (PC)"));
	case kPropModelName:
		PUT(CFStringRef, CFSTR("S9 Audio over USB network"));
	case kPropManufacturer:
		PUT(CFStringRef, CFSTR("ios-s9"));
	case kPropResourceBundle:
		PUT(CFStringRef, CFSTR(""));
	case kPropOwnedObjects:
		if (obj == kObjStream) {
			*outSize = 0;
			return kNoErr;
		}
		PUT(AudioObjectID, obj == kObjPlugIn ? kObjDevice : kObjStream);
	case kPropDeviceList:
	case kPropRelatedDevices:
		PUT(AudioObjectID, kObjDevice);
	case kPropTranslateUIDToDevice: {
		CFStringRef uid = (qsize == sizeof(CFStringRef) && qdata != NULL) ? *(const CFStringRef *)qdata : NULL;
		PUT(AudioObjectID, uid != NULL && CFEqual(uid, kDeviceUID) ? kObjDevice : 0);
	}
	case kPropDeviceUID:
		PUT(CFStringRef, kDeviceUID);
	case kPropModelUID:
		PUT(CFStringRef, kModelUID);
	case kPropTransportType:
		PUT(UInt32, kTransportUSB);
	case kPropClockDomain:
		PUT(UInt32, 0);
	case kPropIsAlive:
		PUT(UInt32, 1);
	case kPropIsRunning: {
		pthread_mutex_lock(&g_lock);
		UInt32 running = g_running > 0;
		pthread_mutex_unlock(&g_lock);
		PUT(UInt32, running);
	}
	case kPropCanBeDefault:
	case kPropCanBeDefaultSystem:
		PUT(UInt32, 1);
	case kPropLatency:
	case kPropSafetyOffset:
		PUT(UInt32, 0);
	case kPropIsHidden:
		PUT(UInt32, 0);
	case kPropZeroTimeStampPeriod:
		PUT(UInt32, kRingFrames);
	case kPropStreams:
		if (a->mScope == kScopeInput) {
			*outSize = 0;
			return kNoErr;
		}
		PUT(AudioObjectID, kObjStream);
	case kPropControlList:
		*outSize = 0;
		return kNoErr;
	case kPropNominalSampleRate:
		PUT(Float64, kRate);
	case kPropAvailableSampleRates: {
		S9ValueRange r = { kRate, kRate };
		PUT(S9ValueRange, r);
	}
	case kPropPreferredStereo:
		if (size < 2 * sizeof(UInt32)) {
			return kErrBadSize;
		}
		((UInt32 *)out)[0] = 1;
		((UInt32 *)out)[1] = 2;
		*outSize = 2 * sizeof(UInt32);
		return kNoErr;
	case kPropPreferredLayout: {
		S9StereoLayout l;
		memset(&l, 0, sizeof(l));
		l.mChannelLayoutTag = 0;                /* kAudioChannelLayoutTag_UseChannelDescriptions */
		l.mNumberChannelDescriptions = 2;
		l.mChannelDescriptions[0].mChannelLabel = 1;    /* left */
		l.mChannelDescriptions[1].mChannelLabel = 2;    /* right */
		PUT(S9StereoLayout, l);
	}
	case kPropIsActive:
		PUT(UInt32, 1);
	case kPropDirection:
		PUT(UInt32, 0);         /* output */
	case kPropTerminalType:
		PUT(UInt32, kTerminalSpeaker);
	case kPropStartingChannel:
		PUT(UInt32, 1);
	case kPropVirtualFormat:
	case kPropPhysicalFormat:
		PUT(S9StreamDescription, g_format);
	case kPropAvailableVirtualFormats:
	case kPropAvailablePhysicalFormats: {
		S9RangedDescription d = { g_format, { kRate, kRate } };
		PUT(S9RangedDescription, d);
	}
	}
	return kErrUnknownProperty;
}

static OSStatus
s9_set(S9DriverRef driver, AudioObjectID obj, pid_t pid, const S9PropertyAddress *a, UInt32 qsize,
    const void *qdata, UInt32 size, const void *data)
{
	(void)qsize; (void)qdata;
	Boolean settable = false;
	if (s9_settable(driver, obj, pid, a, &settable) != kNoErr || !settable || data == NULL) {
		return kErrUnknownProperty;
	}
	if (a->mSelector == kPropNominalSampleRate) {
		if (size < sizeof(Float64)) {
			return kErrBadSize;
		}
		return *(const Float64 *)data == kRate ? kNoErr : kErrFormat;
	}
	if (size < sizeof(S9StreamDescription)) {
		return kErrBadSize;
	}
	const S9StreamDescription *f = data;
	return (f->mSampleRate == kRate && f->mFormatID == kFormatLinearPCM && f->mChannelsPerFrame == kChannels &&
	       f->mBitsPerChannel == 32 && (f->mFormatFlags & 1u)) ? kNoErr : kErrFormat;
}

/* ---- IO ---- */

static OSStatus
s9_start(S9DriverRef driver, AudioObjectID dev, UInt32 client)
{
	(void)client;
	if (driver != g_driver || dev != kObjDevice) {
		return kErrBadObject;
	}
	bool first = false;
	pthread_mutex_lock(&g_lock);
	if (g_running++ == 0) {
		first = true;
		g_anchor = mach_absolute_time();
		g_seq = 0;
		if (g_sock < 0) {
			g_sock = socket(AF_INET, SOCK_DGRAM, 0);
			if (g_sock >= 0) {
				fcntl(g_sock, F_SETFL, fcntl(g_sock, F_GETFL) | O_NONBLOCK);
			}
		}
	}
	pthread_mutex_unlock(&g_lock);
	if (first) {
		changed(kObjDevice, kPropIsRunning);
	}
	return kNoErr;
}

static OSStatus
s9_stop(S9DriverRef driver, AudioObjectID dev, UInt32 client)
{
	(void)client;
	if (driver != g_driver || dev != kObjDevice) {
		return kErrBadObject;
	}
	bool last = false;
	pthread_mutex_lock(&g_lock);
	if (g_running > 0 && --g_running == 0) {
		last = true;
		if (g_sock >= 0) {
			close(g_sock);
			g_sock = -1;
		}
	}
	pthread_mutex_unlock(&g_lock);
	if (last) {
		changed(kObjDevice, kPropIsRunning);
	}
	return kNoErr;
}

/* The clock: one zero time stamp every kRingFrames frames of host time since StartIO. */
static OSStatus
s9_zero(S9DriverRef driver, AudioObjectID dev, UInt32 client, Float64 *sampleTime, UInt64 *hostTime, UInt64 *seed)
{
	(void)client;
	if (driver != g_driver || dev != kObjDevice) {
		return kErrBadObject;
	}
	Float64 ticks_per_ring = g_ticks_per_frame * kRingFrames;
	UInt64 now = mach_absolute_time();
	UInt64 rings = now > g_anchor ? (UInt64)((Float64)(now - g_anchor) / ticks_per_ring) : 0;
	*sampleTime = (Float64)rings * kRingFrames;
	*hostTime = g_anchor + (UInt64)((Float64)rings * ticks_per_ring);
	*seed = 1;
	return kNoErr;
}

static OSStatus
s9_will_do(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, Boolean *willDo, Boolean *inPlace)
{
	(void)client;
	if (driver != g_driver || dev != kObjDevice) {
		return kErrBadObject;
	}
	*willDo = op == kOpWriteMix;
	*inPlace = true;
	return kNoErr;
}

static OSStatus
s9_begin_end(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 frames, const void *cycle)
{
	(void)client; (void)op; (void)frames; (void)cycle;
	return driver != g_driver || dev != kObjDevice ? kErrBadObject : kNoErr;
}

static inline int16_t
pcm16(float x)
{
	if (x >= 1.0f) {
		return 32767;
	}
	if (x <= -1.0f) {
		return -32768;
	}
	return (int16_t)(x * 32767.0f);
}

/* The mix: 16-bit stereo in datagrams of up to kMaxPacketFrames, with a 16-byte header. */
static OSStatus
s9_do(S9DriverRef driver, AudioObjectID dev, AudioObjectID stream, UInt32 client, UInt32 op, UInt32 frames,
    const void *cycle, void *mainBuffer, void *secondaryBuffer)
{
	(void)client; (void)cycle; (void)secondaryBuffer;
	if (driver != g_driver || dev != kObjDevice || stream != kObjStream) {
		return kErrBadObject;
	}
	if (op != kOpWriteMix || mainBuffer == NULL || g_sock < 0) {
		return kNoErr;
	}
	const float *in = mainBuffer;
	uint8_t packet[16 + kMaxPacketFrames * kChannels * 2];
	for (UInt32 done = 0; done < frames;) {
		UInt32 n = frames - done < kMaxPacketFrames ? frames - done : kMaxPacketFrames;
		uint32_t hdr[4] = { 0x55413953u /* "S9AU" */, g_seq++, (uint32_t)kRate, n | (kChannels << 16) | (16u << 24) };
		memcpy(packet, hdr, sizeof(hdr));
		int16_t *pcm = (int16_t *)(packet + 16);
		for (UInt32 i = 0; i < n * kChannels; i++) {
			pcm[i] = pcm16(in[(done * kChannels) + i]);
		}
		(void)sendto(g_sock, packet, 16 + n * kChannels * 2, 0, (const struct sockaddr *)&g_dest, sizeof(g_dest));
		done += n;
	}
	return kNoErr;
}

static OSStatus
s9_begin(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 frames, const void *cycle)
{
	return s9_begin_end(driver, dev, client, op, frames, cycle);
}

static OSStatus
s9_end(S9DriverRef driver, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 frames, const void *cycle)
{
	return s9_begin_end(driver, dev, client, op, frames, cycle);
}

static S9DriverInterface g_interface = {
	NULL,
	s9_query, s9_addref, s9_release,
	s9_initialize, s9_create_device, s9_destroy_device,
	s9_client, s9_client,
	s9_config, s9_config,
	s9_has, s9_settable, s9_get_size, s9_get, s9_set,
	s9_start, s9_stop, s9_zero,
	s9_will_do, s9_begin, s9_do, s9_end,
};

/* The factory named in Info.plist (CFPlugInFactories). */
__attribute__((visibility("default"))) void *S9Audio_Create(CFAllocatorRef allocator, CFUUIDRef type);

void *
S9Audio_Create(CFAllocatorRef allocator, CFUUIDRef type)
{
	(void)allocator;
	CFUUIDRef wanted = CFUUIDGetConstantUUIDWithBytes(NULL, 0x44, 0x3A, 0xBA, 0xB8, 0xE7, 0xB3, 0x49, 0x1A,
	    0xB9, 0x85, 0xBE, 0xB9, 0x18, 0x70, 0x30, 0xDB);     /* kAudioServerPlugInTypeUUID */
	if (type == NULL || !CFEqual(type, wanted)) {
		return NULL;
	}
	pthread_mutex_lock(&g_lock);
	g_refs++;
	pthread_mutex_unlock(&g_lock);
	return (void *)g_driver;
}
