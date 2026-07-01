#include "install/ns_ext_ipc.h"

#include <switch.h>

static Service g_nsAppManSrv;

Result nsextInitialize(void) {
    Result rc = nsInitialize();
    if(R_SUCCEEDED(rc)) {
        rc = nsGetApplicationManagerInterface(&g_nsAppManSrv);
    }
    return rc;
}

void nsextExit(void) {
    serviceClose(&g_nsAppManSrv);
    nsExit();
}

Result nsPushApplicationRecord(u64 application_id,
                                NsApplicationRecordType last_modified_event,
                                ContentStorageRecord *content_records,
                                u32 count) {
    struct {
        u8  last_modified_event;
        u8  padding[7];
        u64 application_id;
    } in = { (u8)last_modified_event, {0}, application_id };

    return serviceDispatchIn(&g_nsAppManSrv, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { content_records, count * sizeof(*content_records) } },
    );
}

Result nsDeleteApplicationRecord(u64 application_id) {
    return serviceDispatchIn(&g_nsAppManSrv, 27, application_id);
}
