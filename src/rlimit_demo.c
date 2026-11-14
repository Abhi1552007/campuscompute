/*
 * rlimit_demo.c - FR7: process resource limits with setrlimit().
 * Owner: Person B.
 *
 * Limits are applied inside a forked child so the main program is unaffected.
 *   cpu : RLIMIT_CPU soft=1s hard=2s, then a CPU-bound loop.
 *         Expected effect: kernel sends SIGXCPU and the child terminates.
 *   mem : RLIMIT_AS = current address-space size + 64 MB, then malloc(256 MB).
 *         Expected effect: malloc returns NULL (ENOMEM) while small allocations work.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

#define MB (1024UL * 1024UL)

static void print_limit(const char *name, int resource, const char *unit)
{
    struct rlimit r;
    if (getrlimit(resource, &r) != 0) { perror("getrlimit"); return; }
    printf("    %s: soft=", name);
    if (r.rlim_cur == RLIM_INFINITY) printf("unlimited"); else printf("%llu%s", (unsigned long long)r.rlim_cur, unit);
    printf(" hard=");
    if (r.rlim_max == RLIM_INFINITY) printf("unlimited"); else printf("%llu%s", (unsigned long long)r.rlim_max, unit);
    printf("\n");
}

static int demo_cpu(void)
{
    printf("--- RLIMIT_CPU demonstration ---\n");
    fflush(stdout);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pid_t p = fork();
    if (p < 0) { perror("Error: fork"); return 1; }
    if (p == 0) {
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        printf("  [child %d] before: ", (int)getpid());
        print_limit("RLIMIT_CPU", RLIMIT_CPU, "s");
        struct rlimit cur, lim;
        getrlimit(RLIMIT_CPU, &cur);
        lim.rlim_cur = 1;
        lim.rlim_max = (cur.rlim_max == RLIM_INFINITY || cur.rlim_max >= 2) ? 2 : cur.rlim_max;
        if (setrlimit(RLIMIT_CPU, &lim) != 0) { perror("  setrlimit(RLIMIT_CPU)"); _exit(10); }
        printf("  [child %d] after setrlimit: ", (int)getpid());
        print_limit("RLIMIT_CPU", RLIMIT_CPU, "s");
        printf("  [child %d] starting an endless CPU-bound loop...\n", (int)getpid());
        fflush(stdout);
        alarm(10);                                  /* safety net if the limit is not enforced */
        volatile unsigned long x = 0;
        for (;;) x++;
    }

    int st;
    waitpid(p, &st, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    struct rusage ru;
    getrusage(RUSAGE_CHILDREN, &ru);
    double wall = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;

    int ok = WIFSIGNALED(st) && WTERMSIG(st) == SIGXCPU;
    if (WIFSIGNALED(st))
        printf("  [parent] child %d terminated by signal %d (%s) after %.2fs wall time\n",
               (int)p, WTERMSIG(st), strsignal(WTERMSIG(st)), wall);
    else
        printf("  [parent] child %d exited with status %d\n", (int)p, WEXITSTATUS(st));
    printf("  [parent] CPU time used by children so far: %.2fs\n", cpu);
    printf("  Observed effect: %s\n\n", ok
           ? "the kernel stopped the runaway loop with SIGXCPU once it used 1s of CPU time"
           : "limit was NOT enforced as expected on this system");
    log_event("RLIMIT", "RLIMIT_CPU child %d -> %s", (int)p, ok ? "killed by SIGXCPU" : "unexpected");
    return ok ? 0 : 1;
}

/* virtual memory size of this process in bytes, from /proc/self/statm */
static unsigned long current_vm_bytes(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    unsigned long pages = 0;
    if (f) {
        if (fscanf(f, "%lu", &pages) != 1) pages = 0;
        fclose(f);
    }
    return pages * (unsigned long)sysconf(_SC_PAGESIZE);
}

static int demo_mem(void)
{
    printf("--- RLIMIT_AS (address space) demonstration ---\n");
    fflush(stdout);
    pid_t p = fork();
    if (p < 0) { perror("Error: fork"); return 1; }
    if (p == 0) {
        pid_t me = getpid();
        size_t big = 256 * MB, small = 8 * MB;

        char *a = malloc(big);
        printf("  [child %d] without limit: malloc(256 MB) %s\n", (int)me, a ? "succeeded" : "failed");
        free(a);

        unsigned long vm = current_vm_bytes();
        struct rlimit lim = { vm + 64 * MB, vm + 64 * MB };
        printf("  [child %d] current address space = %lu MB; setting RLIMIT_AS to %lu MB\n",
               (int)me, vm / MB, (unsigned long)(lim.rlim_cur / MB));
        if (setrlimit(RLIMIT_AS, &lim) != 0) { perror("  setrlimit(RLIMIT_AS)"); _exit(10); }
        printf("  [child %d] after setrlimit: ", (int)me);
        print_limit("RLIMIT_AS", RLIMIT_AS, " bytes");

        char *s = malloc(small);
        printf("  [child %d] with limit: malloc(8 MB)   %s\n", (int)me, s ? "succeeded" : "failed");
        free(s);
        errno = 0;
        char *b = malloc(big);
        int e = errno;
        printf("  [child %d] with limit: malloc(256 MB) %s%s%s\n", (int)me,
               b ? "succeeded" : "FAILED (", b ? "" : strerror(e), b ? "" : ")");
        fflush(stdout);
        free(b);
        _exit(b == NULL ? 0 : 1);                  /* 0 = limit enforced */
    }
    int st;
    waitpid(p, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("  Observed effect: %s\n\n", ok
           ? "the large allocation was refused with ENOMEM because it would exceed the address-space limit"
           : "limit was NOT enforced as expected on this system");
    log_event("RLIMIT", "RLIMIT_AS child %d -> %s", (int)p, ok ? "malloc refused" : "unexpected");
    return ok ? 0 : 1;
}

int cmd_rlimit(int argc, char **argv)
{
    const char *which = argc >= 2 ? argv[1] : "all";
    if (argc > 2 || (strcmp(which, "cpu") && strcmp(which, "mem") && strcmp(which, "all"))) {
        fprintf(stderr, "Usage: campuscompute rlimit [cpu|mem|all]\n");
        return 2;
    }
    printf("=== Resource limits (setrlimit) - limits are applied only inside a child process ===\n");
    printf("Parent's own limits (unchanged):\n");
    print_limit("RLIMIT_CPU", RLIMIT_CPU, "s");
    print_limit("RLIMIT_AS ", RLIMIT_AS, " bytes");
    printf("\n");

    int rc = 0;
    if (!strcmp(which, "cpu") || !strcmp(which, "all")) rc |= demo_cpu();
    if (!strcmp(which, "mem") || !strcmp(which, "all")) rc |= demo_mem();
    return rc;
}
