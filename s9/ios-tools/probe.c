/* ElleKit/injection probe: when this dylib is loaded into a process, it
 * appends the process name to a file. Deployed to /usr/lib/TweakInject/ with
 * a com.apple.UIKit filter, it proves whether tweak injection reaches
 * SpringBoard and apps on the Galaxy S9 port (no vmmap on device to check). */
#include <stdio.h>
#include <string.h>
extern const char *getprogname(void);
__attribute__((constructor))
static void s9probe_init(void) {
    const char *n = getprogname();
    FILE *f = fopen("/private/var/ios-s9/inject-proof.txt", "a");
    if (f) { fprintf(f, "loaded into: %s\n", n ? n : "(null)"); fclose(f); }
}
