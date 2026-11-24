load("//kernel_device_modules-6.18:kernel/kleaf/gki_modules_6.18.bzl",
     "GKI_MODULES", "KUNIT_MODULES")

COMMON_GKI_MODULES_LIST = GKI_MODULES + KUNIT_MODULES

mgk_module_outs = COMMON_GKI_MODULES_LIST

_MGK_MODULE_EXT_OUTS = [
    "drivers/firmware/arm_ffa/ffa-core.ko",
    "drivers/firmware/arm_ffa/ffa-module.ko",
    "drivers/gpu/drm/display/drm_display_helper.ko",
    "drivers/gpu/drm/drm_dma_helper.ko",
    "drivers/hwtracing/coresight/coresight.ko",
    "drivers/hwtracing/coresight/coresight-dummy.ko",
    "drivers/hwtracing/coresight/coresight-etm4x.ko",
    "drivers/hwtracing/coresight/coresight-funnel.ko",
    "drivers/hwtracing/coresight/coresight-replicator.ko",
    "drivers/hwtracing/coresight/coresight-tmc.ko",
    "drivers/iio/buffer/industrialio-triggered-buffer.ko",
    "drivers/iio/buffer/kfifo_buf.ko",
    "drivers/leds/leds-pwm.ko",
    "drivers/media/v4l2-core/v4l2-flash-led-class.ko",
    "drivers/net/ethernet/microchip/lan743x.ko",
    "drivers/perf/arm_dsu_pmu.ko",
    "drivers/power/reset/reboot-mode.ko",
    "drivers/power/reset/syscon-reboot-mode.ko",
    "drivers/tee/tee.ko",
    "drivers/thermal/thermal-generic-adc.ko",
    "drivers/usb/phy/phy-generic.ko",
    "net/wireless/cfg80211.ko",
    "net/mac80211/mac80211.ko",
    "drivers/soc/mediatek/mtk-socinfo.ko",
    "drivers/mmc/core/pwrseq_simple.ko",
]

mgk_module_ext_outs = [
    m for m in _MGK_MODULE_EXT_OUTS if m not in COMMON_GKI_MODULES_LIST
]

# 6.18 get_gki_modules_list() returns a select(), so membership tests are not
# possible at load time. pstore_blk was removed from gki_defconfig 6.18 and
# pstore_zone is not in the GKI module list.
_PSTORE_EXTRA_OUTS = [
    "fs/pstore/pstore_zone.ko",
]

mgk_module_eng_outs = mgk_module_ext_outs + _PSTORE_EXTRA_OUTS

mgk_module_userdebug_outs = mgk_module_ext_outs + _PSTORE_EXTRA_OUTS

mgk_module_user_outs = mgk_module_ext_outs + [
]
