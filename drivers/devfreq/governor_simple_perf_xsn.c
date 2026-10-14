
/*
 * Simple Perf Xsn DevFreq governor
 * Based on the Linux simple_ondemand governor.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/math64.h>
#include "governor.h"

/*
 * Compile-time tuning.
 * Modify these values before building the kernel.
 */
#define SPX_UPTHRESHOLD          85
#define SPX_DOWNDIFFERENTIAL     10
#define SPX_TARGET_LOAD          \
	(SPX_UPTHRESHOLD - SPX_DOWNDIFFERENTIAL / 2)

static int simple_perf_xsn_get_target_freq(struct devfreq *df,
					   unsigned long *freq)
{
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	unsigned long max;
	unsigned int upthreshold = SPX_UPTHRESHOLD;
	unsigned int downdifferential = SPX_DOWNDIFFERENTIAL;
	unsigned int target_load = SPX_TARGET_LOAD;
	int ret;

	if (!df || !freq)
		return -EINVAL;

	if (upthreshold > 100 ||
	    !upthreshold ||
	    downdifferential >= upthreshold ||
	    !target_load || target_load > 100)
		return -EINVAL;

	ret = devfreq_update_stats(df);
	if (ret)
		return ret;

	stat = &df->last_status;
	max = df->max_freq ? df->max_freq : UINT_MAX;

	/* Unknown statistics: request the maximum allowed frequency. */
	if (!stat->total_time || !stat->current_frequency) {
		*freq = max;
		goto clamp;
	}

	/* Keep the load calculation within a safe range. */
	if (stat->busy_time > stat->total_time)
		stat->busy_time = stat->total_time;

	if (stat->busy_time >= (1UL << 24) ||
	    stat->total_time >= (1UL << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/* High load: request maximum frequency. */
	if (stat->busy_time * 100ULL >
	    stat->total_time * upthreshold) {
		*freq = max;
		goto clamp;
	}

	/* Hysteresis: retain the current frequency. */
	if (stat->busy_time * 100ULL >
	    stat->total_time * (upthreshold - downdifferential)) {
		*freq = stat->current_frequency;
		goto clamp;
	}

	/* Scale frequency toward the configured target load. */
	a = stat->busy_time;
	a *= stat->current_frequency;

	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, target_load);

	*freq = (unsigned long)b;

clamp:
	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;

	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	if (*freq > max)
		*freq = max;

	return 0;
}

static int simple_perf_xsn_handler(struct devfreq *df,
				   unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(df);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(df);
		break;

	case DEVFREQ_GOV_INTERVAL:
		devfreq_interval_update(df, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(df);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(df);
		break;

	default:
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_simple_perf_xsn = {
	.name = "simple_perf_xsn",
	.get_target_freq = simple_perf_xsn_get_target_freq,
	.event_handler = simple_perf_xsn_handler,
};

static int __init simple_perf_xsn_init(void)
{
	return devfreq_add_governor(&devfreq_simple_perf_xsn);
}
subsys_initcall(simple_perf_xsn_init);

static void __exit simple_perf_xsn_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_simple_perf_xsn);
	if (ret)
		pr_err("%s: failed to remove governor (%d)\n",
		       __func__, ret);
}
module_exit(simple_perf_xsn_exit);

MODULE_DESCRIPTION("Static Simple Perf Xsn DevFreq governor");
MODULE_AUTHOR("@NeverSannX");
MODULE_LICENSE("GPL");
