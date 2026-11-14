/*
 * util.c - Small shared helpers (input validation).
 * Owner: Person A (shared by both).
 */
#include <errno.h>
#include <stdlib.h>
#include "common.h"

/* Strict integer parse: whole string must be a number within [min,max].
 * Returns 0 on success, -1 on malformed/out-of-range input. */
int parse_int(const char *s, long min, long max, int *out)
{
    if (s == NULL || *s == '\0')
        return -1;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < min || v > max)
        return -1;
    *out = (int)v;
    return 0;
}

const char *status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}
