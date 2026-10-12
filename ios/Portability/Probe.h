#ifndef SSBU_IOS_PORTABILITY_PROBE_H
#define SSBU_IOS_PORTABILITY_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a bitmask; zero means all checks passed. */
int ssbu_portability_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
