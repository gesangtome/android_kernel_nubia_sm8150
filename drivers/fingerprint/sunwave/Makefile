# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)

ccflags-y += -DFEATURE_NUBIA_FINGERPRINT_AUTODETECT

# Just for mtk platform head file, kernel version less than kernle 4.0
ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)

# Just for mtk platform head file, kernel version larger than kernle 4.0
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/teei/V1.0/tz_vfs
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/tkcore/core
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/tkcore/include/

# Sunwave driver file
obj-$(CONFIG_FINGERPRINT_SUNWAVE)          += sunwave_fp.o
sunwave_fp-objs                           := sf_ctl.o sf_hw.o
else
KBUILDDIR := $(ANDROID_PRODUCT_OUT)/obj/KERNEL_OBJ
MODULEPWD := $(shell pwd)

.PHONY: all install clean

all:
	$(MAKE) -C $(KBUILDDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
		M=$(MODULEPWD) CONFIG_MTK_FINGERPRINT_SUNWAVECORP=m modules

install:
	adb remount
	adb push sunwave_fp.ko /data/
	adb shell sync

clean:
	rm -rf *.ko *.o *.mod.c .*.cmd .*tmp_versions *.symvers *.order 

endif # KERNELRELEASE
