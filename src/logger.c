/*
 * logger.c - FR3: concurrent activity logger (bounded producer-consumer).
 * Owner: Person B.
 *
 * Design:
 *   - A circular buffer of LOG_QUEUE_CAPACITY entries is the shared data.
 *   - One mutex protects the buffer and all counters.
 *   - Condition variable not_full : producers wait here when the buffer is full.
 *   - Condition variable not_empty: the consumer waits here when it is empty.
 *   - Any thread calling log_event() is a producer. One consumer thread
 *     writes entries to the log file.
 *   - Shutdown: logger_stop() sets 'stopping', broadcasts both conditions,
 *     the consumer drains what is left and exits, then pthread_join().
 *
 * fork() safety: a child created by fork() has only the forking thread, so
 * the consumer does not exist there. log_event() checks the PID and silently
 * does nothing in a child process.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

typedef struct {
    unsigned long seq;
    char producer[16];
    char text[400];
} LogEntry;

static struct {
    LogEntry buf[LOG_QUEUE_CAPACITY];
    int head, tail, count;
    pthread_mutex_t mtx;
    pthread_cond_t not_full, not_empty;
    pthread_t consumer;
    int running, stopping, writing, verbose;
    FILE *fp;
    pid_t owner;
    unsigned long produced, consumed, full_waits;
    int max_occupancy;
} L = {
    .mtx = PTHREAD_MUTEX_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
};

static __thread const char *tl_name = "main";

void logger_set_thread_name(const char *name) { tl_name = name; }

void logger_set_verbose(int on)
{
    pthread_mutex_lock(&L.mtx);
    L.verbose = on;
    pthread_mutex_unlock(&L.mtx);
}

static void *consumer_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&L.mtx);
    for (;;) {
        while (L.count == 0 && !L.stopping)
            pthread_cond_wait(&L.not_empty, &L.mtx);
        if (L.count == 0 && L.stopping)
            break;                                  /* drained and asked to stop */

        LogEntry e = L.buf[L.head];
        L.head = (L.head + 1) % LOG_QUEUE_CAPACITY;
        L.count--;
        L.writing = 1;
        int verbose = L.verbose, left = L.count;
        pthread_cond_broadcast(&L.not_full);        /* a slot is free */
        pthread_mutex_unlock(&L.mtx);

        /* file I/O happens outside the lock so producers are not blocked by it */
        fprintf(L.fp, "#%04lu %s [%s]\n", e.seq, e.text, e.producer);
        fflush(L.fp);
        if (verbose) {
            printf("  [consumer] wrote entry #%lu from %-4s -> log   (queue %d/%d)\n",
                   e.seq, e.producer, left, LOG_QUEUE_CAPACITY);
            fflush(stdout);
            usleep(3000);                           /* slow consumer so 'full' is visible */
        }

        pthread_mutex_lock(&L.mtx);
        L.consumed++;
        L.writing = 0;
        pthread_cond_broadcast(&L.not_full);        /* wakes logger_flush() too */
    }
    pthread_mutex_unlock(&L.mtx);
    return NULL;
}

int logger_start(const char *path)
{
    if (L.running) return 0;
    L.fp = fopen(path, "a");
    if (!L.fp) {
        fprintf(stderr, "Error: cannot open log file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    L.head = L.tail = L.count = 0;
    L.stopping = L.writing = 0;
    L.produced = L.consumed = L.full_waits = 0;
    L.max_occupancy = 0;
    L.owner = getpid();
    int rc = pthread_create(&L.consumer, NULL, consumer_main, NULL);
    if (rc != 0) {
        fprintf(stderr, "Error: pthread_create(consumer): %s\n", strerror(rc));
        fclose(L.fp);
        L.fp = NULL;
        return -1;
    }
    L.running = 1;
    return 0;
}

void log_event(const char *module, const char *fmt, ...)
{
    if (!L.running || getpid() != L.owner) return;

    /* build the message (timestamp is the time the event happened) */
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    LogEntry e;
    snprintf(e.producer, sizeof e.producer, "%s", tl_name);
    snprintf(e.text, sizeof e.text, "%s.%03ld pid=%d tid=%ld %-6s %s",
             ts, (long)(tv.tv_usec / 1000), (int)getpid(), (long)syscall(SYS_gettid), module, msg);

    pthread_mutex_lock(&L.mtx);
    while (L.count == LOG_QUEUE_CAPACITY && !L.stopping) {
        L.full_waits++;
        if (L.verbose) {
            printf("  [%-4s] queue FULL (%d/%d) -> waiting on not_full\n",
                   tl_name, L.count, LOG_QUEUE_CAPACITY);
            fflush(stdout);
        }
        pthread_cond_wait(&L.not_full, &L.mtx);
    }
    if (L.stopping) {                       /* shutting down: drop late events */
        pthread_mutex_unlock(&L.mtx);
        return;
    }
    e.seq = ++L.produced;
    L.buf[L.tail] = e;
    L.tail = (L.tail + 1) % LOG_QUEUE_CAPACITY;
    L.count++;
    if (L.count > L.max_occupancy) L.max_occupancy = L.count;
    if (L.verbose) {
        printf("  [%-4s] produced entry #%lu: %-28s (queue %d/%d)\n",
               tl_name, e.seq, msg, L.count, LOG_QUEUE_CAPACITY);
        fflush(stdout);
    }
    pthread_cond_signal(&L.not_empty);
    pthread_mutex_unlock(&L.mtx);
}

void logger_flush(void)
{
    if (!L.running) return;
    pthread_mutex_lock(&L.mtx);
    while (L.count > 0 || L.writing)
        pthread_cond_wait(&L.not_full, &L.mtx);
    pthread_mutex_unlock(&L.mtx);
}

void logger_stop(void)
{
    if (!L.running || getpid() != L.owner) return;
    pthread_mutex_lock(&L.mtx);
    int verbose = L.verbose, pending = L.count;
    L.stopping = 1;
    pthread_cond_broadcast(&L.not_empty);
    pthread_cond_broadcast(&L.not_full);
    pthread_mutex_unlock(&L.mtx);

    if (verbose) printf("[main] stop requested; consumer draining %d pending entries\n", pending);
    int rc = pthread_join(L.consumer, NULL);
    if (rc != 0) fprintf(stderr, "Error: pthread_join(consumer): %s\n", strerror(rc));
    else if (verbose) printf("[main] consumer thread joined (produced=%lu, written=%lu)\n",
                             L.produced, L.consumed);
    fclose(L.fp);
    L.fp = NULL;
    L.running = 0;
}

/* ---------------- 'logger' command: S10 / S11 ---------------- */

typedef struct {
    char name[8];
    int events;
} ProducerArg;

static void *producer_main(void *arg)
{
    ProducerArg *p = arg;
    logger_set_thread_name(p->name);
    for (int i = 1; i <= p->events; i++) {
        log_event("PROD", "%s job-event %d/%d", p->name, i, p->events);
        usleep(500 + (rand() % 1000));
    }
    return NULL;
}

int cmd_logger(int argc, char **argv)
{
    int producers = 3, events = 5;
    if (argc > 3 ||
        (argc >= 2 && parse_int(argv[1], 1, 8, &producers)) ||
        (argc >= 3 && parse_int(argv[2], 1, 100, &events))) {
        fprintf(stderr, "Usage: campuscompute logger [producers 1-8] [events_each 1-100]\n");
        return 2;
    }
    if (!L.running && logger_start(LOG_FILE_DEFAULT) != 0) return 1;

    printf("=== Producer-consumer logger ===\n");
    printf("Bounded queue capacity = %d, producers = %d, events each = %d, consumer = 1\n",
           LOG_QUEUE_CAPACITY, producers, events);
    printf("Sync: pthread mutex + condition variables not_full / not_empty\n\n");

    logger_flush();
    unsigned long base_produced = L.produced, base_consumed = L.consumed, base_full = L.full_waits;
    logger_set_verbose(1);

    pthread_t th[8];
    ProducerArg args[8];
    srand((unsigned)getpid());
    int created = 0;
    for (int i = 0; i < producers; i++) {
        snprintf(args[i].name, sizeof args[i].name, "P%d", i + 1);
        args[i].events = events;
        int rc = pthread_create(&th[i], NULL, producer_main, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create(producer): %s\n", strerror(rc));
            break;
        }
        created++;
    }
    for (int i = 0; i < created; i++) {
        pthread_join(th[i], NULL);
        printf("[main] producer %s finished and joined\n", args[i].name);
    }
    logger_flush();

    pthread_mutex_lock(&L.mtx);
    unsigned long prod = L.produced - base_produced, cons = L.consumed - base_consumed;
    unsigned long fw = L.full_waits - base_full;
    int maxocc = L.max_occupancy;
    pthread_mutex_unlock(&L.mtx);

    printf("\nSummary: produced=%lu written=%lu expected=%d, producer waits on FULL=%lu, max occupancy=%d/%d\n",
           prod, cons, created * events, fw, maxocc, LOG_QUEUE_CAPACITY);
    int ok = (prod == cons && prod == (unsigned long)(created * events));
    printf("CHECK: %s\n", ok ? "every event written exactly once - no loss, no duplicates"
                             : "MISMATCH between produced and written counts");
    printf("Log file: %s\n", LOG_FILE_DEFAULT);
    return ok && created == producers ? 0 : 1;
}
