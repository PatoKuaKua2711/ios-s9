/*
 * S9CAEnv: CoreAnimation debug settings for backboardd on the Galaxy S9 port.
 * QuartzCore reads its CA_* options only from the environment (init_debug:
 * getenv + atoi, once, on first use), and backboardd's launchd job has no
 * way to add variables without reloading it. This ElleKit tweak (filter:
 * backboardd) sets them from a text file while the process starts, before
 * QuartzCore is first used:
 *
 *   /var/mobile/Library/S9CAEnv/env.txt   one KEY=VALUE per line, # comments
 *     e.g. CA_PRINT_PERF=1
 *          X_LOG_FILE=/var/mobile/Library/S9CAEnv/ca.log
 *
 * No file = does nothing. Only CA_* and the two CoreAnimation log file
 * variables are accepted. No hooks: nothing but setenv. Restart backboardd
 * after changing the file. What it set goes to /var/mobile/Library/S9CAEnv/log.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef DIR
#define DIR "/var/mobile/Library/S9CAEnv/"
#endif

static int
allowed(const char *key)
{
	return strncmp(key, "CA_", 3) == 0 || strcmp(key, "X_LOG_FILE") == 0 ||
	       strcmp(key, "QUARTZCORE_LOG_FILE") == 0;
}

__attribute__((constructor)) static void
s9caenv_init(void)
{
	FILE *f = fopen(DIR "env.txt", "r");
	if (f == NULL) {
		return;
	}
	FILE *log = fopen(DIR "log.txt", "a");
	if (log) {
		fprintf(log, "%ld pid %d: reading env.txt\n", (long)time(NULL), getpid());
	}
	char line[512];
	int n = 0;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		char *end = strpbrk(p, "\r\n");
		if (end) {
			*end = 0;
		}
		char *eq = strchr(p, '=');
		if (*p == '#' || eq == NULL || eq == p) {
			continue;
		}
		*eq = 0;
		if (!allowed(p)) {
			if (log) {
				fprintf(log, "  ignored %s\n", p);
			}
			continue;
		}
		setenv(p, eq + 1, 1);
		n++;
		if (log) {
			fprintf(log, "  %s=%s\n", p, eq + 1);
		}
	}
	fclose(f);
	if (log) {
		fprintf(log, "  %d set\n", n);
		fclose(log);
	}
}
