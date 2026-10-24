/*
 * Galaxy S9: internet over the USB cable (kernel ue0, CDC-ECM, sweetusb=2).
 * configd never adopts ue0 (it is no IOKit IONetworkInterface, so sweet's
 * preferences.plist service was ignored), so publish the service state
 * directly, the way a VPN daemon does: IPMonitor then installs the default
 * route through 192.168.7.1 and the DNS servers, and withdraws both when
 * the kernel reports the link down (cable out, KEV_DL_LINK_OFF).
 * The PC shares its connection on 192.168.7.1 (s9/tools/usb-internet.sh).
 * SCDynamicStore is SPI on iOS: the functions are declared here.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <unistd.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern Boolean SCDynamicStoreSetValue(SCDynamicStoreRef, CFStringRef, CFPropertyListRef);

#define SERVICE "State:/Network/Service/S9-USB-Ethernet"

static CFArrayRef
one(const char *s)
{
	CFStringRef v = CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
	CFArrayRef a = CFArrayCreate(NULL, (const void **)&v, 1, &kCFTypeArrayCallBacks);
	CFRelease(v);
	return a;
}

int
main(void)
{
	SCDynamicStoreRef st = SCDynamicStoreCreate(NULL, CFSTR("s9net"), NULL, NULL);
	if (st == NULL) {
		fprintf(stderr, "s9net: no dynamic store\n");
		return 1;
	}
	CFMutableDictionaryRef ip = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(ip, CFSTR("Addresses"), one("192.168.7.2"));
	CFDictionarySetValue(ip, CFSTR("SubnetMasks"), one("255.255.255.0"));
	CFDictionarySetValue(ip, CFSTR("Router"), CFSTR("192.168.7.1"));
	CFDictionarySetValue(ip, CFSTR("InterfaceName"), CFSTR("ue0"));

	CFMutableDictionaryRef dns = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	const void *servers[2] = { CFSTR("1.1.1.1"), CFSTR("8.8.8.8") };
	CFArrayRef sa = CFArrayCreate(NULL, servers, 2, &kCFTypeArrayCallBacks);
	CFDictionarySetValue(dns, CFSTR("ServerAddresses"), sa);

	Boolean ok = SCDynamicStoreSetValue(st, CFSTR(SERVICE "/IPv4"), ip) &&
	    SCDynamicStoreSetValue(st, CFSTR(SERVICE "/DNS"), dns);
	printf("s9net: ue0 192.168.7.2 via 192.168.7.1, dns 1.1.1.1 8.8.8.8: %s\n", ok ? "published" : "FAILED");
	fflush(stdout);
	/* stay: launchd KeepAlive restarts us if configd ever drops the session */
	for (;;) {
		pause();
	}
}
