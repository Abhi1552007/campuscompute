# CampusCompute

Intelligent job scheduling and resource management prototype in C for Linux/Ubuntu (OS UE25CS242A, Level 3 mini-project).

## Prerequisites
- Ubuntu 22.04+ or WSL Ubuntu
- `gcc`, `make`, `zip`, `unzip`:
  ```bash
  sudo apt update && sudo apt install -y build-essential zip unzip
  ```

## Build
```bash
make            # builds ./campuscompute
make clean      # removes build files, executable and log
```

## Commands
| Command | Feature |
|---|---|
| `./campuscompute help` | Usage / list of commands (FR8) |
| `./campuscompute fcfs tests/jobs.txt` | FCFS scheduling (FR1, FR2) |
| `./campuscompute rr tests/jobs.txt -q 2` | Round Robin, quantum 2 by default (FR2) |
| `./campuscompute priority tests/jobs.txt` | Non-preemptive priority (FR2) |
| `./campuscompute compare tests/jobs.txt -q 2` | All three policies on the same jobs (FR2) |
| `./campuscompute logger 3 5` | Producer-consumer logger: 3 producers x 5 events (FR3) |
| `./campuscompute ipc 5 3 8 6 2` | Shared memory + named semaphores between processes (FR4) |
| `./campuscompute banker tests/safe.txt` | Banker's algorithm, safe state (FR5) |
| `./campuscompute banker tests/unsafe.txt` | Banker's algorithm, unsafe state (FR5) |
| `./campuscompute worker 3` | fork()/waitpid() with normal completion (FR6) |
| `./campuscompute worker 4 2 exit` | Worker 2 exits with code 3 (SIMULATED failure) (FR6) |
| `./campuscompute worker 4 3 signal` | Worker 3 calls abort() (SIMULATED failure) (FR6) |
| `./campuscompute rlimit all` | RLIMIT_CPU and RLIMIT_AS in a child process (FR7) |
| `./campuscompute ipc-status` | Lists any IPC objects this program created (FR7) |
| `./campuscompute demo` | Integrated run of everything plus a PASS/FAIL summary |

All activity is appended to `campuscompute.log`.

## Rules used
- **FCFS tie-break:** earlier arrival first, then lower job ID.
- **Round Robin:** default time quantum is **2** (change it with `-q N`). Jobs that arrive during a time slice join the queue before the preempted job.
- **Priority:** non-preemptive, and a lower number means a higher priority. Ties go to the earlier arrival, then the lower job ID.
- **Banker:** Need = Max − Allocation. Each step picks the lowest-numbered process whose Need ≤ Work.
- When no job is ready, the CPU is shown as `idle` until the next arrival.

## Input formats
- **Job file:** one job per line, as `<id> <arrival> <burst> <priority>`. A `#` starts a comment.
- **Banker file:** `n m`, then Available (m values), then Allocation (n rows), then Maximum (n rows).

## Simulated failures
Worker failures are deliberate and chosen on the command line, and the program labels them `SIMULATED`. They are not real OS failures.

## Cleanup
- The program removes everything it creates. Threads are joined, child processes are reaped, and shared memory and semaphores are unlinked.
- IPC objects are named `/cc_shm_<pid>`, `/cc_req_<pid>` and `/cc_resp_<pid>`, so only this program's own objects are touched.
- To check: `./campuscompute ipc-status` and `ls /dev/shm`.
- If the program was killed with `kill -9`, remove any leftovers with `rm -f /dev/shm/cc_* /dev/shm/sem.cc_*`.

## Files
```
Makefile  README.md  TEAM_GUIDE.md
include/  common.h logger.h modules.h
src/      main.c util.c job.c scheduler.c banker.c        (Person A)
          logger.c ipc.c worker.c rlimit_demo.c           (Person B)
tests/    jobs.txt bad_jobs.txt malformed_jobs.txt safe.txt unsafe.txt
```
