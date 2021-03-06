/* drivers/gpu/mali400/mali/platform/exynos3250/exynos3_pmm.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali400 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file exynos3_pmm.c
 * Platform specific Mali driver functions for the exynos 3250 based platforms
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "exynos3_pmm.h"
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#include <linux/types.h>
#include <mach/cpufreq.h>
#include <mach/regs-clock.h>
#include <mach/asv-exynos.h>
#ifdef CONFIG_CPU_FREQ
#define EXYNOS4_ASV_ENABLED
#endif
#endif

#ifdef CONFIG_MALI_DVFS
static struct mali_gpu_clk_item gpu_clocks[] = {
	{
		.clock = 80,
		.vol = 50000,
	}, {
		.clock = 160,
		.vol = 100000,
	}, {
		.clock = 266,
		.vol = 135000,
	},
};

struct mali_gpu_clock gpu_clock = {
	.item = gpu_clocks,
	.num_of_steps = ARRAY_SIZE(gpu_clocks),
};

_mali_osk_mutex_t *mali_dvfs_lock;
static int bus_clk_step;
static bool clk_boostup;
static struct delayed_work qos_work;
#endif

#define EXTXTALCLK_NAME  "ext_xtal"
#define VPLLSRCCLK_NAME  "mout_vpllsrc"
#define FOUTVPLLCLK_NAME "fout_vpll"
#define SCLVPLLCLK_NAME  "mout_vpll"
#define GPUMOUT1CLK_NAME "mout_g3d1"
#define MPLLCLK_NAME     "mout_mpll"
#define GPUMOUT0CLK_NAME "mout_g3d0"
#define GPUMOUTCLK_NAME "mout_g3d"
#define GPUCLK_NAME      "sclk_g3d"
#define GPU_NAME      "g3d"
#define CLK_DIV_STAT_G3D 0x1003C62C
#define CLK_DESC         "clk-divider-status"

static struct clk *ext_xtal_clock;
static struct clk *vpll_src_clock;
static struct clk *mpll_clock;
static struct clk *mali_mout0_clock;
static struct clk *mali_mout_clock;
static struct clk *fout_vpll_clock;
static struct clk *sclk_vpll_clock;
static struct clk *mali_parent_clock;
static struct clk *mali_clock;
static struct clk *g3d_clock;

/* PegaW1 */
int mali_gpu_clk = 80;
int mali_gpu_vol = 100000;
static unsigned int GPU_MHZ = 1000000;
static unsigned int const GPU_ASV_VOLT = 1000;
static int nPowermode;
static atomic_t clk_active;

#ifdef CONFIG_REGULATOR
struct regulator *g3d_regulator;
#endif

#ifdef CONFIG_MALI_DVFS
#ifdef CONFIG_REGULATOR
extern int g3d_regulator_set_voltage(int int_target_freq);
extern void int_g3d_regulator_init(struct regulator *regulator);
static void mali_regulator_set_voltage(int int_target_freq)
{
	int g3d_voltage;
	_mali_osk_mutex_wait(mali_dvfs_lock);
	if (IS_ERR_OR_NULL(g3d_regulator)) {
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		_mali_osk_mutex_signal(mali_dvfs_lock);
		return;
	}
	g3d_voltage = g3d_regulator_set_voltage(int_target_freq);
	MALI_DEBUG_PRINT(1, ("= regulator_set_voltage: %d\n", g3d_voltage));

	mali_gpu_vol = regulator_get_voltage(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("Mali voltage: %d\n", mali_gpu_vol));
	_mali_osk_mutex_signal(mali_dvfs_lock);
}
#endif
#endif

static mali_bool mali_clk_get(struct platform_device *pdev)
{
	if (ext_xtal_clock == NULL)	{
		ext_xtal_clock = clk_get(&pdev->dev, EXTXTALCLK_NAME);
		if (IS_ERR(ext_xtal_clock)) {
			MALI_PRINT_ERROR(("failed to get source ext_xtal_clock\n"));
			return MALI_FALSE;
		}
	}

	if (vpll_src_clock == NULL)	{
		vpll_src_clock = clk_get(&pdev->dev, VPLLSRCCLK_NAME);
		if (IS_ERR(vpll_src_clock)) {
			MALI_PRINT_ERROR(("failed to get source vpll_src_clock\n"));
			return MALI_FALSE;
		}
	}

	if (fout_vpll_clock == NULL) {
		fout_vpll_clock = clk_get(&pdev->dev, FOUTVPLLCLK_NAME);
		if (IS_ERR(fout_vpll_clock)) {
			MALI_PRINT_ERROR(("failed to get source fout_vpll_clock\n"));
			return MALI_FALSE;
		}
	}

	if (mpll_clock == NULL) {
		mpll_clock = clk_get(&pdev->dev, MPLLCLK_NAME);

		if (IS_ERR(mpll_clock)) {
			MALI_PRINT_ERROR(("failed to get source mpll clock\n"));
			return MALI_FALSE;
		}
	}

	if (mali_mout0_clock == NULL) {
		mali_mout0_clock = clk_get(&pdev->dev, GPUMOUT0CLK_NAME);

		if (IS_ERR(mali_mout0_clock)) {
			MALI_PRINT_ERROR(("failed to get source mali mout0 clock\n"));
			return MALI_FALSE;
		}
	}

	if (mali_mout_clock == NULL) {
		mali_mout_clock = clk_get(&pdev->dev, GPUMOUTCLK_NAME);

		if (IS_ERR(mali_mout_clock)) {
			MALI_PRINT_ERROR(("failed to get source mali mout clock\n"));
			return MALI_FALSE;
		}
	}

	if (sclk_vpll_clock == NULL) {
		sclk_vpll_clock = clk_get(&pdev->dev, SCLVPLLCLK_NAME);
		if (IS_ERR(sclk_vpll_clock)) {
			MALI_PRINT_ERROR(("failed to get source sclk_vpll_clock\n"));
			return MALI_FALSE;
		}
	}

	if (mali_parent_clock == NULL) {
		mali_parent_clock = clk_get(&pdev->dev, GPUMOUT1CLK_NAME);

		if (IS_ERR(mali_parent_clock)) {
			MALI_PRINT_ERROR(("ailed to get source mali parent clock\n"));
			return MALI_FALSE;
		}
	}

	if (mali_clock == NULL) {
		mali_clock = clk_get(&pdev->dev, GPUCLK_NAME);

		if (IS_ERR(mali_clock)) {
			MALI_PRINT_ERROR(("failed to get source mali clock\n"));
			return MALI_FALSE;
		}
	}
	if (g3d_clock == NULL) {
		g3d_clock = clk_get(&pdev->dev, GPU_NAME);

		if (IS_ERR(g3d_clock)) {
			MALI_PRINT_ERROR(("failed to get mali g3d clock\n"));
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}

static void mali_clk_put(mali_bool binc_mali_clock)
{
	if (mali_parent_clock)
		clk_put(mali_parent_clock);

	if (sclk_vpll_clock)
		clk_put(sclk_vpll_clock);

	if (binc_mali_clock && fout_vpll_clock)
		clk_put(fout_vpll_clock);

	if (mpll_clock)
		clk_put(mpll_clock);

	if (mali_mout0_clock)
		clk_put(mali_mout0_clock);

	if (mali_mout_clock)
		clk_put(mali_mout_clock);

	if (vpll_src_clock)
		clk_put(vpll_src_clock);

	if (ext_xtal_clock)
		clk_put(ext_xtal_clock);
}

static void mali_dvfs_clk_set_rate(unsigned int clk, unsigned int mhz)
{
	int err;
	unsigned long rate = (unsigned long)clk * (unsigned long)mhz;
	unsigned long cur_rate = 0, mali_rate;

	_mali_osk_mutex_wait(mali_dvfs_lock);
	MALI_DEBUG_PRINT(3, ("Mali platform: Setting frequency to %d mhz\n",
				clk));

	cur_rate = clk_get_rate(mali_clock);

	if (cur_rate == 0) {
		_mali_osk_mutex_signal(mali_dvfs_lock);
		MALI_PRINT_ERROR(("clk_get_rate[mali_clock] is 0 - return\n"));
		return;
	}

	err = clk_set_rate(fout_vpll_clock, (unsigned int)clk * GPU_MHZ);
	if (err) {
		_mali_osk_mutex_signal(mali_dvfs_lock);
		MALI_PRINT_ERROR(("Failed to set fout_vpll clock:\n"));
		return;
	}

	mali_rate = clk_get_rate(mali_clock);
	if (mali_rate != rate) {
		_mali_osk_mutex_signal(mali_dvfs_lock);
		MALI_PRINT_ERROR(("Failed to set mali rate to %lu\n", rate));
		return;
	}
	MALI_DEBUG_PRINT(1, ("Mali frequency %d\n", rate / mhz));
	GPU_MHZ = mhz;

	mali_gpu_clk = (int)(rate / mhz);
	mali_clk_put(MALI_FALSE);
	_mali_osk_mutex_signal(mali_dvfs_lock);
}

static void mali_clk_set_rate(struct platform_device *pdev, unsigned int clk,
		unsigned int mhz)
{
	int err;
	unsigned long rate = (unsigned long)clk * (unsigned long)mhz;

	_mali_osk_mutex_wait(mali_dvfs_lock);
	MALI_DEBUG_PRINT(3, ("Mali platform: Setting frequency to %d mhz\n",
				clk));

	if (mali_clk_get(pdev) == MALI_FALSE) {
		_mali_osk_mutex_signal(mali_dvfs_lock);
		return;
	}

	err = clk_set_rate(mali_clock, rate);
	if (err)
		MALI_PRINT_ERROR(("Failed to set Mali clock: %d\n", err));

	rate = clk_get_rate(mali_clock);
	GPU_MHZ = mhz;

	mali_gpu_clk = rate / mhz;
	MALI_DEBUG_PRINT(1, ("Mali frequency %dMhz\n", rate / mhz));

	mali_clk_put(MALI_FALSE);

	_mali_osk_mutex_signal(mali_dvfs_lock);
}

static mali_bool configure_mali_clocks(struct platform_device *pdev)
{
	int err = 0;
	mali_bool ret = MALI_TRUE;

	if (!mali_clk_get(pdev)) {
		MALI_PRINT_ERROR(("Failed to get Mali clock\n"));
		ret = MALI_FALSE;
		goto err_clk;
	}

	err = clk_set_rate(fout_vpll_clock, (unsigned int)mali_gpu_clk *
			GPU_MHZ);
	if (err)
		MALI_PRINT_ERROR(("Failed to set fout_vpll clock:\n"));

	err = clk_set_parent(vpll_src_clock, ext_xtal_clock);
	if (err)
		MALI_PRINT_ERROR(("vpll_src_clock set parent to ext_xtal_clock failed\n"));

	err = clk_set_parent(sclk_vpll_clock, fout_vpll_clock);
	if (err)
		MALI_PRINT_ERROR(("sclk_vpll_clock set parent to fout_vpll_clock failed\n"));

	err = clk_set_parent(mali_parent_clock, sclk_vpll_clock);
	if (err)
		MALI_PRINT_ERROR(("mali_parent_clock set parent to sclk_vpll_clock failed\n"));

	err = clk_set_parent(mali_mout_clock, mali_parent_clock);
	if (err)
		MALI_PRINT_ERROR(("mali_clock set parent to mali_parent_clock failed\n"));
	if (!atomic_read(&clk_active)) {
		if ((clk_prepare_enable(mali_clock)  < 0)
				|| (clk_prepare_enable(g3d_clock)  < 0)) {
			MALI_PRINT_ERROR(("Failed to enable clock\n"));
			goto err_clk;
		}
		atomic_set(&clk_active, 1);
	}

	mali_clk_set_rate(pdev, (unsigned int)mali_gpu_clk, GPU_MHZ);
	mali_clk_put(MALI_FALSE);

	return MALI_TRUE;
err_clk:
	mali_clk_put(MALI_TRUE);
	return ret;
}

static mali_bool init_mali_clock(struct platform_device *pdev)
{
	mali_bool ret = MALI_TRUE;
	nPowermode = MALI_POWER_MODE_DEEP_SLEEP;

	if (mali_clock != NULL)
		return ret; /* already initialized */

	mali_dvfs_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED, 0);

	if (mali_dvfs_lock == NULL)
		return _MALI_OSK_ERR_FAULT;

	ret = configure_mali_clocks(pdev);
	/* The VPLL clock should not be clock-gated for it caused
	 * hangs in S2R */
	clk_prepare_enable(fout_vpll_clock);

	if (ret != MALI_TRUE)
		goto err_mali_clock;

#ifdef CONFIG_MALI_DVFS
#ifdef CONFIG_REGULATOR
	if (g3d_regulator == NULL) {
		g3d_regulator = regulator_get(NULL, "vdd_int");
		if (IS_ERR(g3d_regulator)) {
			MALI_PRINT(("MALI Error : failed to get vdd_int for g3d\n"));
			ret = MALI_FALSE;
			regulator_put(g3d_regulator);
			g3d_regulator = NULL;
			return MALI_FALSE;
		}

		int_g3d_regulator_init(g3d_regulator);

		mali_gpu_vol = gpu_clocks[bus_clk_step].vol;
#ifdef EXYNOS4_ASV_ENABLED
		mali_gpu_vol = get_match_volt(ID_G3D,
				gpu_clocks[bus_clk_step].clock * GPU_ASV_VOLT);
#endif
	}
#endif
#endif
	mali_clk_put(MALI_FALSE);

	return MALI_TRUE;

err_mali_clock:
	return ret;
}

static mali_bool deinit_mali_clock(void)
{
	if (mali_clock == 0 || g3d_clock == 0)
		return MALI_TRUE;

	mali_clk_put(MALI_TRUE);
	return MALI_TRUE;
}

static _mali_osk_errcode_t enable_mali_clocks(struct device *dev)
{
	int err = 0;
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);

	if (!atomic_read(&clk_active)) {
		err = clk_prepare_enable(mali_clock);
		err = clk_prepare_enable(g3d_clock);
		atomic_set(&clk_active, 1);
	}

	MALI_DEBUG_PRINT(3, ("enable_mali_clocks mali_clock %p error %d\n",
				mali_clock, err));
	/*
	 * Right now we are configuring clock each time, during runtime
	 * s2r and s2r as it has been observed mali failed to enter into
	 * deep sleep state during s2r.
	 * If this gets fixed we *MUST* remove LIGHT_SLEEP condition from below
	 */
	if (nPowermode == MALI_POWER_MODE_DEEP_SLEEP ||
			nPowermode == MALI_POWER_MODE_LIGHT_SLEEP)
		configure_mali_clocks(pdev);


	/* set clock rate */
#ifdef CONFIG_MALI_DVFS
	if (bus_clk_step)
		mali_dvfs_clk_set_rate(gpu_clocks[bus_clk_step].clock,
				GPU_MHZ);
	else {
#ifdef CONFIG_REGULATOR
		mali_regulator_set_voltage(gpu_clocks[bus_clk_step].vol);
#endif
		mali_dvfs_clk_set_rate(gpu_clocks[bus_clk_step].clock,
				GPU_MHZ);
	}
#endif

	MALI_SUCCESS;
}

static _mali_osk_errcode_t disable_mali_clocks(void)
{
	if (atomic_read(&clk_active)) {
		clk_disable_unprepare(mali_clock);
		clk_put(mali_clock);
		clk_disable_unprepare(g3d_clock);
		clk_put(g3d_clock);
		deinit_mali_clock();
		MALI_DEBUG_PRINT(3, ("disable_mali_clocks mali_clock %p g3d %p\n",
					mali_clock, g3d_clock));
		atomic_set(&clk_active, 0);
	}

	MALI_SUCCESS;
}

#ifdef CONFIG_MALI_DVFS
static void
exynos3_gpu_qos_work_handler(struct work_struct *work)
{
	if (clk_boostup) {
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(gpu_clocks[bus_clk_step].vol);
#endif
		/*change the clock*/
		mali_dvfs_clk_set_rate(gpu_clocks[bus_clk_step].clock,
				1000000);
	} else {
		/*change the clock*/
		mali_dvfs_clk_set_rate(gpu_clocks[bus_clk_step].clock,
				1000000);
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(gpu_clocks[bus_clk_step].vol);
#endif
	}

	mali_clk_put(MALI_FALSE);
}

void exynos3_get_clock_info(struct mali_gpu_clock **data)
{
	*data = &gpu_clock;
}

int exynos3_get_freq(void)
{
	return bus_clk_step;
}

int exynos3_set_freq(int step)
{
	if (step != bus_clk_step) {
		clk_boostup = (step > bus_clk_step) ? true : false;
		bus_clk_step = step;

		INIT_DELAYED_WORK(&qos_work, exynos3_gpu_qos_work_handler);
		queue_delayed_work(system_wq, &qos_work, 0);
	}

	return 0;
}
#endif

_mali_osk_errcode_t mali_platform_init(struct platform_device *pdev)
{
	atomic_set(&clk_active, 0);

	MALI_CHECK(init_mali_clock(pdev), _MALI_OSK_ERR_FAULT);
	mali_platform_power_mode_change(&pdev->dev, MALI_POWER_MODE_ON);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(struct platform_device *pdev)
{
	mali_platform_power_mode_change(&pdev->dev, MALI_POWER_MODE_DEEP_SLEEP);
	deinit_mali_clock();
	clk_disable_unprepare(fout_vpll_clock);

#ifdef CONFIG_MALI_DVFS
#ifdef CONFIG_REGULATOR
	if (g3d_regulator) {
		regulator_put(g3d_regulator);
		g3d_regulator = NULL;
	}
#endif
#endif
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(struct device *dev, mali_power_mode power_mode)
{
	switch (power_mode) {
	case MALI_POWER_MODE_ON:
		MALI_DEBUG_PRINT(3, ("Got MALI_POWER_MODE_ON event, %s\n",
					nPowermode ? "powering on" :
						     "already on"));
		if (nPowermode == MALI_POWER_MODE_LIGHT_SLEEP ||
				nPowermode == MALI_POWER_MODE_DEEP_SLEEP) {
			MALI_DEBUG_PRINT(4, ("enable clock\n"));
			enable_mali_clocks(dev);
			nPowermode = power_mode;
		}
		break;
	case MALI_POWER_MODE_DEEP_SLEEP:
	case MALI_POWER_MODE_LIGHT_SLEEP:
		MALI_DEBUG_PRINT(3, ("Got %s event, %s\n",
				power_mode == MALI_POWER_MODE_LIGHT_SLEEP ?
					"MALI_POWER_MODE_LIGHT_SLEEP" :
					"MALI_POWER_MODE_DEEP_SLEEP",
					nPowermode ? "already off" :
						     "powering off"));
		if (nPowermode == MALI_POWER_MODE_ON)	{
			disable_mali_clocks();
			nPowermode = power_mode;
		}
		break;
	}
	MALI_SUCCESS;
}
