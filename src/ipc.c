/*
 * ipc.c - FR4: POSIX shared memory + named semaphores between processes.
 * Owner: Person B.
 *
 * Protocol (parent = dispatcher, child = analyser):
 *   sem_req  (initial 0): parent posts after writing a request into shm.
 *   sem_resp (initial 0): child posts after writing its answer into shm.
 * The two semaphores make the processes take strict turns, so neither
 * reads the shared block while the other is writing it.
 *
 * All names contain the parent's PID ("/cc_shm_<pid>", "/cc_req_<pid>",
 * "/cc_resp_<pid>") so we only ever create - and unlink - our own objects.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

#define MAX_VALUES 16
#define ROUNDS 3
#define SEM_TIMEOUT_SEC 5

typedef struct {
    int round;
    int quit;
    int n;
    int values[MAX_VALUES];
    long sum;
    double avg;
    int max;
    pid_t answered_by;
} ShmBlock;

static char g_shm[64], g_req[64], g_resp[64];
static volatile sig_atomic_t g_have_objects = 0;

static void unlink_all(void)
{
    shm_unlink(g_shm);
    sem_unlink(g_req);
    sem_unlink(g_resp);
}

/* Ctrl-C during the demo: remove our IPC objects before dying. */
static void on_sigint(int sig)
{
    (void)sig;
    if (g_have_objects) unlink_all();
    _exit(130);
}

static int wait_sem(sem_t *s, const char *who)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += SEM_TIMEOUT_SEC;
    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR) continue;
        fprintf(stderr, "Error: %s: sem_timedwait: %s\n", who, strerror(errno));
        return -1;
    }
    return 0;
}

/* Child process: analyse each request until told to quit. Never logs (see logger.c). */
static void child_loop(ShmBlock *shm, sem_t *req, sem_t *resp)
{
    pid_t me = getpid();
    for (;;) {
        if (wait_sem(req, "child") != 0) _exit(1);
        if (shm->quit) {
            printf("  [child  %d] received QUIT, detaching\n", (int)me);
            fflush(stdout);
            _exit(0);
        }
        long sum = 0;
        int mx = shm->values[0];
        for (int i = 0; i < shm->n; i++) {
            sum += shm->values[i];
            if (shm->values[i] > mx) mx = shm->values[i];
        }
        shm->sum = sum;
        shm->avg = (double)sum / shm->n;
        shm->max = mx;
        shm->answered_by = me;
        printf("  [child  %d] sem_wait(req) ok -> read round %d (%d values) -> sum=%ld avg=%.2f max=%d -> sem_post(resp)\n",
               (int)me, shm->round, shm->n, sum, shm->avg, mx);
        fflush(stdout);
        sem_post(resp);
    }
}

int cmd_ipc(int argc, char **argv)
{
    int vals[MAX_VALUES] = {5, 3, 8, 6, 2};
    int n = 5;
    if (argc > 1) {
        if (argc - 1 > MAX_VALUES) {
            fprintf(stderr, "Error: at most %d values\n", MAX_VALUES);
            return 2;
        }
        n = argc - 1;
        for (int i = 0; i < n; i++)
            if (parse_int(argv[i + 1], 0, 1000000, &vals[i])) {
                fprintf(stderr, "Error: '%s' is not a non-negative integer\nUsage: campuscompute ipc [value ...]\n",
                        argv[i + 1]);
                return 2;
            }
    }

    pid_t me = getpid();
    snprintf(g_shm, sizeof g_shm, "/cc_shm_%d", (int)me);
    snprintf(g_req, sizeof g_req, "/cc_req_%d", (int)me);
    snprintf(g_resp, sizeof g_resp, "/cc_resp_%d", (int)me);

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, &old_sa);

    int rc = 1, fd = -1;
    ShmBlock *shm = MAP_FAILED;
    sem_t *req = SEM_FAILED, *resp = SEM_FAILED;
    pid_t child = -1;

    printf("=== Shared-memory IPC ===\n");
    fd = shm_open(g_shm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { fprintf(stderr, "Error: shm_open(%s): %s\n", g_shm, strerror(errno)); goto cleanup; }
    g_have_objects = 1;
    if (ftruncate(fd, sizeof(ShmBlock)) < 0) { perror("Error: ftruncate"); goto cleanup; }
    shm = mmap(NULL, sizeof(ShmBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("Error: mmap"); goto cleanup; }
    memset(shm, 0, sizeof *shm);

    req = sem_open(g_req, O_CREAT | O_EXCL, 0600, 0);
    if (req == SEM_FAILED) { fprintf(stderr, "Error: sem_open(%s): %s\n", g_req, strerror(errno)); goto cleanup; }
    resp = sem_open(g_resp, O_CREAT | O_EXCL, 0600, 0);
    if (resp == SEM_FAILED) { fprintf(stderr, "Error: sem_open(%s): %s\n", g_resp, strerror(errno)); goto cleanup; }

    printf("Created shared memory %s (%zu bytes) and named semaphores %s, %s (initial value 0)\n",
           g_shm, sizeof(ShmBlock), g_req, g_resp);
    printf("Objects visible now in /dev/shm:\n");
    ipc_count_leftovers(1);
    log_event("IPC", "created %s %s %s", g_shm, g_req, g_resp);

    fflush(stdout);
    child = fork();
    if (child < 0) { perror("Error: fork"); goto cleanup; }
    if (child == 0) child_loop(shm, req, resp);   /* never returns */

    printf("  [parent %d] forked analyser child %d\n", (int)me, (int)child);
    for (int r = 1; r <= ROUNDS; r++) {
        shm->round = r;
        shm->n = n;
        for (int i = 0; i < n; i++) shm->values[i] = vals[i] * r;   /* different data each round */
        printf("  [parent %d] wrote round %d values:", (int)me, r);
        for (int i = 0; i < n; i++) printf(" %d", shm->values[i]);
        printf(" -> sem_post(req)\n");
        fflush(stdout);
        sem_post(req);

        if (wait_sem(resp, "parent") != 0) goto cleanup;
        printf("  [parent %d] sem_wait(resp) ok -> child %d answered: sum=%ld avg=%.2f max=%d\n",
               (int)me, (int)shm->answered_by, shm->sum, shm->avg, shm->max);
        log_event("IPC", "round %d: sum=%ld avg=%.2f max=%d from child %d",
                  r, shm->sum, shm->avg, shm->max, (int)shm->answered_by);
        fflush(stdout);
    }
    shm->quit = 1;
    sem_post(req);
    rc = 0;

cleanup:
    if (child > 0) {
        int st;
        if (rc != 0) kill(child, SIGTERM);
        waitpid(child, &st, 0);
        printf("  [parent %d] child %d reaped (%s %d)\n", (int)me, (int)child,
               WIFEXITED(st) ? "exit status" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        if (WIFEXITED(st) && WEXITSTATUS(st) != 0) rc = 1;
    }
    printf("\nCleanup:\n");
    if (shm != MAP_FAILED) { munmap(shm, sizeof(ShmBlock)); printf("  munmap shared block\n"); }
    if (fd >= 0) close(fd);
    if (req != SEM_FAILED) sem_close(req);
    if (resp != SEM_FAILED) sem_close(resp);
    if (g_have_objects) {
        unlink_all();
        g_have_objects = 0;
        printf("  shm_unlink(%s), sem_unlink(%s), sem_unlink(%s)\n", g_shm, g_req, g_resp);
    }
    sigaction(SIGINT, &old_sa, NULL);
    int left = ipc_count_leftovers(0);
    printf("  CampusCompute objects remaining in /dev/shm: %d\n", left);
    log_event("IPC", "cleanup done, %d objects remaining", left);
    printf("IPC result: %s\n", rc == 0 && left == 0 ? "SUCCESS" : "FAILED");
    return (rc == 0 && left == 0) ? 0 : 1;
}

/* Linux exposes POSIX shm as /dev/shm/<name> and named semaphores as /dev/shm/sem.<name> */
int ipc_count_leftovers(int print)
{
    DIR *d = opendir("/dev/shm");
    if (!d) {
        if (print) printf("  (cannot open /dev/shm: %s)\n", strerror(errno));
        return 0;
    }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "cc_", 3) == 0 || strncmp(e->d_name, "sem.cc_", 7) == 0) {
            if (print) printf("  /dev/shm/%s\n", e->d_name);
            count++;
        }
    }
    closedir(d);
    if (print && count == 0) printf("  (none) - no CampusCompute shared memory or semaphores exist\n");
    return count;
}

int cmd_ipc_status(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "Usage: campuscompute ipc-status\n");
        return 2;
    }
    printf("CampusCompute IPC objects in /dev/shm (cc_* and sem.cc_*):\n");
    int n = ipc_count_leftovers(1);
    printf("Total: %d\n", n);
    return 0;
}
