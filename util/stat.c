#include <assert.h>		/* assert */
#include <errno.h>		/* ENOMEM */
#include <glib.h>
#include <string.h>		/* memset */
#include <time.h>		/* time */
#include <unistd.h>		/* sleep */
#include "qemu/stat.h"

#define NSEC_PER_SEC 1000000000LL

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif /* min */

#define qemu_zero(arr, nr) memset(arr, 0, (nr) * sizeof((arr)[0]))

int qemu_stat_init(QemuStat *stat, const char *name, unsigned sec_max)
{
	stat->st_name      = g_strdup(name);
	stat->st_current   = 0;
	stat->st_timestamp = time(NULL);
	stat->st_last      = stat->st_timestamp;
	stat->st_cbuf_nr   = sec_max + 2;
	stat->st_cbuf      = g_new0(uint64_t, stat->st_cbuf_nr);
	if (stat->st_cbuf != NULL)
		qemu_mutex_init(&stat->st_lock);

	return stat->st_cbuf == NULL ? -ENOMEM : 0;
}

void qemu_stat_fini(QemuStat *stat)
{
	qemu_mutex_destroy(&stat->st_lock);
	g_free(stat->st_cbuf);
	g_free(stat->st_name);
}

static void qemu_stat_update(QemuStat *stat)
{
	time_t   t = time(NULL);
	unsigned prev;

	if (t == stat->st_timestamp)
		return;

	assert(t > stat->st_timestamp);
	prev              = stat->st_current;
	stat->st_current += t - stat->st_timestamp;
	qemu_zero(&stat->st_cbuf[prev + 1],
		  min(stat->st_current, stat->st_cbuf_nr - 1) - prev);
	if (stat->st_current >= stat->st_cbuf_nr) {
		prev              = stat->st_current / stat->st_cbuf_nr;
		stat->st_current %= stat->st_cbuf_nr;
		if (prev > 1)
			qemu_zero(stat->st_cbuf, stat->st_cbuf_nr);
		else
			qemu_zero(stat->st_cbuf, stat->st_current + 1);
	}
	stat->st_timestamp = t;
}

void qemu_stat_accum(QemuStat *stat, uint64_t val)
{
	qemu_mutex_lock(&stat->st_lock);
	qemu_stat_update(stat);
	stat->st_cbuf[stat->st_current] += val;
	qemu_mutex_unlock(&stat->st_lock);
}

static uint64_t qemu_stat_get_unlocked(QemuStat *stat, unsigned sec)
{
	uint64_t v = 0;
	unsigned i;

	i = stat->st_current;
	for (; sec > 0; --sec) {
		i  = i == 0 ? stat->st_cbuf_nr - 1 : i - 1;
		v += stat->st_cbuf[i];
	}
	return v;
}

uint64_t qemu_stat_get(QemuStat *stat, unsigned sec)
{
	uint64_t v;

	assert(sec < stat->st_cbuf_nr);

	qemu_mutex_lock(&stat->st_lock);
	qemu_stat_update(stat);
	v = qemu_stat_get_unlocked(stat, sec);
	qemu_mutex_unlock(&stat->st_lock);

	return v;
}

void qemu_stat_new_get(QemuStat *stat, uint64_t *val, unsigned *sec)
{
	assert(stat->st_last <= stat->st_timestamp);

	qemu_mutex_lock(&stat->st_lock);
	qemu_stat_update(stat);
	*sec = min(stat->st_timestamp - stat->st_last, stat->st_cbuf_nr - 1);
	*val = qemu_stat_get_unlocked(stat, *sec);
	stat->st_last = stat->st_timestamp;
	qemu_mutex_unlock(&stat->st_lock);
}

static void qemu_stat_sched_timer_cb(void *opaque);

int qemu_stat_sched_init(QemuStatSched *sched, FILE *fd, unsigned interval)
{
	sched->sts_fd           = fd;
	sched->sts_interval     = interval;
	sched->sts_stat_list    = NULL;
	sched->sts_stat_nr      = 0;
	sched->sts_counter      = 0;
	sched->sts_timer_inited = false;
	qemu_mutex_init(&sched->sts_lock);

	return 0;
}

void qemu_stat_sched_fini(QemuStatSched *sched)
{
	/* QemuStatSched is finialised out of main loop. Therefore, timer
	 * can't be rised during finalisation.
	 */
	if (sched->sts_timer_inited) {
		timer_del(&sched->sts_timer);
		timer_deinit(&sched->sts_timer);
	}
	qemu_mutex_destroy(&sched->sts_lock);
}

static void qemu_stat_sched_timer_rearm(QemuStatSched *sched)
{
	int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

	timer_mod(&sched->sts_timer,
		  current_time + NSEC_PER_SEC * sched->sts_interval);
}

int qemu_stat_sched_launch(QemuStatSched *sched)
{
	sched->sts_timer_inited = true;
	timer_init_ns(&sched->sts_timer, QEMU_CLOCK_REALTIME,
		      &qemu_stat_sched_timer_cb, sched);
	qemu_stat_sched_timer_rearm(sched);
	return 0;
}

void qemu_stat_sched_add(QemuStatSched *sched, QemuStat *stat)
{
	qemu_mutex_lock(&sched->sts_lock);
	stat->st_next        = sched->sts_stat_list;
	sched->sts_stat_list = stat;
	++sched->sts_stat_nr;
	qemu_mutex_unlock(&sched->sts_lock);
}

QemuStatSched *qemu_stat_sched_default_get(void)
{
	static QemuStatSched sched = {};

	return &sched;
}

static void qemu_stat_sched_header_write(QemuStatSched *sched)
{
	QemuStat *stat;
	int       i;

	for (i = 0; i < sched->sts_stat_nr; ++i)
		printf("+------------------");
	fprintf(sched->sts_fd, "+\n");
	stat = sched->sts_stat_list;
	while (stat != NULL) {
		fprintf(sched->sts_fd, "| %16s ", stat->st_name);
		stat = stat->st_next;
	}
	fprintf(sched->sts_fd, "|\n");
	for (i = 0; i < sched->sts_stat_nr; ++i)
		printf("+------------------");
	fprintf(sched->sts_fd, "+\n");
}

static void qemu_stat_sched_timer_cb(void *opaque)
{
	static const char suffix[] = {' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
	QemuStatSched    *sched = (QemuStatSched *)opaque;
	QemuStat         *stat;
	uint64_t          val;
	unsigned          sec;
	int               i;

	qemu_mutex_lock(&sched->sts_lock);
	/* Repeat header every 21 records. So header is always visible
	 * even on 24 height terminals.
	 */
	if (sched->sts_counter == 0)
		qemu_stat_sched_header_write(sched);
	sched->sts_counter = (sched->sts_counter + 1) % 21;
	stat = sched->sts_stat_list;
	while (stat != NULL) {
		qemu_stat_new_get(stat, &val, &sec);
		val = sec == 0 ? 0 : val / (uint64_t)sec;
		i = 0;
		while (val >= 10000) {
			val /= 1000ULL;
			++i;
		}
		fprintf(sched->sts_fd, "| %15llu%c ",
			(unsigned long long)val, suffix[i]);
		stat = stat->st_next;
	}
	fprintf(sched->sts_fd, "|\n");
	qemu_stat_sched_timer_rearm(sched);
	qemu_mutex_unlock(&sched->sts_lock);
}
