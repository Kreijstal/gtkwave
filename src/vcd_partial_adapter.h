#ifndef VCD_PARTIAL_ADAPTER_H
#define VCD_PARTIAL_ADAPTER_H

#include "gw-time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adapter functions for compatibility with old global interface */
GwTime vcd_partial_main(char *fname);
void kick_partial_vcd(void);
void vcd_partial_mark_and_sweep(int mandclear);
void vcd_partial_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* VCD_PARTIAL_ADAPTER_H */