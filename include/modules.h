/*
 * modules.h - Command entry points. Every command has the signature
 *   int cmd_xxx(int argc, char **argv)   where argv[0] is the command name.
 * Return 0 on success, non-zero on error.
 */
#ifndef CC_MODULES_H
#define CC_MODULES_H

/* ---- Person A ---- */
int cmd_fcfs(int argc, char **argv);
int cmd_rr(int argc, char **argv);
int cmd_priority(int argc, char **argv);
int cmd_compare(int argc, char **argv);
int cmd_banker(int argc, char **argv);
/* returns 1 = safe, 0 = unsafe, -1 = error */
int banker_check_file(const char *path);

/* ---- Person B ---- */
int cmd_logger(int argc, char **argv);
int cmd_ipc(int argc, char **argv);
int cmd_ipc_status(int argc, char **argv);
int cmd_worker(int argc, char **argv);
int cmd_rlimit(int argc, char **argv);
/* number of CampusCompute IPC objects left in /dev/shm (prints them if print=1) */
int ipc_count_leftovers(int print);

#endif
