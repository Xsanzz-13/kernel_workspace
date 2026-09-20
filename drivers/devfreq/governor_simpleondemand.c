/*
 *  linux/drivers/devfreq/governor_simpleondemand.c
 *
 *  Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/math64.h>
#include "governor.h"

/*
 * ================================================================
 *                 simpe_ondemand tune @Xsann
 * ================================================================
 *
 * UPTHRESHOLD = 100
 *
 * Jangan dinaikkan >100 karena source asli governor memang
 * membatasi threshold maksimum 100%.
 *
 * DOWNDIFFERENTIAL = 20
 *
 * Dengan UP=100 dan DOWN=20:
 *
 *     100 - 20 = 80
 *
 * Artinya load >80% akan mempertahankan frequency saat ini.
 *
 * ================================================================
 */
#define DFSO_UPTHRESHOLD		(100)
#define DFSO_DOWNDIFFERENCTIAL		(20)

/*
 * ================================================================
 *                 TUNING TAMBAHAN SAMA BIJI KONTOL 10
 * ================================================================
 *
 * GAMING_MAX_LOAD = 80
 *
 * Kalau utilization >=80%, langsung minta MAX frequency.
 *
 * Ini membuat spike berat seperti particle/effect/skill tidak
 * menunggu kalkulasi frequency normal.
 */
#define DFSO_GAMING_MAX_LOAD		(80)

/*
 * GAMING_HOLD_LOAD = 60
 *
 * Kalau utilization >=60%, pertahankan current frequency.
 *
 * Jadi frequency tidak gampang turun hanya karena load turun
 * sebentar di antara dua frame.
 */
#define DFSO_GAMING_HOLD_LOAD		(60)

/*
 * GAMING_BOOST_PERCENT = 15
 *
 * Hasil kalkulasi frequency normal diberi tambahan 15%.
 *
 * Contoh:
 *
 *     hasil normal = 400 MHz
 *     boost 15%    = 460 MHz
 *
 * Nilai tetap dibatasi oleh max_freq.
 */
#define DFSO_GAMING_BOOST_PERCENT	(15)

/*
 * =================================================================
 *                      GOVERNOR FUNCTION
 * =================================================================
 */
static int devfreq_simple_ondemand_func(struct devfreq *df,
					unsigned long *freq)
{
	int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	unsigned int dfso_upthreshold = DFSO_UPTHRESHOLD;
	unsigned int dfso_downdifferential = DFSO_DOWNDIFFERENCTIAL;
	struct devfreq_simple_ondemand_data *data = df->data;
	unsigned long max = (df->max_freq) ? df->max_freq : UINT_MAX;

	err = devfreq_update_stats(df);
	if (err)
		return err;

	stat = &df->last_status;

	/*
	 * ============================================================
	 * Ambil setting governor dari devfreq data jika tersedia.
	 * ============================================================
	 */
	if (data) {
		if (data->upthreshold)
			dfso_upthreshold = data->upthreshold;

		if (data->downdifferential)
			dfso_downdifferential = data->downdifferential;
	}

	/*
	 * Validasi threshold.
	 *
	 * UPTHRESHOLD memang tidak boleh >100 karena utilization
	 * dihitung dalam rentang 0-100%.
	 */
	if (dfso_upthreshold > 100 ||
	    dfso_upthreshold < dfso_downdifferential)
		return -EINVAL;

	/*
	 * Kalau total_time = 0, statistik tidak valid.
	 *
	 * Dalam kondisi ini langsung gunakan MAX agar tidak
	 * mengambil keputusan berdasarkan data kosong.
	 */
	if (stat->total_time == 0) {
		*freq = max;
		return 0;
	}

	/*
	 * ============================================================
	 * Hindari overflow pada perhitungan busy_time * 100.
	 * ============================================================
	 */
	if (stat->busy_time >= (1 << 24) ||
	    stat->total_time >= (1 << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/*
	 * ============================================================
	 * HITUNG UTILIZATION
	 * ============================================================
	 *
	 * utilization = busy_time / total_time
	 *
	 * Kita menggunakan perkalian 100 agar hasil dalam persen.
	 */
	a = stat->busy_time * 100;
	b = div_u64(a, stat->total_time);

	/*
	 * ============================================================
	 * GAMING MODE 1:
	 *
	 * Load >=80% langsung MAX.
	 *
	 * Ini jalur agresif untuk workload berat seperti:
	 *
	 *     - particle effect
	 *     - skill effect
	 *     - shader spike
	 *     - rendering spike
	 *     - recording + game
	 *
	 * Tujuannya mengurangi waktu governor untuk bereaksi. Mungkin....
	 * ============================================================
	 */
	if (b >= DFSO_GAMING_MAX_LOAD) {
		*freq = max;
		return 0;
	}

	/*
	 * ============================================================
	 * GAMING MODE 2:
	 *
	 * Load >=60%:
	 *
	 * PERTAHANKAN current frequency.
	 *
	 * Ini mencegah frequency turun ketika load hanya turun
	 * sebentar di antara workload/frame. 
	 * ============================================================
	 */
	if (b >= DFSO_GAMING_HOLD_LOAD) {
		if (stat->current_frequency)
			*freq = stat->current_frequency;
		else
			*freq = max;

		return 0;
	}

	/*
	 * ============================================================
	 * Kalau current frequency belum diketahui:
	 * gunakan MAX.
	 * ============================================================
	 */
	if (stat->current_frequency == 0) {
		*freq = max;
		return 0;
	}

	/*
	 * ============================================================
	 * KALKULASI KAYAK KONTOL
	 * ============================================================
	 *
	 * Form dasar:
	 *
	 *     target = current_frequency * utilization
	 *
	 * Kemudian menggunakan threshold governor.
	 *
	 * DOWN=20 membuat threshold efektif:
	 *
	 *     100 - 20 = 80
	 *
	 * ============================================================
	 */
	a = stat->busy_time;
	a *= stat->current_frequency;

	b = div_u64(a, stat->total_time);

	b *= 100;

	b = div_u64(
		b,
		(dfso_upthreshold - dfso_downdifferential / 2)
	);

	/*
	 * ============================================================
	 * BOOST
	 * ============================================================
	 *
	 * Tambahkan 15% pada hasil kalkulasi.
	 *
	 * Contoh:
	 *
	 *     400 MHz -> 460 MHz
	 *
	 * Tujuannya mengurangi frequency yang terlalu rendah
	 * ketika load sedang.
	 * ============================================================
	 */
	b *= (100 + DFSO_GAMING_BOOST_PERCENT);
	b = div_u64(b, 100);

	*freq = (unsigned long)b;

	/*
	 * ============================================================
	 * JEMBOT TAMBAH AN.
	 * ============================================================
	 */
	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	/*
	 * ============================================================
	 * KONTOL TAMBAH AN
	 * ============================================================
	 */
	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;

	return 0;
}

static int devfreq_simple_ondemand_handler(struct devfreq *devfreq,
				unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_INTERVAL:
		devfreq_interval_update(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_simple_ondemand = {
	.name = "simple_ondemand",
	.get_target_freq = devfreq_simple_ondemand_func,
	.event_handler = devfreq_simple_ondemand_handler,
};

static int __init devfreq_simple_ondemand_init(void)
{
	return devfreq_add_governor(&devfreq_simple_ondemand);
}

subsys_initcall(devfreq_simple_ondemand_init);

static void __exit devfreq_simple_ondemand_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_simple_ondemand);
	if (ret)
		pr_err("%s: failed remove governor %d\n",
		       __func__, ret);

	return;
}

module_exit(devfreq_simple_ondemand_exit);
MODULE_LICENSE("GPL");
