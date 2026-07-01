#include "install/es_ipc.h"

#include <string.h>
#include <switch.h>

static Service g_esSrv;

Result esInitialize(void) {
    return smGetService(&g_esSrv, "es");
}

void esExit(void) {
    serviceClose(&g_esSrv);
}

Result esImportTicket(const void *tik_buf, size_t tik_size,
                      const void *cert_buf, size_t cert_size) {
    return serviceDispatch(&g_esSrv, 1,
        .buffer_attrs = {
            SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
            SfBufferAttr_HipcMapAlias | SfBufferAttr_In,
        },
        .buffers = {
            { tik_buf,  tik_size },
            { cert_buf, cert_size },
        },
    );
}
