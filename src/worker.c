/*
 * worker.c - FR6: worker processes with fork()/waitpid() and failure detection.
 * Owner: Person B.
 *
 * Every failure produced here is a SIMULATED failure chosen on the command
 * line; it is reported as such and is not a real OS fault.
 *   exit   -> the chosen worker calls _exit(3)        (non-zero exit status)
 *   signal -> the chosen worker calls abort()         (killed by SIGABRT)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

#define MAX_WORKERS 16
#define SIM_EXIT_CODE 3

typedef enum { FAIL_NONE, FAIL_EXIT, FAIL_SIGNAL } FailMode;

/* Child body. Uses only printf + _exit (no logger, no exit handlers). */
static void worker_body(int index, FailMode fail)
{
    pid_t me = getpid();
    int burst = 2 + index;                         /* pretend job size */
    printf("  [worker %d pid %d] started job J%d (burst %d)\n", index, (int)me, index, burst);
    fflush(stdout);

    volatile unsigned long acc = 0;                /* small real CPU work */
    for (unsigned long i = 0; i < (unsigned long)burst * 2000000UL; i++) acc += i % 7;
    usleep(20000 * index);                         /* stagger finishing order */

    if (fail == FAIL_EXIT) {
        printf("  [worker %d pid %d] SIMULATED FAILURE: exiting with code %d\n", index, (int)me, SIM_EXIT_CODE);
        fflush(stdout);
        _exit(SIM_EXIT_CODE);
    }
    if (fail == FAIL_SIGNAL) {
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);          /* don't leave a core file behind */
        printf("  [worker %d pid %d] SIMULATED FAILURE: calling abort()\n", index, (int)me);
        fflush(stdout);
        abort();
    }
    printf("  [worker %d pid %d] finished job J%d (checksum %lu)\n", index, (int)me, index, acc % 1000);
    fflush(stdout);
    _exit(0);
}

int cmd_worker(int argc, char **argv)
{
    int count = 3, fail_index = 0;
    FailMode mode = FAIL_NONE;

    if (argc > 4 ||
        (argc >= 2 && parse_int(argv[1], 1, MAX_WORKERS, &count)) ||
        (argc >= 3 && parse_int(argv[2], 1, MAX_WORKERS, &fail_index))) {
        goto usage;
    }
    if (argc >= 3) {
        if (fail_index > count) {
            fprintf(stderr, "Error: fail_index %d is larger than worker count %d\n", fail_index, count);
            return 2;
        }
        mode = FAIL_EXIT;
        if (argc == 4) {
            if (strcmp(argv[3], "exit") == 0) mode = FAIL_EXIT;
            else if (strcmp(argv[3], "signal") == 0) mode = FAIL_SIGNAL;
            else goto usage;
        }
    }

    printf("=== Worker processes (fork + waitpid) ===\n");
    printf("Workers: %d   Simulated failure: %s", count,
           mode == FAIL_NONE ? "none" : mode == FAIL_EXIT ? "exit code" : "signal (abort)");
    if (mode != FAIL_NONE) printf(" in worker %d", fail_index);
    printf("\n\n");

    pid_t pids[MAX_WORKERS + 1] = {0};
    for (int i = 1; i <= count; i++) {
        fflush(stdout);
        pid_t p = fork();
        if (p < 0) {
            perror("Error: fork");
            count = i - 1;
            break;
        }
        if (p == 0) worker_body(i, (i == fail_index) ? mode : FAIL_NONE);
        pids[i] = p;
        printf("  [parent %d] forked worker %d -> pid %d\n", (int)getpid(), i, (int)p);
        log_event("WORKER", "forked worker %d pid %d", i, (int)p);
    }

    int normal = 0, simulated = 0, unexpected = 0;
    printf("\n");
    for (int reaped = 0; reaped < count; reaped++) {
        int st;
        pid_t p = waitpid(-1, &st, 0);
        if (p < 0) {
            if (errno == EINTR) { reaped--; continue; }
            perror("Error: waitpid");
            break;
        }
        int idx = 0;
        for (int i = 1; i <= count; i++) if (pids[i] == p) idx = i;
        int expected_fail = (idx == fail_index && mode != FAIL_NONE);

        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
            printf("  [parent] waitpid: worker %d (pid %d) -> NORMAL completion, exit status 0\n", idx, (int)p);
            log_event("WORKER", "worker %d pid %d exited normally", idx, (int)p);
            normal++;
        } else {
            char why[96];
            if (WIFEXITED(st)) snprintf(why, sizeof why, "exit status %d", WEXITSTATUS(st));
            else if (WIFSIGNALED(st)) snprintf(why, sizeof why, "killed by signal %d (%s)",
                                               WTERMSIG(st), strsignal(WTERMSIG(st)));
            else snprintf(why, sizeof why, "raw status 0x%x", st);
            printf("  [parent] waitpid: worker %d (pid %d) -> FAILURE DETECTED: %s%s\n", idx, (int)p, why,
                   expected_fail ? "  [SIMULATED failure, not a real OS fault]" : "  [UNEXPECTED]");
            log_event("WORKER", "worker %d pid %d failed: %s (%s)", idx, (int)p, why,
                      expected_fail ? "simulated" : "unexpected");
            if (expected_fail) simulated++; else unexpected++;
        }
    }

    /* every child must now be reaped: waitpid should report ECHILD */
    int no_children = (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    printf("\nSummary: %d normal, %d simulated failure(s) detected, %d unexpected failure(s)\n",
           normal, simulated, unexpected);
    printf("All children reaped (no zombies): %s\n", no_children ? "yes" : "NO");

    int want_sim = (mode != FAIL_NONE) ? 1 : 0;
    return (unexpected == 0 && simulated == want_sim && no_children) ? 0 : 1;

usage:
    fprintf(stderr, "Usage: campuscompute worker [count 1-%d] [fail_index] [exit|signal]\n"
                    "  worker 3            three workers, all succeed\n"
                    "  worker 4 2 exit     worker 2 exits with code %d (simulated)\n"
                    "  worker 4 3 signal   worker 3 aborts with SIGABRT (simulated)\n",
            MAX_WORKERS, SIM_EXIT_CODE);
    return 2;
}
