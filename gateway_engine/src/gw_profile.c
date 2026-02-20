#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <gateway_engine/gw_profile.h>

static const char *const GW_PROFILE_NAMES[] = {
    "iiot_gateway",
    "generic_gateway",
    "lighting_gateway",
};

const char *gw_profile_name(gw_profile_t profile)
{
    if ((int)profile < 0 || profile > GW_PROFILE_LIGHTING_GATEWAY) {
        return "unknown";
    }

    return GW_PROFILE_NAMES[(int)profile];
}

int gw_profile_from_name(const char *name, gw_profile_t *out_profile)
{
    int i;

    if (name == NULL || out_profile == NULL) {
        return -EINVAL;
    }

    for (i = 0; i <= (int)GW_PROFILE_LIGHTING_GATEWAY; ++i) {
        if (strcmp(name, GW_PROFILE_NAMES[i]) == 0) {
            *out_profile = (gw_profile_t)i;
            return 0;
        }
    }

    return -ENOENT;
}
