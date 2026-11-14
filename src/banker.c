/*
 * banker.c - FR5: Banker's safety algorithm.
 * Owner: Person A.
 *
 * State file format ('#' starts a comment, whitespace separated integers):
 *   n m                      (processes, resource types)
 *   Available[m]
 *   Allocation[n][m]         (n rows)
 *   Maximum[n][m]            (n rows)
 *
 * Need = Maximum - Allocation.
 * Search rule: each step picks the LOWEST-numbered unfinished process whose
 * Need <= Work, then restarts the scan from P0.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

#define MAX_P 10
#define MAX_R 10

typedef struct {
    int n, m;
    int avail[MAX_R];
    int alloc[MAX_P][MAX_R];
    int max[MAX_P][MAX_R];
    int need[MAX_P][MAX_R];
} BankState;

/* Read every integer token from the file, skipping comments. */
static int read_ints(const char *path, int *vals, int cap, int *count)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open state file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    char line[512];
    int lineno = 0, k = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        for (char *t = strtok(line, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) {
            int v;
            if (parse_int(t, 0, 1000000, &v)) {
                fprintf(stderr, "Error: %s line %d: '%s' is not a non-negative integer\n", path, lineno, t);
                fclose(f);
                return -1;
            }
            if (k >= cap) {
                fprintf(stderr, "Error: %s: too many values\n", path);
                fclose(f);
                return -1;
            }
            vals[k++] = v;
        }
    }
    fclose(f);
    *count = k;
    return 0;
}

static int load_state(const char *path, BankState *s)
{
    int vals[2 + MAX_R + 2 * MAX_P * MAX_R + 8];
    int k;
    if (read_ints(path, vals, (int)(sizeof vals / sizeof vals[0]), &k)) return -1;
    if (k < 2) {
        fprintf(stderr, "Error: %s: missing 'n m' header\n", path);
        return -1;
    }
    s->n = vals[0];
    s->m = vals[1];
    if (s->n < 1 || s->n > MAX_P || s->m < 1 || s->m > MAX_R) {
        fprintf(stderr, "Error: %s: n must be 1..%d and m must be 1..%d\n", path, MAX_P, MAX_R);
        return -1;
    }
    int expected = 2 + s->m + 2 * s->n * s->m;
    if (k != expected) {
        fprintf(stderr, "Error: %s: expected %d numbers for n=%d, m=%d but found %d\n",
                path, expected, s->n, s->m, k);
        return -1;
    }
    int p = 2;
    for (int j = 0; j < s->m; j++) s->avail[j] = vals[p++];
    for (int i = 0; i < s->n; i++) for (int j = 0; j < s->m; j++) s->alloc[i][j] = vals[p++];
    for (int i = 0; i < s->n; i++) for (int j = 0; j < s->m; j++) s->max[i][j] = vals[p++];

    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            if (s->alloc[i][j] > s->max[i][j]) {
                fprintf(stderr, "Error: %s: P%d allocation of R%d (%d) exceeds its maximum (%d)\n",
                        path, i, j, s->alloc[i][j], s->max[i][j]);
                return -1;
            }
            s->need[i][j] = s->max[i][j] - s->alloc[i][j];
        }
    return 0;
}

static void print_vec(const int *v, int m)
{
    printf("(");
    for (int j = 0; j < m; j++) printf("%d%s", v[j], j + 1 < m ? " " : "");
    printf(")");
}

static void print_state(const BankState *s)
{
    printf("Processes: %d   Resource types: %d\n", s->n, s->m);
    printf("Available: ");
    print_vec(s->avail, s->m);
    printf("\n\n%-6s %-*s %-*s %-*s\n", "Proc", s->m * 3 + 4, "Allocation", s->m * 3 + 4, "Maximum",
           s->m * 3 + 4, "Need");
    for (int i = 0; i < s->n; i++) {
        printf("P%-5d ", i);
        for (int j = 0; j < s->m; j++) printf("%3d", s->alloc[i][j]);
        printf("     ");
        for (int j = 0; j < s->m; j++) printf("%3d", s->max[i][j]);
        printf("     ");
        for (int j = 0; j < s->m; j++) printf("%3d", s->need[i][j]);
        printf("\n");
    }
    printf("\n");
}

/* The safety algorithm. Returns 1 if safe (seq filled), 0 if unsafe. */
static int is_safe(const BankState *s, int *seq, int print)
{
    int work[MAX_R], finish[MAX_P] = {0}, count = 0;
    memcpy(work, s->avail, sizeof work);
    if (print) { printf("Work = Available = "); print_vec(work, s->m); printf("\n"); }

    while (count < s->n) {
        int found = -1;
        for (int i = 0; i < s->n && found < 0; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < s->m; j++)
                if (s->need[i][j] > work[j]) { ok = 0; break; }
            if (ok) found = i;
        }
        if (found < 0) break;

        if (print) {
            printf("Step %d: P%d Need ", count + 1, found);
            print_vec(s->need[found], s->m);
            printf(" <= Work ");
            print_vec(work, s->m);
            printf(" -> P%d runs and releases ", found);
            print_vec(s->alloc[found], s->m);
        }
        for (int j = 0; j < s->m; j++) work[j] += s->alloc[found][j];
        finish[found] = 1;
        seq[count++] = found;
        if (print) { printf(", Work = "); print_vec(work, s->m); printf("\n"); }
    }

    if (count == s->n) return 1;

    if (print) {
        printf("No unfinished process has Need <= Work ");
        print_vec(work, s->m);
        printf(".\nBlocked processes:");
        for (int i = 0; i < s->n; i++) {
            if (finish[i]) continue;
            printf(" P%d Need", i);
            print_vec(s->need[i], s->m);
        }
        printf("\n");
    }
    return 0;
}

int banker_check_file(const char *path)
{
    BankState s;
    if (load_state(path, &s)) return -1;
    int seq[MAX_P];
    return is_safe(&s, seq, 0);
}

int cmd_banker(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: campuscompute banker <statefile>\n");
        return 2;
    }
    BankState s;
    if (load_state(argv[1], &s)) return 1;

    printf("=== Banker's Safety Algorithm: %s ===\n", argv[1]);
    printf("Need = Maximum - Allocation. Rule: pick lowest-numbered process with Need <= Work.\n\n");
    print_state(&s);

    int seq[MAX_P];
    int safe = is_safe(&s, seq, 1);
    printf("\n");
    if (safe) {
        printf("RESULT: SAFE state. Safe sequence: < ");
        for (int i = 0; i < s.n; i++) printf("P%d%s", seq[i], i + 1 < s.n ? ", " : " ");
        printf(">\n");
        log_event("BANKER", "%s: SAFE", argv[1]);
    } else {
        printf("RESULT: UNSAFE state. No safe sequence exists; granting these requests could lead to deadlock.\n");
        log_event("BANKER", "%s: UNSAFE", argv[1]);
    }
    return 0;
}
