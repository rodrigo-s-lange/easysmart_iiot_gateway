#ifndef GW_PROFILE_H
#define GW_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GW_PROFILE_IIOT_GATEWAY = 0,
    GW_PROFILE_GENERIC_GATEWAY = 1,
    GW_PROFILE_LIGHTING_GATEWAY = 2,
} gw_profile_t;

const char *gw_profile_name(gw_profile_t profile);
int gw_profile_from_name(const char *name, gw_profile_t *out_profile);

#ifdef __cplusplus
}
#endif

#endif
