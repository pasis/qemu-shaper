#ifndef __QEMU_SHAPER_H__
#define __QEMU_SHAPER_H__

#include <stdbool.h>		/* bool */
#include <stdint.h>		/* uint64_t */
#include "qemu/thread.h"	/* QemuMutex */
#include "qemu/timer.h"		/* QEMUTimer */

/* Token bucket algorithm implementation. */

/* Built-in configuration */
#define QEMU_SHAPER_R         100
#define QEMU_SHAPER_LIMIT     (100 * 1000 * 1000)
#define QEMU_SHAPER_UNLIMITED (~((uint64_t)0))

typedef struct QemuShaper QemuShaper;

struct QemuShaper {
	/** Bucket contains available tokens. */
	uint64_t  sh_bucket;
	/** Maximum number of tokens. */
	uint64_t  sh_limit;
	/** How many times per second the bucket is filled by tokens. */
	unsigned  sh_r;
	QemuMutex sh_lock;
	QEMUTimer sh_timer;
};

void qemu_shaper_init(QemuShaper *shaper, unsigned r, uint64_t limit);
void qemu_shaper_fini(QemuShaper *shaper);
/**
 * Consumes n tokens if they are available. If there are not enough available
 * tokens user should make decision. For network traffic shaper it can be
 * dropping respective packet.
 *
 * @return true on success, false if tokens aren't consumed.
 */
bool qemu_shaper_request(QemuShaper *shaper, uint64_t n);

#endif /* __QEMU_SHAPER_H__ */
