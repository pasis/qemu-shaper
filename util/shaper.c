#include <assert.h>		/* assert */
#include "qemu/shaper.h"
#include "qemu/timer.h"

#define NSEC_PER_SEC 1000000000LL

static void qemu_shaper_timer_rearm(QemuShaper *shaper);
static void qemu_shaper_timer_cb(void *opaque);

void qemu_shaper_init(QemuShaper *shaper, unsigned r, uint64_t limit)
{
	assert(r > 0);

	shaper->sh_bucket = limit;
	shaper->sh_limit  = limit;
	shaper->sh_r      = r;
	qemu_mutex_init(&shaper->sh_lock);
	timer_init_ns(&shaper->sh_timer, QEMU_CLOCK_REALTIME,
		      &qemu_shaper_timer_cb, shaper);
	qemu_shaper_timer_rearm(shaper);
}

void qemu_shaper_fini(QemuShaper *shaper)
{
	/* XXX Need to lock timer list to avoid race that can occur if shaper
	 * is finalised while main loop runs.
	 * Current user calls finalisation at exit, so race shouldn't appear.
	 */
	timer_del(&shaper->sh_timer);
	timer_deinit(&shaper->sh_timer);
	qemu_mutex_destroy(&shaper->sh_lock);
}

bool qemu_shaper_request(QemuShaper *shaper, uint64_t n)
{
	bool ret = false;

	qemu_mutex_lock(&shaper->sh_lock);
	if (shaper->sh_bucket >= n) {
		shaper->sh_bucket -= n;
		ret = true;
	}
	qemu_mutex_unlock(&shaper->sh_lock);

	return ret;
}

static void qemu_shaper_token_add(QemuShaper *shaper, uint64_t token)
{
	uint64_t bucket_new;

	bucket_new = shaper->sh_bucket + token;
	if (bucket_new > shaper->sh_limit || bucket_new < shaper->sh_bucket)
		shaper->sh_bucket = shaper->sh_limit;
	else
		shaper->sh_bucket = bucket_new;
}

static void qemu_shaper_timer_rearm(QemuShaper *shaper)
{
	int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

	timer_mod(&shaper->sh_timer, current_time + NSEC_PER_SEC / shaper->sh_r);
}

static void qemu_shaper_timer_cb(void *opaque)
{
	QemuShaper *shaper = (QemuShaper *)opaque;

	qemu_mutex_lock(&shaper->sh_lock);
	qemu_shaper_token_add(shaper, shaper->sh_limit / (uint64_t)shaper->sh_r);
	qemu_shaper_timer_rearm(shaper);
	qemu_mutex_unlock(&shaper->sh_lock);
}
