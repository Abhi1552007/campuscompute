/*
 * job.c - FR1: job representation, input loading and validation.
 * Owner: Person A.
 *
 * Job file format (one job per line, '#' starts a comment):
 *     <id> <arrival> <burst> <priority>
 */
#include <errno.h>
#include <string.h>
#include "common.h"

#define MAX_FIELDS 8

void reset_jobs(Job *jobs, int n)
{
    for (int i = 0; i < n; i++) {
        jobs[i].remaining  = jobs[i].burst;
        jobs[i].start      = -1;
        jobs[i].completion = 0;
        jobs[i].turnaround = 0;
        jobs[i].waiting    = 0;
        jobs[i].status     = JOB_NEW;
    }
}

int load_jobs(const char *path, Job *jobs, int max, int *count)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char line[512];
    int lineno = 0, n = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *tok[MAX_FIELDS];
        int nt = 0;
        for (char *t = strtok(line, " \t\r\n"); t && nt < MAX_FIELDS; t = strtok(NULL, " \t\r\n"))
            tok[nt++] = t;
        if (nt == 0)
            continue;                       /* blank / comment line */

        if (nt != 4) {
            fprintf(stderr, "Error: %s line %d: expected 4 fields <id> <arrival> <burst> <priority>, got %d\n",
                    path, lineno, nt);
            goto fail;
        }
        if (n >= max) {
            fprintf(stderr, "Error: %s: too many jobs (max %d)\n", path, max);
            goto fail;
        }

        Job j;
        memset(&j, 0, sizeof j);
        if (parse_int(tok[0], 1, 9999, &j.id)) {
            fprintf(stderr, "Error: %s line %d: job id '%s' must be an integer 1..9999\n", path, lineno, tok[0]);
            goto fail;
        }
        if (parse_int(tok[1], 0, 100000, &j.arrival)) {
            fprintf(stderr, "Error: %s line %d: arrival '%s' must be an integer >= 0\n", path, lineno, tok[1]);
            goto fail;
        }
        if (parse_int(tok[2], 1, 100000, &j.burst)) {
            fprintf(stderr, "Error: %s line %d: CPU burst '%s' must be an integer >= 1\n", path, lineno, tok[2]);
            goto fail;
        }
        if (parse_int(tok[3], 0, 99, &j.priority)) {
            fprintf(stderr, "Error: %s line %d: priority '%s' must be an integer 0..99\n", path, lineno, tok[3]);
            goto fail;
        }
        for (int k = 0; k < n; k++) {
            if (jobs[k].id == j.id) {
                fprintf(stderr, "Error: %s line %d: duplicate job id %d\n", path, lineno, j.id);
                goto fail;
            }
        }
        jobs[n++] = j;
    }
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "Error: %s contains no jobs\n", path);
        return -1;
    }
    reset_jobs(jobs, n);
    *count = n;
    return 0;

fail:
    fclose(f);
    return -1;
}
