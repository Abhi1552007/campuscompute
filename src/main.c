/*
 * main.c - FR8: command-line interface, dispatch and integrated demo.
 * Owner: Person A (the 'demo' command is finished jointly).
 */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "logger.h"
#include "modules.h"

static int cmd_help(int argc, char **argv);
static int cmd_demo(int argc, char **argv);

typedef struct {
    const char *name;
    int (*fn)(int, char **);
    const char *usage;
    const char *desc;
} Command;

static const Command COMMANDS[] = {
    {"help",       cmd_help,       "help",                               "Show this help"},
    {"fcfs",       cmd_fcfs,       "fcfs <jobfile>",                     "FCFS scheduling (FR2)"},
    {"rr",         cmd_rr,         "rr <jobfile> [-q quantum]",          "Round Robin, default quantum 2 (FR2)"},
    {"priority",   cmd_priority,   "priority <jobfile>",                 "Non-preemptive priority scheduling (FR2)"},
    {"compare",    cmd_compare,    "compare <jobfile> [-q quantum]",     "Compare all three policies (FR2)"},
    {"banker",     cmd_banker,     "banker <statefile>",                 "Banker's safety algorithm (FR5)"},
    {"logger",     cmd_logger,     "logger [producers] [events_each]",   "Producer-consumer logger demo (FR3)"},
    {"ipc",        cmd_ipc,        "ipc [value ...]",                    "Shared memory + semaphores between processes (FR4)"},
    {"ipc-status", cmd_ipc_status, "ipc-status",                         "List IPC objects created by this program (FR7)"},
    {"worker",     cmd_worker,     "worker [count] [fail_index] [exit|signal]", "fork()/waitpid() workers, simulated failure (FR6)"},
    {"rlimit",     cmd_rlimit,     "rlimit [cpu|mem|all]",               "setrlimit() demonstration (FR7)"},
    {"demo",       cmd_demo,       "demo",                               "Integrated run of every feature with summary"},
};
#define NCOMMANDS ((int)(sizeof COMMANDS / sizeof COMMANDS[0]))

static void print_usage(FILE *out)
{
    fprintf(out, "CampusCompute - job scheduling and resource management prototype\n\n");
    fprintf(out, "Usage: ./campuscompute <command> [arguments]\n\nCommands:\n");
    for (int i = 0; i < NCOMMANDS; i++)
        fprintf(out, "  %-44s %s\n", COMMANDS[i].usage, COMMANDS[i].desc);
    fprintf(out, "\nExamples:\n"
                 "  ./campuscompute rr tests/jobs.txt -q 3\n"
                 "  ./campuscompute banker tests/safe.txt\n"
                 "  ./campuscompute worker 4 2 exit\n"
                 "Activity is logged to %s\n", LOG_FILE_DEFAULT);
}

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    print_usage(stdout);
    return 0;
}

/* ---------- integrated demo (S21) ---------- */

typedef struct { const char *name; const char *expect; int ok; } StepResult;

static int run_step(StepResult *r, const char *title, int (*fn)(int, char **), int argc, char **argv)
{
    printf("\n########## %s ##########\n", title);
    fflush(stdout);
    int rc = fn(argc, argv);
    fflush(stdout);
    r->name = title;
    r->ok = (rc == 0);
    return rc;
}

static int cmd_demo(int argc, char **argv)
{
    (void)argc; (void)argv;
    StepResult res[16];
    int k = 0;

    printf("=== CampusCompute integrated demonstration ===\n");
    log_event("DEMO", "integrated demo started");

    char *a_fcfs[] = {"fcfs", "tests/jobs.txt"};
    run_step(&res[k++], "FR1/FR2 FCFS", cmd_fcfs, 2, a_fcfs);
    char *a_cmp[] = {"compare", "tests/jobs.txt", "-q", "2"};
    run_step(&res[k++], "FR2 policy comparison", cmd_compare, 4, a_cmp);

    char *a_log[] = {"logger", "2", "4"};
    run_step(&res[k++], "FR3 producer-consumer logger", cmd_logger, 3, a_log);
    logger_set_verbose(0);

    char *a_ipc[] = {"ipc", "5", "3", "8", "6", "2"};
    run_step(&res[k++], "FR4 shared memory IPC", cmd_ipc, 6, a_ipc);

    printf("\n########## FR5 Banker's algorithm ##########\n");
    int s1 = banker_check_file("tests/safe.txt");
    int s2 = banker_check_file("tests/unsafe.txt");
    printf("tests/safe.txt   -> %s\n", s1 == 1 ? "SAFE" : s1 == 0 ? "UNSAFE" : "ERROR");
    printf("tests/unsafe.txt -> %s\n", s2 == 1 ? "SAFE" : s2 == 0 ? "UNSAFE" : "ERROR");
    res[k].name = "FR5 Banker safe state detected";   res[k++].ok = (s1 == 1);
    res[k].name = "FR5 Banker unsafe state detected"; res[k++].ok = (s2 == 0);

    char *a_w1[] = {"worker", "3"};
    run_step(&res[k++], "FR6 normal workers", cmd_worker, 2, a_w1);
    char *a_w2[] = {"worker", "4", "2", "exit"};
    run_step(&res[k++], "FR6 simulated failure detected", cmd_worker, 4, a_w2);

    char *a_rl[] = {"rlimit", "all"};
    run_step(&res[k++], "FR7 resource limits", cmd_rlimit, 2, a_rl);

    printf("\n########## FR7 cleanup verification ##########\n");
    int left = ipc_count_leftovers(1);
    res[k].name = "FR7 no IPC objects left behind"; res[k++].ok = (left == 0);

    int passed = 0;
    printf("\n=================== DEMO SUMMARY ===================\n");
    for (int i = 0; i < k; i++) {
        printf("  [%s] %s\n", res[i].ok ? "PASS" : "FAIL", res[i].name);
        passed += res[i].ok;
    }
    printf("----------------------------------------------------\n");
    printf("  %d/%d checks passed. Log written to %s\n", passed, k, LOG_FILE_DEFAULT);
    log_event("DEMO", "integrated demo finished: %d/%d checks passed", passed, k);
    return passed == k ? 0 : 1;
}

/* ---------- entry point ---------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    const Command *cmd = NULL;
    for (int i = 0; i < NCOMMANDS; i++)
        if (strcmp(argv[1], COMMANDS[i].name) == 0) cmd = &COMMANDS[i];

    if (!cmd) {
        fprintf(stderr, "Error: unknown command '%s'\n\n", argv[1]);
        print_usage(stderr);
        return 2;
    }
    if (cmd->fn == cmd_help) return cmd_help(argc - 1, argv + 1);

    if (logger_start(LOG_FILE_DEFAULT) != 0)
        fprintf(stderr, "Warning: logger could not start; continuing without a log file\n");
    log_event("MAIN", "command '%s' started", cmd->name);

    int rc = cmd->fn(argc - 1, argv + 1);

    log_event("MAIN", "command '%s' finished with status %d", cmd->name, rc);
    logger_stop();
    return rc;
}
