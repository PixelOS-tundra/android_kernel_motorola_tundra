# SPDX-License-Identifier: GPL-2.0

# The lahaina/tundra DT is the 5.4 vendor DT (seventeen 053cdcc70b89), built
# against the 5.4 dt-bindings in include/: the drivers here index clocks,
# interconnects etc. by those numbers.
# Base DTBs (go into vendor_boot) and the board overlays (go into dtbo).
# ABL picks the base by qcom,msm-id and the overlay by qcom,msm-id/board-id.
dtb-$(CONFIG_ARCH_QCOM) += lahaina-moto-base-v2.1.dtb \
	lahaina-moto-base.dtb \
	lahaina-tundra-evb-overlay.dtbo \
	lahaina-tundra-dvt1-overlay.dtbo \
	lahaina-tundra-dvt2-overlay.dtbo

DTC_FLAGS += -@
DTC_INCLUDE := $(srctree)/$(src)/include
