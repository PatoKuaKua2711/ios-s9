/*
 * s9top: CPU use per core and per thread on the Galaxy S9 iOS port (iOS ships
 * no top). Every interval it prints how busy each of the 8 cores was (cores
 * 0-3 are the Cortex-A55s, 4-7 the A75s) and, for every process named on the
 * command line, its threads sorted by CPU time used in that interval, with
 * their names and scheduling priority.
 *
 *   s9top [-i SECONDS] [-n COUNT] [-t THREADS] NAME [NAME...]
 *
 * Read only: host_processor_info and proc_pidinfo (root sees every process).
 * The iOS SDK has no libproc.h, so the few declarations needed are below,
 * matching bsd/sys/proc_info.h of xnu-8796.
 */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/processor_info.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROC_PIDTHREADID64INFO  15
#define PROC_PIDLISTTHREADIDS   28
#define MAXTHREADNAMESIZE       64

struct proc_threadinfo {
	uint64_t pth_user_time;         /* ns */
	uint64_t pth_system_time;       /* ns */
	int32_t  pth_cpu_usage;
	int32_t  pth_policy;
	int32_t  pth_run_state;
	int32_t  pth_flags;
	int32_t  pth_sleep_time;
	int32_t  pth_curpri;
	int32_t  pth_priority;
	int32_t  pth_maxpriority;
	char     pth_name[MAXTHREADNAMESIZE];
};

extern int proc_listallpids(void *buffer, int buffersize);
extern int proc_name(int pid, void *buffer, uint32_t buffersize);
extern int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);

#define MAX_THREADS     512
#define MAX_PROCS       8

struct thr {
	uint64_t id, ns;
	int pri, curpri;
	char name[MAXTHREADNAMESIZE];
};

struct proc {
	const char *want;
	int pid;
	int n;
	struct thr t[MAX_THREADS];
};

static int
find_pid(const char *want)
{
	static int pids[4096];
	int n = proc_listallpids(pids, sizeof(pids)) ;
	for (int i = 0; i < n; i++) {
		char name[256] = "";
		if (pids[i] > 0 && proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, want) == 0) {
			return pids[i];
		}
	}
	return -1;
}

static void
sample_threads(struct proc *p)
{
	static uint64_t ids[MAX_THREADS];
	p->n = 0;
	if (p->pid <= 0) {
		p->pid = find_pid(p->want);
		if (p->pid <= 0) {
			return;
		}
	}
	int bytes = proc_pidinfo(p->pid, PROC_PIDLISTTHREADIDS, 0, ids, sizeof(ids));
	if (bytes <= 0) {
		p->pid = -1;            /* gone: look it up again next time */
		return;
	}
	for (int i = 0; i < bytes / (int)sizeof(uint64_t) && p->n < MAX_THREADS; i++) {
		struct proc_threadinfo ti;
		if (proc_pidinfo(p->pid, PROC_PIDTHREADID64INFO, ids[i], &ti, sizeof(ti)) != (int)sizeof(ti)) {
			continue;
		}
		struct thr *t = &p->t[p->n++];
		t->id = ids[i];
		t->ns = ti.pth_user_time + ti.pth_system_time;
		t->pri = ti.pth_priority;
		t->curpri = ti.pth_curpri;
		memcpy(t->name, ti.pth_name, sizeof(t->name));
		t->name[sizeof(t->name) - 1] = 0;
	}
}

static int
cpu_ticks(natural_t *count, processor_cpu_load_info_t *info)
{
	mach_msg_type_number_t n = 0;
	processor_info_array_t arr = NULL;
	if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, count, &arr, &n) != KERN_SUCCESS) {
		return -1;
	}
	*info = (processor_cpu_load_info_t)arr;
	return 0;
}

struct delta {
	uint64_t ns;
	struct thr *t;
};

static int
by_ns(const void *a, const void *b)
{
	const struct delta *x = a, *y = b;
	return x->ns < y->ns ? 1 : (x->ns > y->ns ? -1 : 0);
}

int
main(int argc, char **argv)
{
	double interval = 1.0;
	int count = 10, show = 12, ch;
	while ((ch = getopt(argc, argv, "i:n:t:")) != -1) {
		switch (ch) {
		case 'i': interval = atof(optarg); break;
		case 'n': count = atoi(optarg); break;
		case 't': show = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: s9top [-i SECONDS] [-n COUNT] [-t THREADS] NAME [NAME...]\n");
			return 2;
		}
	}
	int nprocs = argc - optind > MAX_PROCS ? MAX_PROCS : argc - optind;
	static struct proc before[MAX_PROCS], after[MAX_PROCS];
	for (int i = 0; i < nprocs; i++) {
		before[i].want = after[i].want = argv[optind + i];
		before[i].pid = after[i].pid = 0;
		sample_threads(&before[i]);
		after[i].pid = before[i].pid;
	}
	natural_t ncpu = 0;
	processor_cpu_load_info_t t0 = NULL, t1 = NULL;
	if (cpu_ticks(&ncpu, &t0) != 0) {
		fprintf(stderr, "host_processor_info failed\n");
		return 1;
	}
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	uint64_t m0 = mach_absolute_time();

	for (int round = 0; round < count; round++) {
		usleep((useconds_t)(interval * 1e6));
		uint64_t m1 = mach_absolute_time();
		double secs = (double)(m1 - m0) * tb.numer / tb.denom / 1e9;
		m0 = m1;
		natural_t n2 = 0;
		if (cpu_ticks(&n2, &t1) != 0) {
			return 1;
		}
		printf("--- %.2f s  cpu busy %%:", secs);
		double sum_small = 0, sum_big = 0;
		for (natural_t c = 0; c < ncpu && c < n2; c++) {
			uint64_t busy = 0, all = 0;
			for (int s = 0; s < CPU_STATE_MAX; s++) {
				uint64_t d = (uint64_t)(t1[c].cpu_ticks[s] - t0[c].cpu_ticks[s]);
				all += d;
				if (s != CPU_STATE_IDLE) {
					busy += d;
				}
			}
			double pct = all ? 100.0 * (double)busy / (double)all : 0;
			printf(" %s%.0f", c == 4 ? "| " : "", pct);
			if (c < 4) {
				sum_small += pct;
			} else {
				sum_big += pct;
			}
		}
		printf("   (A55 %.0f%%, A75 %.0f%% of 400)\n", sum_small, sum_big);
		vm_deallocate(mach_task_self(), (vm_address_t)t0, ncpu * sizeof(*t0));
		t0 = t1;
		ncpu = n2;

		for (int i = 0; i < nprocs; i++) {
			after[i].pid = before[i].pid;
			sample_threads(&after[i]);
			if (after[i].n == 0) {
				printf("  %s: not running\n", after[i].want);
				before[i] = after[i];
				continue;
			}
			static struct delta d[MAX_THREADS];
			int nd = 0;
			uint64_t total = 0;
			for (int a = 0; a < after[i].n; a++) {
				uint64_t prev = 0;
				for (int b = 0; b < before[i].n; b++) {
					if (before[i].t[b].id == after[i].t[a].id) {
						prev = before[i].t[b].ns;
						break;
					}
				}
				uint64_t ns = after[i].t[a].ns >= prev ? after[i].t[a].ns - prev : 0;
				total += ns;
				d[nd].ns = ns;
				d[nd].t = &after[i].t[a];
				nd++;
			}
			qsort(d, (size_t)nd, sizeof(d[0]), by_ns);
			printf("  %s [%d]: %.0f%% of one core, %d threads\n", after[i].want, after[i].pid,
			    100.0 * (double)total / (secs * 1e9), after[i].n);
			for (int k = 0; k < nd && k < show && d[k].ns > 0; k++) {
				printf("    %5.1f%%  pri %2d/%2d  %#llx %s\n", 100.0 * (double)d[k].ns / (secs * 1e9),
				    d[k].t->curpri, d[k].t->pri, (unsigned long long)d[k].t->id,
				    d[k].t->name[0] ? d[k].t->name : "-");
			}
			before[i] = after[i];
		}
		fflush(stdout);
	}
	return 0;
}
