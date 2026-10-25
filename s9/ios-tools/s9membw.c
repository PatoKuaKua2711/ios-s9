/*
 * s9membw: memory speed on the Galaxy S9 iOS port, to see what the DDR and
 * L3 deliver at the bandwidth vote ABL leaves (kern.sweet_mem_vote stays 0
 * unless the owner says otherwise). Prints, for 1, 2, 4 and 8 threads,
 * write (memset), copy (memcpy) and read (summing 64-bit words) bandwidth
 * over buffers several times larger than the 2 MB L3, then a dependent-load
 * latency (pointer chasing through 32 MB in a random order, so nearly every
 * load misses the caches).
 *
 *   s9membw [MB per buffer, default 8]
 *
 * Uses 2 buffers per thread (128 MB at the default) plus 34 MB for the
 * latency test: small enough not to wake jetsam. With the kernel's fast-core
 * scheduling, 1-4 threads at the default priority land on the A75s first.
 * Only reads and writes its own memory.
 */
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THREADS     8
#define MOVE_PER_THREAD (256u << 20)    /* bytes each thread moves per test */

static size_t g_bytes;
static int g_reps;
static mach_timebase_info_data_t g_tb;

struct job {
	uint8_t *a, *b;
	int kind;               /* 0 write, 1 copy, 2 read */
	uint64_t sum;
	double t0, t1;
};

static double
now_s(void)
{
	return (double)mach_absolute_time() * g_tb.numer / g_tb.denom / 1e9;
}

/* Darwin has no pthread barriers: a tiny spin barrier among the workers. */
static volatile int g_arrived, g_gen, g_threads;
static void
barrier(void)
{
	int gen = g_gen;
	if (__sync_add_and_fetch(&g_arrived, 1) == g_threads) {
		g_arrived = 0;
		__sync_synchronize();
		g_gen = gen + 1;
	} else {
		while (g_gen == gen) {
		}
	}
}

static void *
run(void *arg)
{
	struct job *j = arg;
	barrier();              /* every worker exists: start together */
	j->t0 = now_s();
	for (int rep = 0; rep < g_reps; rep++) {
		if (j->kind == 0) {
			memset(j->a, rep + 1, g_bytes);
		} else if (j->kind == 1) {
			memcpy(j->b, j->a, g_bytes);
		} else {
			const uint64_t *p = (const uint64_t *)j->a;
			uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
			for (size_t i = 0; i < g_bytes / 8; i += 4) {
				s0 += p[i];
				s1 += p[i + 1];
				s2 += p[i + 2];
				s3 += p[i + 3];
			}
			j->sum += s0 + s1 + s2 + s3;
		}
	}
	j->t1 = now_s();
	return NULL;
}

/* GB/s for one kind of access, from the first start to the last finish. */
static double
bench(int threads, int kind, struct job *jobs)
{
	pthread_t tid[MAX_THREADS];
	g_threads = threads;
	g_arrived = 0;
	for (int t = 0; t < threads; t++) {
		jobs[t].kind = kind;
		pthread_create(&tid[t], NULL, run, &jobs[t]);
	}
	for (int t = 0; t < threads; t++) {
		pthread_join(tid[t], NULL);
	}
	double t0 = jobs[0].t0, t1 = jobs[0].t1;
	for (int t = 1; t < threads; t++) {
		if (jobs[t].t0 < t0) {
			t0 = jobs[t].t0;
		}
		if (jobs[t].t1 > t1) {
			t1 = jobs[t].t1;
		}
	}
	double moved = (double)g_bytes * g_reps * threads * (kind == 1 ? 2 : 1);
	return moved / (t1 - t0) / 1e9;
}

int
main(int argc, char **argv)
{
	size_t mb = argc > 1 ? (size_t)atoi(argv[1]) : 8;
	if (mb < 1 || mb > 256) {
		fprintf(stderr, "usage: s9membw [MB per buffer, 1-256]\n");
		return 2;
	}
	mach_timebase_info(&g_tb);
	g_bytes = mb << 20;
	g_reps = (int)(MOVE_PER_THREAD / g_bytes);
	if (g_reps < 2) {
		g_reps = 2;
	}
	struct job jobs[MAX_THREADS];
	for (int t = 0; t < MAX_THREADS; t++) {
		jobs[t].a = malloc(g_bytes);
		jobs[t].b = malloc(g_bytes);
		jobs[t].sum = 0;
		if (!jobs[t].a || !jobs[t].b) {
			fprintf(stderr, "out of memory\n");
			return 1;
		}
		memset(jobs[t].a, 1, g_bytes);  /* fault the pages in first */
		memset(jobs[t].b, 1, g_bytes);
	}
	printf("buffers: %zu MB, %d passes per test (GB/s; copy counts read + write)\n", mb, g_reps);
	printf("threads   write    copy    read\n");
	for (int n = 1; n <= MAX_THREADS; n *= 2) {
		double w = bench(n, 0, jobs), c = bench(n, 1, jobs), r = bench(n, 2, jobs);
		printf("  %d     %6.2f  %6.2f  %6.2f\n", n, w, c, r);
		fflush(stdout);
	}

	/* Latency: one pointer chain through 32 MB, random order, 64-byte steps. */
	size_t lines = (32u << 20) / 64;
	uint64_t *chain = malloc(lines * 64);
	uint32_t *order = malloc(lines * sizeof(uint32_t));
	if (chain && order) {
		for (size_t i = 0; i < lines; i++) {
			order[i] = (uint32_t)i;
		}
		uint64_t x = 0x9e3779b97f4a7c15ull;
		for (size_t i = lines - 1; i > 0; i--) {
			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
			size_t k = (size_t)(x % (i + 1));
			uint32_t tmp = order[i];
			order[i] = order[k];
			order[k] = tmp;
		}
		for (size_t i = 0; i < lines; i++) {
			chain[(size_t)order[i] * 8] = (uint64_t)(uintptr_t)&chain[(size_t)order[(i + 1) % lines] * 8];
		}
		uint64_t *p = &chain[(size_t)order[0] * 8];
		size_t steps = 4u << 20;
		double t0 = now_s();
		for (size_t i = 0; i < steps; i++) {
			p = (uint64_t *)(uintptr_t)*p;
		}
		double t1 = now_s();
		/* Printing p keeps the chase from being optimised away. */
		printf("latency (32 MB random chain): %.1f ns per load (end %p)\n",
		    (t1 - t0) / (double)steps * 1e9, (void *)p);
	}
	uint64_t s = 0;
	for (int t = 0; t < MAX_THREADS; t++) {
		s += jobs[t].sum;
	}
	printf("(checksum %llu)\n", (unsigned long long)s);
	return 0;
}
