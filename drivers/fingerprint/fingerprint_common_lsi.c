#include "fingerprint_common.h"

#if defined(CONFIG_SENSORS_FINGERPRINT_MODULE)
#if IS_ENABLED(CONFIG_EXYNOS_PM_QOS) || IS_ENABLED(CONFIG_EXYNOS_PM_QOS_MODULE)
#include <soc/samsung/exynos_pm_qos.h>
static struct exynos_pm_qos_request fingerprint_boost_qos;
#endif
#elif defined(CONFIG_SECURE_OS_BOOSTER_API)
#include <mach/secos_booster.h>
#elif defined(CONFIG_TZDEV_BOOST)
#if defined(CONFIG_TEEGRIS_VERSION) && (CONFIG_TEEGRIS_VERSION >= 4)
#include <../drivers/misc/tzdev/extensions/boost.h>
#else
#include <../drivers/misc/tzdev/tz_boost.h>
#endif
#endif


int cpu_speedup_enable(struct boosting_config *boosting)
{
	int retval = 0;

	pr_debug("%s\n", __func__);
/* Module build & TEEGris */
#if defined(CONFIG_SENSORS_FINGERPRINT_MODULE)
#if IS_ENABLED(CONFIG_EXYNOS_PM_QOS) || IS_ENABLED(CONFIG_EXYNOS_PM_QOS_MODULE)
	exynos_pm_qos_add_request(&fingerprint_boost_qos, PM_QOS_CLUSTER1_FREQ_MIN,
			PM_QOS_CLUSTER1_FREQ_MAX_DEFAULT_VALUE);
#endif
/* TEEGris */
#elif defined(CONFIG_TZDEV_BOOST)
	tz_boost_enable();
/* Kinibi */
#elif defined(CONFIG_SECURE_OS_BOOSTER_API)
	retval = secos_booster_start(MAX_PERFORMANCE);
	if (retval)
		pr_err("booster start failed. (%d)\n", retval);

#else
	pr_debug("FP_CPU_SPEEDUP does not supported\n");
#endif
	return retval;
}

int cpu_speedup_disable(struct boosting_config *boosting)
{
	int retval = 0;

	pr_debug("%s\n", __func__);
/* Module build & TEEGris */
#if defined(CONFIG_SENSORS_FINGERPRINT_MODULE)
#if IS_ENABLED(CONFIG_EXYNOS_PM_QOS) || IS_ENABLED(CONFIG_EXYNOS_PM_QOS_MODULE)
	exynos_pm_qos_remove_request(&fingerprint_boost_qos);
#endif
/* TEEGris */
#elif defined(CONFIG_TZDEV_BOOST)
	tz_boost_disable();
/* Kinibi */
#elif defined(CONFIG_SECURE_OS_BOOSTER_API)
	retval = secos_booster_stop();
	if (retval)
		pr_err("booster stop failed. (%d)\n", retval);

#else
	pr_debug("FP_CPU_SPEEDUP does not supported\n");
#endif
	return retval;
}
