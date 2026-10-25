/*
 * s9batt: where the battery information stops on the Galaxy S9 iOS port.
 * Prints, in the order the information travels:
 *   1. the kernel's last MAX77705 reading (sysctl kern.sweet_battery) and the
 *      charging switch (kern.sweet_charge);
 *   2. the IOPMPowerSource the kernel publishes (SweetBattery, IORegistry);
 *   3. what powerd made of it (IOPSCopyPowerSourcesInfo, the dictionary
 *      SpringBoard's BatteryCenter reads: "Is Charging", "Power Source State",
 *      "Show Charging UI"...), and the adapter details;
 * With -w SECONDS it then waits that long and prints every power source
 * notification (plug and unplug the cable meanwhile) with the new summary.
 *
 *   s9batt [-w SECONDS]
 *
 * Read only. The iOS SDK ships IOKit without headers, so the few
 * declarations needed are below. Link with -framework IOKit.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <notify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

typedef mach_port_t io_object_t;
typedef io_object_t io_service_t;
extern CFMutableDictionaryRef IOServiceMatching(const char *name);
extern io_service_t IOServiceGetMatchingService(mach_port_t mainPort, CFDictionaryRef matching);
extern kern_return_t IORegistryEntryCreateCFProperties(io_object_t entry, CFMutableDictionaryRef *properties,
    CFAllocatorRef allocator, uint32_t options);
extern kern_return_t IOObjectRelease(io_object_t object);
/* The power source calls are SPI on iOS (maybe absent from the SDK's
 * IOKit.tbd), so they are looked up at run time in the loaded IOKit. */
static CFTypeRef (*IOPSCopyPowerSourcesInfo)(void);
static CFArrayRef (*IOPSCopyPowerSourcesList)(CFTypeRef blob);
static CFDictionaryRef (*IOPSGetPowerSourceDescription)(CFTypeRef blob, CFTypeRef ps);
static CFStringRef (*IOPSGetProvidingPowerSourceType)(CFTypeRef snapshot);
static CFDictionaryRef (*IOPSCopyExternalPowerAdapterDetails)(void);

static int
load_ps(void)
{
	IOPSCopyPowerSourcesInfo = (CFTypeRef (*)(void))dlsym(RTLD_DEFAULT, "IOPSCopyPowerSourcesInfo");
	IOPSCopyPowerSourcesList = (CFArrayRef (*)(CFTypeRef))dlsym(RTLD_DEFAULT, "IOPSCopyPowerSourcesList");
	IOPSGetPowerSourceDescription = (CFDictionaryRef (*)(CFTypeRef, CFTypeRef))dlsym(RTLD_DEFAULT,
	    "IOPSGetPowerSourceDescription");
	IOPSGetProvidingPowerSourceType = (CFStringRef (*)(CFTypeRef))dlsym(RTLD_DEFAULT,
	    "IOPSGetProvidingPowerSourceType");
	IOPSCopyExternalPowerAdapterDetails = (CFDictionaryRef (*)(void))dlsym(RTLD_DEFAULT,
	    "IOPSCopyExternalPowerAdapterDetails");
	return IOPSCopyPowerSourcesInfo && IOPSCopyPowerSourcesList && IOPSGetPowerSourceDescription &&
	       IOPSGetProvidingPowerSourceType && IOPSCopyExternalPowerAdapterDetails ? 0 : -1;
}

static void
print_cf(const char *indent, CFTypeRef obj)
{
	if (obj == NULL) {
		printf("%s(none)\n", indent);
		return;
	}
	CFStringRef desc = CFCopyDescription(obj);
	char buf[16384];
	if (desc && CFStringGetCString(desc, buf, sizeof(buf), kCFStringEncodingUTF8)) {
		/* indent every line */
		char *line = buf, *nl;
		while ((nl = strchr(line, '\n')) != NULL) {
			*nl = 0;
			printf("%s%s\n", indent, line);
			line = nl + 1;
		}
		printf("%s%s\n", indent, line);
	}
	if (desc) {
		CFRelease(desc);
	}
}

static void
print_value(CFDictionaryRef d, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFTypeRef v = CFDictionaryGetValue(d, k);
	CFRelease(k);
	char buf[256] = "(missing)";
	if (v && CFGetTypeID(v) == CFBooleanGetTypeID()) {
		snprintf(buf, sizeof(buf), "%s", CFBooleanGetValue(v) ? "true" : "false");
	} else if (v && CFGetTypeID(v) == CFNumberGetTypeID()) {
		long long n = 0;
		CFNumberGetValue(v, kCFNumberLongLongType, &n);
		snprintf(buf, sizeof(buf), "%lld", n);
	} else if (v && CFGetTypeID(v) == CFStringGetTypeID()) {
		CFStringGetCString(v, buf, sizeof(buf), kCFStringEncodingUTF8);
	} else if (v) {
		snprintf(buf, sizeof(buf), "(other type)");
	}
	printf("    %-28s %s\n", key, buf);
}

static void
kernel_part(void)
{
	char reading[512] = "";
	size_t len = sizeof(reading);
	int charge = -1;
	size_t clen = sizeof(charge);
	printf("1. kernel\n");
	if (sysctlbyname("kern.sweet_battery", reading, &len, NULL, 0) == 0) {
		printf("    kern.sweet_battery: %s\n", reading);
	} else {
		printf("    kern.sweet_battery: (not in this kernel)\n");
	}
	if (sysctlbyname("kern.sweet_charge", &charge, &clen, NULL, 0) == 0) {
		printf("    kern.sweet_charge: %d (0 untouched, 1 charging on, 2 off)\n", charge);
	}
}

static void
registry_part(int full)
{
	static const char *keys[] = {
		"ExternalConnected", "AppleRawExternalConnected", "ExternalChargeCapable", "IsCharging",
		"FullyCharged", "BatteryInstalled", "CurrentCapacity", "MaxCapacity", "AppleRawCurrentCapacity",
		"AppleRawMaxCapacity", "DesignCapacity", "Voltage", "Amperage", "InstantAmperage",
		"TimeRemaining", "AvgTimeToEmpty", "AvgTimeToFull", "CycleCount", "Temperature",
		"AtCriticalLevel", "AtWarnLevel", "SweetBatteryHardwareSampleValid", "SweetBatteryCurrentValid",
		"SweetBatteryFallback", "SweetBatteryPercent",
	};
	printf("2. kernel's power source (IORegistry IOPMPowerSource)\n");
	io_service_t s = IOServiceGetMatchingService(MACH_PORT_NULL, IOServiceMatching("IOPMPowerSource"));
	if (s == MACH_PORT_NULL) {
		printf("    (no IOPMPowerSource registered: powerd has nothing to read)\n");
		return;
	}
	CFMutableDictionaryRef props = NULL;
	if (IORegistryEntryCreateCFProperties(s, &props, NULL, 0) == KERN_SUCCESS && props) {
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			print_value(props, keys[i]);
		}
		CFStringRef ak = CFSTR("AdapterDetails");
		if (CFDictionaryGetValue(props, ak)) {
			printf("    AdapterDetails:\n");
			print_cf("      ", CFDictionaryGetValue(props, ak));
		}
		if (full) {
			printf("    all properties:\n");
			print_cf("      ", props);
		}
		CFRelease(props);
	}
	IOObjectRelease(s);
}

static void
powerd_part(void)
{
	static const char *keys[] = {
		"Name", "Type", "Transport Type", "Power Source State", "Is Present", "Is Charging",
		"Is Charged", "Is Finishing Charge", "Show Charging UI", "Current Capacity", "Max Capacity",
		"Time to Empty", "Time to Full Charge", "Current", "Voltage", "Battery Provides Time Remaining",
		"Optimized Battery Charging Engaged", "Power Source ID", "Hardware Serial Number",
	};
	printf("3. powerd (IOPSCopyPowerSourcesInfo, what BatteryCenter reads)\n");
	if (load_ps() != 0) {
		printf("    (IOKit power source calls not found)\n");
		return;
	}
	CFTypeRef blob = IOPSCopyPowerSourcesInfo();
	if (blob == NULL) {
		printf("    (powerd gave nothing: not running, or no source accepted)\n");
		return;
	}
	CFStringRef providing = IOPSGetProvidingPowerSourceType(blob);
	char buf[128] = "(none)";
	if (providing) {
		CFStringGetCString(providing, buf, sizeof(buf), kCFStringEncodingUTF8);
	}
	printf("    providing power: %s\n", buf);
	CFArrayRef list = IOPSCopyPowerSourcesList(blob);
	CFIndex n = list ? CFArrayGetCount(list) : 0;
	printf("    %ld source(s)\n", (long)n);
	for (CFIndex i = 0; i < n; i++) {
		CFDictionaryRef d = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(list, i));
		if (d == NULL) {
			continue;
		}
		printf("   source %ld:\n", (long)i);
		for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
			print_value(d, keys[k]);
		}
	}
	if (list) {
		CFRelease(list);
	}
	CFRelease(blob);
	printf("    adapter details:\n");
	CFDictionaryRef ad = IOPSCopyExternalPowerAdapterDetails();
	print_cf("      ", ad);
	if (ad) {
		CFRelease(ad);
	}
}

int
main(int argc, char **argv)
{
	int wait = 0, full = 0, ch;
	while ((ch = getopt(argc, argv, "w:a")) != -1) {
		switch (ch) {
		case 'w': wait = atoi(optarg); break;
		case 'a': full = 1; break;
		default:
			fprintf(stderr, "usage: s9batt [-a (all registry properties)] [-w SECONDS]\n");
			return 2;
		}
	}
	kernel_part();
	registry_part(full);
	powerd_part();
	if (wait <= 0) {
		return 0;
	}

	static const char *names[] = {
		"com.apple.system.powersources",
		"com.apple.system.powersources.source",
		"com.apple.system.powersources.timeremaining",
		"com.apple.system.powersources.percent",
		"com.apple.system.powermanagement.poweradapter",
		"com.apple.system.powersources.criticallevel",
	};
	int tokens[6];
	for (int i = 0; i < 6; i++) {
		if (notify_register_check(names[i], &tokens[i]) != NOTIFY_STATUS_OK) {
			tokens[i] = -1;
		} else {
			int changed;
			notify_check(tokens[i], &changed);      /* the first check always says changed */
		}
	}
	printf("\nwatching power notifications for %d s (plug / unplug the cable now)\n", wait);
	fflush(stdout);
	time_t end = time(NULL) + wait;
	while (time(NULL) < end) {
		usleep(250000);
		int any = 0;
		for (int i = 0; i < 6; i++) {
			int changed = 0;
			if (tokens[i] >= 0 && notify_check(tokens[i], &changed) == NOTIFY_STATUS_OK && changed) {
				printf("[%ld] notification: %s\n", (long)(wait - (end - time(NULL))), names[i]);
				any = 1;
			}
		}
		if (any) {
			kernel_part();
			powerd_part();
			fflush(stdout);
		}
	}
	return 0;
}
