/*
 * common.h - Shared types and helpers for CampusCompute.
 * Owner: Person A (agreed jointly with Person B).
 */
#ifndef CC_COMMON_H
#define CC_COMMON_H

#include <stdio.h>
#include <sys/types.h>

#define MAX_JOBS      64
#define MAX_TIMELINE  1024
#define DEFAULT_QUANTUM 2

/* FR1: lifecycle states a job moves through */
typedef enum { JOB_NEW = 0, JOB_READY, JOB_RUNNING, JOB_DONE } JobStatus;

typedef struct {
    int id;
    int arrival;
    int burst;
    int priority;       /* lower number = higher priority */
    /* filled in by the scheduler */
    int remaining;
    int start;          /* first time on CPU, -1 if never */
    int completion;
    int turnaround;
    int waiting;
    JobStatus status;
} Job;

/* One slice of the CPU timeline; job_id == -1 means CPU idle. */
typedef struct {
    int job_id;
    int start;
    int end;
} Segment;

/* util.c */
int  parse_int(const char *s, long min, long max, int *out);
const char *status_name(JobStatus s);

/* job.c */
int  load_jobs(const char *path, Job *jobs, int max, int *count);
void reset_jobs(Job *jobs, int n);

#endif
