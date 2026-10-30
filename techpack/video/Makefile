# SPDX-License-Identifier: GPL-2.0-only

# lahaina uses the kona video configuration, as on 5.4
include $(srctree)/techpack/video/config/konavid.conf
LINUXINCLUDE    += -include $(srctree)/techpack/video/config/konavidconf.h

LINUXINCLUDE    += -I$(srctree)/techpack/video/include \
                   -I$(srctree)/techpack/video/include/uapi

USERINCLUDE     += -I$(srctree)/techpack/video/include/uapi

obj-y +=msm/
