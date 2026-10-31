### AnyKernel3 Ramdisk Mod Script
## osm0sis @ xda-developers
## tundra (Motorola Edge 30 Fusion): kernel-only flash into boot of the active slot.
## Boot header v3: the DTB lives in vendor_boot and the ramdisk is left untouched.

### AnyKernel setup
# global properties
properties() { '
kernel.string=torracat kernel for tundra (5.4)
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=tundra
device.name2=
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; } # end properties


### AnyKernel install
# boot shell variables
BLOCK=boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

# import functions/variables and setup patching - see for reference (DO NOT REMOVE)
. tools/ak3-core.sh;

# boot install: replace only the kernel, keep the stock ramdisk as-is
split_boot;

flash_boot;
## end boot install
