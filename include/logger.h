/*
 * logger.h - Concurrent activity logger interface (FR3).
 * Owner: Person B. Person A's modules only call log_event().
 *
 * Any thread may call log_event(): it acts as a PRODUCER and places the
 * message into a bounded queue. A single CONSUMER thread drains the queue
 * and writes entries to the log file.
 */
#ifndef CC_LOGGER_H
#define CC_LOGGER_H

#define LOG_FILE_DEFAULT "campuscompute.log"
#define LOG_QUEUE_CAPACITY 4

int  logger_start(const char *path);           /* 0 on success */
void logger_stop(void);                        /* drains queue, joins thread */
void logger_flush(void);                       /* wait until queue is empty */
void logger_set_verbose(int on);               /* print producer/consumer activity */
void logger_set_thread_name(const char *name); /* label for the calling thread */
void log_event(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
