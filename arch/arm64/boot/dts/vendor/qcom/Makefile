# SPDX-License-Identifier: GPL-2.0

# Base DTB (goes into vendor_boot) and the board overlays (go into dtbo).
# ABL picks the base by qcom,msm-id and the overlay by qcom,msm-id/board-id.
dtb-$(CONFIG_ARCH_QCOM) += lahaina-tundra-base.dtb \
	lahaina-tundra-evb-overlay.dtbo \
	lahaina-tundra-dvt1-overlay.dtbo \
	lahaina-tundra-dvt2-overlay.dtbo

DTC_FLAGS += -@
