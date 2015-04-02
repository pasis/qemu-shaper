#ifndef __QEMU_STAT_H__
#define __QEMU_STAT_H__

#include <stdbool.h>		/* bool */
#include <stdint.h>		/* uint64_t */
#include <stdio.h>		/* FILE */
#include <time.h>		/* time_t */
#include "qemu/thread.h"	/* QemuMutex */
#include "qemu/timer.h"		/* QEMUTimer */

/* Statistics collector. */

/* Built-in configuration */
enum {
	QEMU_STAT_INTERVAL = 5,
};

typedef struct QemuStat QemuStat;

struct QemuStat {
	char      *st_name;
	/** Circular buffer. */
	uint64_t  *st_cbuf;
	unsigned   st_cbuf_nr;
	/** Current position in the circular buffer. */
	unsigned   st_current;
	/** Timestamp of the current position. */
	time_t     st_timestamp;
	/** Timestamp of the last call of qemu_stat_new_get(). */
	time_t     st_last;
	QemuMutex  st_lock;
	QemuStat  *st_next;
};

int qemu_stat_init(QemuStat *stat, const char *name, unsigned sec_max);
void qemu_stat_fini(QemuStat *stat);
/** Collect val for the current second. */
void qemu_stat_accum(QemuStat *stat, uint64_t val);
/** Returns amount of data that was collected for the last full sec seconds. */
uint64_t qemu_stat_get(QemuStat *stat, unsigned sec);
/**
 * Returns value and number os seconds that has been collected since previous
 * call of this function.
 */
void qemu_stat_new_get(QemuStat *stat, uint64_t *val, unsigned *sec);

/* Statistics logger. */

typedef struct QemuStatSched QemuStatSched;

struct QemuStatSched {
	/** File descriptor where statistics is written to. */
	FILE       *sts_fd;
	/** Interval in seconds between writes. */
	unsigned    sts_interval;
	/** List of collectors that are logged. */
	QemuStat   *sts_stat_list;
	int         sts_stat_nr;
	/** Used for periodic writes of header. */
	int         sts_counter;
	QemuMutex   sts_lock;
	QEMUTimer   sts_timer;
	bool        sts_timer_inited;
};

int qemu_stat_sched_init(QemuStatSched *sched, FILE *fd, unsigned interval);
void qemu_stat_sched_fini(QemuStatSched *sched);
int qemu_stat_sched_launch(QemuStatSched *sched);
void qemu_stat_sched_add(QemuStatSched *sched, QemuStat *stat);
QemuStatSched *qemu_stat_sched_default_get(void);

#endif /* __QEMU_STAT_H__ */
