obj-m := gc573_native.o
gc573_native-y := gc573_pci.o gc573_i2c.o gc573_block.o gc573_init.o gc573_calibrate.o gc573_clock.o gc573_timing.o gc573_edid.o
gc573_native-y += gc573_ddc.o gc573_input.o gc573_splitter.o
gc573_native-y += gc573_splitter_timing.o
gc573_native-y += gc573_splitter_map.o
gc573_native-y += gc573_splitter_calibrate.o
gc573_native-y += gc573_splitter_setup.o
gc573_native-y += gc573_splitter_finish.o
gc573_native-y += gc573_splitter_tx.o
gc573_native-y += gc573_splitter_ports.o
gc573_native-y += gc573_splitter_link.o
gc573_native-y += gc573_splitter_hpd.o
gc573_native-y += gc573_splitter_edid.o
gc573_native-y += gc573_splitter_video.o
gc573_native-y += gc573_receiver_video.o
gc573_native-y += gc573_capture.o
gc573_native-y += gc573_led.o

gc573_native-y += gc573_receiver_audio.o
gc573_native-y += gc573_audio.o
gc573_native-y += gc573_hdmi.o

# VB2 moved queue wait locking into the core in newer kernels.
ifneq ($(shell grep -F 'void (*wait_prepare)' $(srctree)/include/media/videobuf2-core.h),)
ccflags-y += -DGC573_VB2_NEEDS_WAIT_OPS
endif

# Detect the sysfs const API in the target headers (including distro backports).
ifneq ($(shell grep -E 'ssize_t.*read.*const struct bin_attribute' $(srctree)/include/linux/sysfs.h),)
ccflags-y += -DGC573_SYSFS_READ_CONST=const
else
ccflags-y += -DGC573_SYSFS_READ_CONST=
endif
ifneq ($(shell grep -E 'const struct bin_attribute.*bin_attrs;' $(srctree)/include/linux/sysfs.h),)
ccflags-y += -DGC573_SYSFS_BIN_CONST=const
else
ccflags-y += -DGC573_SYSFS_BIN_CONST=
endif

gc573_native-y += gc573_sink.o
gc573_native-y += gc573_passthrough_edid.o
gc573_native-y += gc573_passthrough.o
