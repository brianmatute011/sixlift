#include "sixlift/config.h"

const char *const SIXLIFT_DNS64[] = {
    "2a00:1098:2b::1",       /* nat64.net */
    "2a00:1098:2c::1",       /* nat64.net */
    "2a01:4f9:c010:3f02::1", /* nat64.net */
    "2001:67c:2b0::4",       /* Trex      */
    "2001:67c:2b0::6",       /* Trex      */
};

const int SIXLIFT_DNS64_COUNT =
    (int)(sizeof(SIXLIFT_DNS64) / sizeof(SIXLIFT_DNS64[0]));
