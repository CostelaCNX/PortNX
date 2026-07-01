#pragma once

#include <switch/types.h>

#ifdef __cplusplus
extern "C" {
#endif

Result esInitialize(void);
void esExit(void);
Result esImportTicket(const void *tik_buf, size_t tik_size,
                      const void *cert_buf, size_t cert_size);

#ifdef __cplusplus
}
#endif
