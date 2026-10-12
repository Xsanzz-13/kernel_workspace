/*
 * Simple Perf Xsn DevFreq governor.
 * Derived from the Linux simple_ondemand governor.
 *
 * The original simple_ondemand implementation remains unchanged.
 * Runtime settings are maintained separately for each DevFreq device.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/timekeeping.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "governor.h"

#define SPX_DEFAULT_UPTHRESHOLD       85
#define SPX_DEFAULT_DOWNDIFFERENTIAL  10
#define SPX_DEFAULT_TARGET_LOAD       80
#define SPX_MAX_RATE_LIMIT_US         10000000

struct simple_perf_xsn_data {
	unsigned int upthreshold;
	unsigned int downdifferential;
	unsigned int target_load;
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	unsigned int boost_enable;
	unsigned int boost_threshold;

	u64 last_up_ns;
	u64 last_down_ns;
	bool sysfs_created;
};

/*
 * Preserve compatibility with drivers passing the standard two-field
 * simple_ondemand configuration through devfreq_add_device().
 * Do not overwrite devfreq->data with governor-owned state.
 */

enum spx_param {
	SPX_PARAM_UPTHRESHOLD,
	SPX_PARAM_DOWNDIFFERENTIAL,
	SPX_PARAM_TARGET_LOAD,
	SPX_PARAM_UP_RATE_LIMIT,
	SPX_PARAM_DOWN_RATE_LIMIT,
	SPX_PARAM_BOOST_ENABLE,
	SPX_PARAM_BOOST_THRESHOLD,
};

static unsigned int spx_get_param(const struct simple_perf_xsn_data *cfg,
				  enum spx_param param)
{
	switch (param) {
	case SPX_PARAM_UPTHRESHOLD:
		return cfg->upthreshold;
	case SPX_PARAM_DOWNDIFFERENTIAL:
		return cfg->downdifferential;
	case SPX_PARAM_TARGET_LOAD:
		return cfg->target_load;
	case SPX_PARAM_UP_RATE_LIMIT:
		return cfg->up_rate_limit_us;
	case SPX_PARAM_DOWN_RATE_LIMIT:
		return cfg->down_rate_limit_us;
	case SPX_PARAM_BOOST_ENABLE:
		return cfg->boost_enable;
	case SPX_PARAM_BOOST_THRESHOLD:
		return cfg->boost_threshold;
	default:
		return 0;
	}
}

static bool spx_valid_param(const struct simple_perf_xsn_data *cfg,
			    enum spx_param param, unsigned int value)
{
	switch (param) {
	case SPX_PARAM_UPTHRESHOLD:
		return value > cfg->downdifferential;
	case SPX_PARAM_DOWNDIFFERENTIAL:
		return value < cfg->upthreshold;
	default:
		return true;
	}
}

static void spx_set_param(struct simple_perf_xsn_data *cfg,
			  enum spx_param param, unsigned int value)
{
	switch (param) {
	case SPX_PARAM_UPTHRESHOLD:
		cfg->upthreshold = value;
		break;
	case SPX_PARAM_DOWNDIFFERENTIAL:
		cfg->downdifferential = value;
		break;
	case SPX_PARAM_TARGET_LOAD:
		cfg->target_load = value;
		break;
	case SPX_PARAM_UP_RATE_LIMIT:
		cfg->up_rate_limit_us = value;
		break;
	case SPX_PARAM_DOWN_RATE_LIMIT:
		cfg->down_rate_limit_us = value;
		break;
	case SPX_PARAM_BOOST_ENABLE:
		cfg->boost_enable = value;
		break;
	case SPX_PARAM_BOOST_THRESHOLD:
		cfg->boost_threshold = value;
		break;
	}
}

static ssize_t spx_show_value(struct device *dev, char *buf,
			      enum spx_param param)
{
	struct devfreq *df = to_devfreq(dev);
	struct simple_perf_xsn_data *cfg;
	ssize_t ret;

	mutex_lock(&df->lock);
	cfg = df->governor_data;
	if (!cfg) {
		ret = -EINVAL;
	} else {
		ret = scnprintf(buf, PAGE_SIZE, "%u\n",
				spx_get_param(cfg, param));
	}
	mutex_unlock(&df->lock);

	return ret;
}

static ssize_t spx_store_value(struct device *dev, const char *buf,
			       size_t count, enum spx_param param,
			       unsigned int min, unsigned int max)
{
	struct devfreq *df = to_devfreq(dev);
	struct simple_perf_xsn_data *cfg;
	unsigned int value;
	unsigned int old_value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	if (value < min || value > max)
		return -EINVAL;

	mutex_lock(&df->lock);
	cfg = df->governor_data;
	if (!cfg) {
		ret = -EINVAL;
		goto out;
	}

	if (!spx_valid_param(cfg, param, value)) {
		ret = -EINVAL;
		goto out;
	}

	old_value = spx_get_param(cfg, param);
	spx_set_param(cfg, param, value);

	/*
	 * Apply the new setting immediately. Roll back the configuration
	 * if devfreq cannot apply the resulting target.
	 */
	ret = update_devfreq(df);
	if (ret) {
		spx_set_param(cfg, param, old_value);
		goto out;
	}

	ret = count;
out:
	mutex_unlock(&df->lock);
	return ret;
}

#define SPX_ATTR(_name, _param, _min, _max)				\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	return spx_show_value(dev, buf, _param);				\
}									\
static ssize_t _name##_store(struct device *dev,			\
			     struct device_attribute *attr,		\
			     const char *buf, size_t count)		\
{									\
	return spx_store_value(dev, buf, count, _param, _min, _max);	\
}									\
static DEVICE_ATTR(_name, 0644, _name##_show, _name##_store)

SPX_ATTR(upthreshold, SPX_PARAM_UPTHRESHOLD, 1, 100);
SPX_ATTR(downdifferential, SPX_PARAM_DOWNDIFFERENTIAL, 0, 99);
SPX_ATTR(target_load, SPX_PARAM_TARGET_LOAD, 1, 100);
SPX_ATTR(up_rate_limit_us, SPX_PARAM_UP_RATE_LIMIT, 0,
	 SPX_MAX_RATE_LIMIT_US);
SPX_ATTR(down_rate_limit_us, SPX_PARAM_DOWN_RATE_LIMIT, 0,
	 SPX_MAX_RATE_LIMIT_US);
SPX_ATTR(boost_enable, SPX_PARAM_BOOST_ENABLE, 0, 1);
SPX_ATTR(boost_threshold, SPX_PARAM_BOOST_THRESHOLD, 1, 100);

static struct attribute *spx_attrs[] = {
	&dev_attr_upthreshold.attr,
	&dev_attr_downdifferential.attr,
	&dev_attr_target_load.attr,
	&dev_attr_up_rate_limit_us.attr,
	&dev_attr_down_rate_limit_us.attr,
	&dev_attr_boost_enable.attr,
	&dev_attr_boost_threshold.attr,
	NULL,
};

static const struct attribute_group spx_attr_group = {
	.name = "simple_perf_xsn",
	.attrs = spx_attrs,
};

static int simple_perf_xsn_start(struct devfreq *df)
{
	struct simple_perf_xsn_data *cfg;
	struct devfreq_simple_ondemand_data *legacy = df->data;
	int ret;

	if (df->governor_data)
		return -EBUSY;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	cfg->upthreshold = SPX_DEFAULT_UPTHRESHOLD;
	cfg->downdifferential = SPX_DEFAULT_DOWNDIFFERENTIAL;
	cfg->target_load = SPX_DEFAULT_TARGET_LOAD;
	cfg->up_rate_limit_us = 0;
	cfg->down_rate_limit_us = 0;
	cfg->boost_enable = 0;
	cfg->boost_threshold = 70;

	/*
	 * Import valid legacy settings when supplied by the device driver.
	 * The sysfs values then become the active runtime configuration.
	 */
	if (legacy) {
		if (legacy->upthreshold)
			cfg->upthreshold = legacy->upthreshold;
		if (legacy->downdifferential)
			cfg->downdifferential = legacy->downdifferential;
	}

	if (!cfg->upthreshold || cfg->upthreshold > 100 ||
	    cfg->downdifferential >= cfg->upthreshold) {
		cfg->upthreshold = SPX_DEFAULT_UPTHRESHOLD;
		cfg->downdifferential = SPX_DEFAULT_DOWNDIFFERENTIAL;
	}

	cfg->target_load = cfg->upthreshold -
			   (cfg->downdifferential / 2);

	df->governor_data = cfg;

	ret = sysfs_create_group(&df->dev.kobj, &spx_attr_group);
	if (ret)
		goto err_free;

	cfg->sysfs_created = true;
	devfreq_monitor_start(df);

	return 0;

err_free:
	df->governor_data = NULL;
	kfree(cfg);
	return ret;
}

static void simple_perf_xsn_stop(struct devfreq *df)
{
	struct simple_perf_xsn_data *cfg = df->governor_data;

	devfreq_monitor_stop(df);

	if (!cfg)
		return;

	if (cfg->sysfs_created && df->dev.kobj.sd)
		sysfs_remove_group(&df->dev.kobj, &spx_attr_group);

	df->governor_data = NULL;
	kfree(cfg);
}

static int simple_perf_xsn_get_target_freq(struct devfreq *df,
					   unsigned long *freq)
{
	struct devfreq_dev_status *stat;
	struct simple_perf_xsn_data *cfg;
	u64 busy, total, target;
	u64 now, limit_ns;
	unsigned int load;
	unsigned long max;
	unsigned long previous;
	int ret;

	if (!df || !freq)
		return -EINVAL;

	cfg = df->governor_data;
	if (!cfg)
		return -EINVAL;

	max = df->max_freq ? df->max_freq : ~0UL;

	ret = devfreq_update_stats(df);
	if (ret)
		return ret;

	stat = &df->last_status;
	busy = stat->busy_time;
	total = stat->total_time;

	if (!total || !stat->current_frequency) {
		target = max;
		goto clamp;
	}

	if (busy > total)
		busy = total;

	/*
	 * Calculate utilization without overflowing busy * 100.
	 */
	while (total > (~0ULL / 100)) {
		busy >>= 1;
		total >>= 1;
		if (!total) {
			target = max;
			goto clamp;
		}
	}

	load = (unsigned int)div64_u64(busy * 100, total);

	/*
	 * Boost takes priority over the normal upthreshold.
	 * It requests the device's maximum allowed frequency.
	 */
	if ((cfg->boost_enable && load >= cfg->boost_threshold) ||
	    load >= cfg->upthreshold) {
		target = max;
		goto clamp;
	}

	/*
	 * Hysteresis: hold the current frequency in the upper load band.
	 */
	if (load > cfg->upthreshold - cfg->downdifferential) {
		target = stat->current_frequency;
		goto clamp;
	}

	/*
	 * Scale current frequency toward the configured target utilization.
	 */
	target = (u64)load * stat->current_frequency;
	target = div64_u64(target, cfg->target_load);

clamp:
	if (df->min_freq && target < df->min_freq)
		target = df->min_freq;
	if (df->max_freq && target > df->max_freq)
		target = df->max_freq;
	if (target > max)
		target = max;

	/*
	 * Rate limits apply to changes relative to the last frequency
	 * requested by the devfreq core. A zero limit disables that limit.
	 */
	previous = df->previous_freq;
	if (!previous)
		previous = stat->current_frequency;

	if (previous && target != previous) {
		now = ktime_get_ns();

		if (target > previous) {
			limit_ns = (u64)cfg->up_rate_limit_us * 1000;

			if (limit_ns && cfg->last_up_ns &&
			    now - cfg->last_up_ns < limit_ns) {
				target = previous;
			} else {
				cfg->last_up_ns = now;
			}
		} else {
			limit_ns = (u64)cfg->down_rate_limit_us * 1000;

			if (limit_ns && cfg->last_down_ns &&
			    now - cfg->last_down_ns < limit_ns) {
				target = previous;
			} else {
				cfg->last_down_ns = now;
			}
		}
	}

	*freq = (unsigned long)target;
	return 0;
}

static int simple_perf_xsn_event_handler(struct devfreq *df,
					 unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		return simple_perf_xsn_start(df);

	case DEVFREQ_GOV_STOP:
		simple_perf_xsn_stop(df);
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
	.event_handler = simple_perf_xsn_event_handler,
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

MODULE_DESCRIPTION("Runtime-configurable Simple Perf Xsn DevFreq governor");
MODULE_AUTHOR("Xsann");
MODULE_LICENSE("GPL");
