/*
 * scheduler.c - FR2: FCFS, Round Robin and non-preemptive Priority scheduling.
 * Owner: Person A.
 *
 * Tie-breaking rules (printed with every run):
 *   FCFS     : earlier arrival first; equal arrival -> lower job ID first.
 *   RR       : ready queue ordered by arrival (then ID). Jobs that arrive
 *              during a time slice join the queue BEFORE the preempted job
 *              is put back at the tail. Default quantum = 2.
 *   Priority : lower priority number runs first; tie -> earlier arrival;
 *              still tied -> lower job ID. Non-preemptive.
 *   If no job is ready, the CPU idles until the next arrival.
 */
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

typedef struct {
    const char *name;
    double avg_tat, avg_wt;
    int makespan;
    char order[256];
} PolicyResult;

/* ---------- helpers ---------- */

static int cmp_arrival_id(const void *a, const void *b)
{
    const Job *x = a, *y = b;
    if (x->arrival != y->arrival) return x->arrival - y->arrival;
    return x->id - y->id;
}

/* Append a segment, merging with the previous one if same job and contiguous. */
static void add_segment(Segment *tl, int *n, int job_id, int start, int end)
{
    if (*n > 0 && tl[*n - 1].job_id == job_id && tl[*n - 1].end == start) {
        tl[*n - 1].end = end;
        return;
    }
    if (*n >= MAX_TIMELINE) return;
    tl[*n].job_id = job_id;
    tl[*n].start  = start;
    tl[*n].end    = end;
    (*n)++;
}

static void finish_job(Job *j, int time)
{
    j->completion = time;
    j->turnaround = j->completion - j->arrival;
    j->waiting    = j->turnaround - j->burst;
    j->status     = JOB_DONE;
    log_event("SCHED", "job J%d completed at t=%d (TAT=%d, WT=%d)",
              j->id, j->completion, j->turnaround, j->waiting);
}

/* ---------- algorithms ---------- */

static void run_fcfs(Job *jobs, int n, Segment *tl, int *tn)
{
    qsort(jobs, n, sizeof(Job), cmp_arrival_id);
    int time = 0;
    for (int i = 0; i < n; i++) {
        if (time < jobs[i].arrival) {
            add_segment(tl, tn, -1, time, jobs[i].arrival);
            time = jobs[i].arrival;
        }
        jobs[i].status = JOB_RUNNING;
        jobs[i].start  = time;
        add_segment(tl, tn, jobs[i].id, time, time + jobs[i].burst);
        time += jobs[i].burst;
        jobs[i].remaining = 0;
        finish_job(&jobs[i], time);
    }
}

static void run_rr(Job *jobs, int n, int quantum, Segment *tl, int *tn)
{
    qsort(jobs, n, sizeof(Job), cmp_arrival_id);
    int queue[MAX_JOBS * 4];          /* circular buffer of job indices */
    int qh = 0, qt = 0, qcount = 0;
    int next = 0, done = 0, time = 0;

#define ENQ(i) do { int ji_ = (i); queue[qt] = ji_; qt = (qt + 1) % (MAX_JOBS * 4); qcount++; \
                    jobs[ji_].status = JOB_READY; } while (0)

    while (done < n) {
        /* admit everything that has arrived by now */
        while (next < n && jobs[next].arrival <= time) ENQ(next++);

        if (qcount == 0) {             /* CPU idle until next arrival */
            add_segment(tl, tn, -1, time, jobs[next].arrival);
            time = jobs[next].arrival;
            continue;
        }

        int i = queue[qh];
        qh = (qh + 1) % (MAX_JOBS * 4);
        qcount--;

        Job *j = &jobs[i];
        if (j->start < 0) j->start = time;
        j->status = JOB_RUNNING;
        int slice = j->remaining < quantum ? j->remaining : quantum;
        add_segment(tl, tn, j->id, time, time + slice);
        time += slice;
        j->remaining -= slice;

        /* arrivals during this slice go in before the preempted job */
        while (next < n && jobs[next].arrival <= time) ENQ(next++);

        if (j->remaining == 0) {
            finish_job(j, time);
            done++;
        } else {
            ENQ(i);
        }
    }
#undef ENQ
}

static void run_priority(Job *jobs, int n, Segment *tl, int *tn)
{
    qsort(jobs, n, sizeof(Job), cmp_arrival_id);
    int time = 0, done = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (jobs[i].status == JOB_DONE || jobs[i].arrival > time) continue;
            if (best < 0 ||
                jobs[i].priority < jobs[best].priority ||
                (jobs[i].priority == jobs[best].priority && jobs[i].arrival < jobs[best].arrival) ||
                (jobs[i].priority == jobs[best].priority && jobs[i].arrival == jobs[best].arrival &&
                 jobs[i].id < jobs[best].id))
                best = i;
        }
        if (best < 0) {                /* nothing ready: idle to next arrival */
            int na = -1;
            for (int i = 0; i < n; i++)
                if (jobs[i].status != JOB_DONE && (na < 0 || jobs[i].arrival < na))
                    na = jobs[i].arrival;
            add_segment(tl, tn, -1, time, na);
            time = na;
            continue;
        }
        Job *j = &jobs[best];
        j->status = JOB_RUNNING;
        j->start  = time;
        add_segment(tl, tn, j->id, time, time + j->burst);
        time += j->burst;
        j->remaining = 0;
        finish_job(j, time);
        done++;
    }
}

/* ---------- output ---------- */

static void print_gantt(const Segment *tl, int tn)
{
    printf("Gantt chart:\n|");
    for (int i = 0; i < tn; i++) {
        char label[16];
        if (tl[i].job_id < 0) snprintf(label, sizeof label, "idle");
        else                  snprintf(label, sizeof label, "J%d", tl[i].job_id);
        printf(" %-5s|", label);
    }
    printf("\n");
    for (int i = 0; i < tn; i++) printf("%-7d", tl[i].start);
    if (tn > 0) printf("%d", tl[tn - 1].end);
    printf("\n");
}

static int cmp_id(const void *a, const void *b)
{
    return ((const Job *)a)->id - ((const Job *)b)->id;
}

static void summarise(const char *name, Job *jobs, int n, const Segment *tl, int tn,
                      PolicyResult *r, int print)
{
    double sum_tat = 0, sum_wt = 0;
    for (int i = 0; i < n; i++) { sum_tat += jobs[i].turnaround; sum_wt += jobs[i].waiting; }
    r->name = name;
    r->avg_tat = sum_tat / n;
    r->avg_wt  = sum_wt / n;
    r->makespan = tn > 0 ? tl[tn - 1].end : 0;

    /* execution order string, e.g. "J1 J2 J5 ..." (CPU slices, idle omitted) */
    r->order[0] = '\0';
    for (int i = 0; i < tn; i++) {
        if (tl[i].job_id < 0) continue;
        char b[16];
        snprintf(b, sizeof b, "J%d ", tl[i].job_id);
        if (strlen(r->order) + strlen(b) < sizeof r->order - 4) strcat(r->order, b);
        else { strcat(r->order, "..."); break; }
    }

    log_event("SCHED", "%s finished: avg TAT=%.2f avg WT=%.2f", name, r->avg_tat, r->avg_wt);
    if (!print) return;

    print_gantt(tl, tn);
    printf("Execution order: %s\n\n", r->order);
    qsort(jobs, n, sizeof(Job), cmp_id);
    printf("%-5s %-8s %-6s %-9s %-6s %-11s %-11s %-8s %-7s\n",
           "Job", "Arrival", "Burst", "Priority", "Start", "Completion", "Turnaround", "Waiting", "Status");
    for (int i = 0; i < n; i++)
        printf("J%-4d %-8d %-6d %-9d %-6d %-11d %-11d %-8d %-7s\n",
               jobs[i].id, jobs[i].arrival, jobs[i].burst, jobs[i].priority, jobs[i].start,
               jobs[i].completion, jobs[i].turnaround, jobs[i].waiting, status_name(jobs[i].status));
    printf("\nAverage turnaround time = %.2f\n", r->avg_tat);
    printf("Average waiting time    = %.2f\n", r->avg_wt);
}

/* ---------- command plumbing ---------- */

enum { POL_FCFS, POL_RR, POL_PRIO };

/* Parse "<jobfile> [-q N]" for the given command. Returns 0 on success. */
static int parse_args(int argc, char **argv, const char **file, int *quantum, int allow_q)
{
    *file = NULL;
    *quantum = DEFAULT_QUANTUM;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            if (!allow_q) {
                fprintf(stderr, "Error: '%s' does not take a time quantum\n", argv[0]);
                return -1;
            }
            if (i + 1 >= argc || parse_int(argv[i + 1], 1, 1000, quantum)) {
                fprintf(stderr, "Error: -q needs a time quantum between 1 and 1000\n");
                return -1;
            }
            i++;
        } else if (*file == NULL) {
            *file = argv[i];
        } else {
            fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
            return -1;
        }
    }
    if (*file == NULL) {
        fprintf(stderr, "Usage: campuscompute %s <jobfile>%s\n", argv[0], allow_q ? " [-q quantum]" : "");
        return -1;
    }
    return 0;
}

static int run_policy(int policy, Job *orig, int n, int quantum, PolicyResult *r, int print)
{
    Job jobs[MAX_JOBS];
    Segment tl[MAX_TIMELINE];
    int tn = 0;
    memcpy(jobs, orig, sizeof(Job) * n);
    reset_jobs(jobs, n);

    const char *name = "";
    switch (policy) {
    case POL_FCFS:
        name = "FCFS";
        if (print) printf("=== FCFS (First-Come, First-Served) ===\n"
                          "Tie-break: earlier arrival first; equal arrival -> lower job ID first.\n\n");
        run_fcfs(jobs, n, tl, &tn);
        break;
    case POL_RR:
        name = "Round Robin";
        if (print) printf("=== Round Robin (time quantum = %d) ===\n"
                          "Tie-break: ready queue by arrival then job ID; new arrivals join before the preempted job.\n\n",
                          quantum);
        run_rr(jobs, n, quantum, tl, &tn);
        break;
    case POL_PRIO:
        name = "Priority (NP)";
        if (print) printf("=== Priority Scheduling (non-preemptive, lower number = higher priority) ===\n"
                          "Tie-break: equal priority -> earlier arrival -> lower job ID.\n\n");
        run_priority(jobs, n, tl, &tn);
        break;
    }
    summarise(name, jobs, n, tl, tn, r, print);
    return 0;
}

static int cmd_single(int policy, int argc, char **argv)
{
    const char *file;
    int q;
    if (parse_args(argc, argv, &file, &q, policy == POL_RR)) return 2;
    Job jobs[MAX_JOBS];
    int n;
    if (load_jobs(file, jobs, MAX_JOBS, &n)) return 1;
    printf("Loaded %d jobs from %s\n\n", n, file);
    log_event("SCHED", "%s run on %s (%d jobs)", argv[0], file, n);
    PolicyResult r;
    return run_policy(policy, jobs, n, q, &r, 1);
}

int cmd_fcfs(int argc, char **argv)     { return cmd_single(POL_FCFS, argc, argv); }
int cmd_rr(int argc, char **argv)       { return cmd_single(POL_RR, argc, argv); }
int cmd_priority(int argc, char **argv) { return cmd_single(POL_PRIO, argc, argv); }

int cmd_compare(int argc, char **argv)
{
    const char *file;
    int q;
    if (parse_args(argc, argv, &file, &q, 1)) return 2;
    Job jobs[MAX_JOBS];
    int n;
    if (load_jobs(file, jobs, MAX_JOBS, &n)) return 1;

    printf("=== Scheduling comparison: %d jobs from %s (RR quantum = %d) ===\n\n", n, file, q);
    printf("%-5s %-8s %-6s %-8s\n", "Job", "Arrival", "Burst", "Priority");
    for (int i = 0; i < n; i++)
        printf("J%-4d %-8d %-6d %-8d\n", jobs[i].id, jobs[i].arrival, jobs[i].burst, jobs[i].priority);
    printf("\n");

    PolicyResult r[3];
    run_policy(POL_FCFS, jobs, n, q, &r[0], 0);
    run_policy(POL_RR,   jobs, n, q, &r[1], 0);
    run_policy(POL_PRIO, jobs, n, q, &r[2], 0);

    printf("%-15s %-12s %-12s %-9s %s\n", "Policy", "Avg TAT", "Avg WT", "Makespan", "Execution order");
    printf("-------------------------------------------------------------------------------\n");
    int best = 0;
    for (int i = 0; i < 3; i++) {
        printf("%-15s %-12.2f %-12.2f %-9d %s\n", r[i].name, r[i].avg_tat, r[i].avg_wt, r[i].makespan, r[i].order);
        if (r[i].avg_wt < r[best].avg_wt) best = i;
    }
    printf("\nLowest average waiting time: %s (%.2f)\n", r[best].name, r[best].avg_wt);
    log_event("SCHED", "comparison on %s: best avg WT = %s", file, r[best].name);
    return 0;
}
