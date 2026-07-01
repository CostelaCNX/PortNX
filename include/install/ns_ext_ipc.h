#pragma once

#include <switch/services/ns.h>
#include <switch/services/ncm_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NsApplicationRecordType_Installed       = 0x3,
    NsApplicationRecordType_GamecardMissing = 0x5,
    NsApplicationRecordType_Archived        = 0xB,
} NsApplicationRecordType;

typedef struct {
    NcmContentMetaKey meta_key;
    u64 storage_id;
} ContentStorageRecord;

Result nsextInitialize(void);
void nsextExit(void);
Result nsPushApplicationRecord(u64 application_id,
                                NsApplicationRecordType last_modified_event,
                                ContentStorageRecord *content_records,
                                u32 count);
Result nsDeleteApplicationRecord(u64 application_id);

#ifdef __cplusplus
}
#endif
