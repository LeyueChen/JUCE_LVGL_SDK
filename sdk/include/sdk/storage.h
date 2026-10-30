#ifndef SDK_STORAGE_H
#define SDK_STORAGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { SDK_STORAGE_KV_SET = 1, SDK_STORAGE_KV_GET, SDK_STORAGE_FILE_WRITE, SDK_STORAGE_FILE_READ } sdk_storage_operation_t;
typedef struct { sdk_storage_operation_t operation; char key[64]; uint32_t size; uint8_t data[256]; } sdk_storage_request_t;
typedef struct { int result; uint32_t size; uint8_t data[256]; } sdk_storage_result_t;
/* Called on StorageTask. UI updates must be posted to UiTask. */
typedef void (*sdk_storage_completion_fn)(const sdk_storage_result_t *, void *context);
int sdk_storage_submit(const sdk_storage_request_t *request, sdk_storage_completion_fn completion, void *context);
#ifdef __cplusplus
}
#endif
#endif
