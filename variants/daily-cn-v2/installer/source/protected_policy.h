#include "install_policy.h"
struct mer_static_slice {const char *name;uint32_t start,end;const char *sha;};
static const struct mer_static_slice static_slices[]={
 {"boot",0,0x40000,MER_BOOT_SHA},
 {"device-info",0xd30000,0xd40000,MER_DEVICE_INFO_SHA},
 {"firmware-info",0xd40000,0xd50000,MER_FIRMWARE_INFO_SHA},
 {"tddp",0xd50000,0xd60000,MER_TDDP_SHA},
 {"ART",0xff0000,0x1000000,MER_ART_SHA},
};
#define MER_DESIRED_SHA MER_IMAGE_SHA
