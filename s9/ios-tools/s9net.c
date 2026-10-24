/*
 * Galaxy S9: internet over the USB cable (kernel ue0, CDC-ECM, sweetusb=2).
 * configd never adopts ue0 (it is no IOKit IONetworkInterface, so sweet's
 * preferences.plist service was ignored). CLI tools (ping, apt, ntp) use the
 * route directly and work, but GUI apps (Safari, Sileo) gate on nw_path /
 * SCNetworkReachability, which only sees a network if configd ranks a
 * service as primary. So publish the service AND the global primary state
 * directly, the way a VPN daemon does, then report what configd concludes.
 * SCDynamicStore / SCNetworkReachability are public SystemConfiguration API.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <unistd.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern Boolean SCDynamicStoreSetValue(SCDynamicStoreRef, CFStringRef, CFPropertyListRef);
extern CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef, CFStringRef);

typedef const struct __SCNetworkReachability *SCNetworkReachabilityRef;
typedef uint32_t SCNetworkReachabilityFlags;
extern SCNetworkReachabilityRef SCNetworkReachabilityCreateWithName(CFAllocatorRef, const char *);
extern Boolean SCNetworkReachabilityGetFlags(SCNetworkReachabilityRef, SCNetworkReachabilityFlags *);

#define SERVICE_ID "S9-USB-Ethernet"
#define SERVICE "State:/Network/Service/" SERVICE_ID
#define IFACE "ue0"
#define ROUTER "192.168.7.1"
#define SELF "192.168.7.2"

static CFArrayRef
cfarray(const char *const *items, int n)
{
	CFStringRef v[4];
	for (int i = 0; i < n; i++) {
		v[i] = CFStringCreateWithCString(NULL, items[i], kCFStringEncodingUTF8);
	}
	CFArrayRef a = CFArrayCreate(NULL, (const void **)v, n, &kCFTypeArrayCallBacks);
	for (int i = 0; i < n; i++) {
		CFRelease(v[i]);
	}
	return a;
}

static void
setdict(SCDynamicStoreRef st, const char *key, CFDictionaryRef d)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	SCDynamicStoreSetValue(st, k, d);
	CFRelease(k);
}

static void
report(SCNetworkReachabilityRef r, const char *who)
{
	SCNetworkReachabilityFlags f = 0;
	Boolean ok = r && SCNetworkReachabilityGetFlags(r, &f);
	printf("s9net: reachability %s: %s flags=0x%x reachable=%d needs-connection=%d\n",
	    who, ok ? "ok" : "FAILED", f, (f & 2) ? 1 : 0, (f & 4) ? 1 : 0);
	fflush(stdout);
}

int
main(void)
{
	SCDynamicStoreRef st = SCDynamicStoreCreate(NULL, CFSTR("s9net"), NULL, NULL);
	if (st == NULL) {
		fprintf(stderr, "s9net: no dynamic store\n");
		return 1;
	}
	const char *one_self[] = { SELF };
	const char *one_mask[] = { "255.255.255.0" };
	const char *dns_servers[] = { "1.1.1.1", "8.8.8.8", "8.8.4.4" };

	/* Per-service IPv4 + DNS (the interface, address, router). */
	CFMutableDictionaryRef ip = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFArrayRef addrs = cfarray(one_self, 1), masks = cfarray(one_mask, 1);
	CFDictionarySetValue(ip, CFSTR("Addresses"), addrs);
	CFDictionarySetValue(ip, CFSTR("SubnetMasks"), masks);
	CFDictionarySetValue(ip, CFSTR("Router"), CFSTR(ROUTER));
	CFDictionarySetValue(ip, CFSTR("InterfaceName"), CFSTR(IFACE));

	CFMutableDictionaryRef dns = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFArrayRef servers = cfarray(dns_servers, 3);
	CFDictionarySetValue(dns, CFSTR("ServerAddresses"), servers);

	/* Interface link state, so IPMonitor/nwi treat ue0 as an active link. */
	CFMutableDictionaryRef link = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(link, CFSTR("Active"), kCFBooleanTrue);

	SCNetworkReachabilityRef before = SCNetworkReachabilityCreateWithName(NULL, "www.apple.com");
	report(before, "before");

	setdict(st, SERVICE "/IPv4", ip);
	setdict(st, SERVICE "/DNS", dns);
	setdict(st, "State:/Network/Interface/" IFACE "/Link", link);
	/*
	 * NB: do NOT force State:/Network/Global/IPv4 PrimaryService here. Flipping
	 * the primary network abruptly woke every network daemon at once and the
	 * CPU-only compositor missed SpringBoard's watchdog -> panic. Let IPMonitor
	 * rank the service; measure reachability and decide the real fix from data.
	 */
	printf("s9net: published service %s on %s via %s (no forced primary)\n", SERVICE_ID, IFACE, ROUTER);
	fflush(stdout);

	/* Give configd a moment, then report what it concluded. */
	for (int i = 0; i < 5; i++) {
		sleep(2);
		SCNetworkReachabilityRef r = SCNetworkReachabilityCreateWithName(NULL, "www.apple.com");
		report(r, "after");
		if (r) {
			CFRelease(r);
		}
	}
	/* stay: launchd KeepAlive restarts us if configd ever drops the session */
	for (;;) {
		pause();
	}
}
