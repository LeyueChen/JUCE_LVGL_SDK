#ifndef SDK_STORAGE_H
#define SDK_STORAGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
	SDK_STORAGE_KV_SET = 1,
	SDK_STORAGE_KV_GET,
	SDK_STORAGE_FILE_WRITE,
	SDK_STORAGE_FILE_READ,
	SDK_STORAGE_KV_DELETE,
	SDK_STORAGE_KV_SIZE,
	SDK_STORAGE_FILE_WRITE_AT,
	SDK_STORAGE_FILE_READ_AT,
	SDK_STORAGE_FILE_SIZE,
	SDK_STORAGE_FILE_REMOVE,
	SDK_STORAGE_FILE_MKDIR,
	SDK_STORAGE_FILE_RENAME
} sdk_storage_operation_t;
enum {
	SDK_STORAGE_IO = -1,
	SDK_STORAGE_INVALID = -2,
	SDK_STORAGE_NOT_FOUND = -3,
	SDK_STORAGE_NO_SPACE = -4,
	SDK_STORAGE_UNAVAILABLE = -5,
	SDK_STORAGE_BUFFER_TOO_SMALL = -6
};
typedef struct {
	sdk_storage_operation_t operation;
	char key[64];
	uint32_t size;
	uint8_t data[256];
	uint32_t offset;
	char destination[64]; /* FILE_RENAME only */
} sdk_storage_request_t;
typedef struct {
	int result;
	uint32_t size;
	uint8_t data[256];
} sdk_storage_result_t;
/* Requests and payloads are copied; submission is nonblocking, CPU0 tasks only.
 * Read/write at offset allow large models/IRs/files in 256-byte chunks.
 * FILE_WRITE truncates, WRITE_AT preserves existing content. For atomic resource
 * replacement: write a temporary file, then FILE_RENAME on the same filesystem.
 * KV_GET rejects a short buffer; SIZE returns object bytes in result.size.
 * Completion data is borrowed until callback returns, on StorageTask; callbacks
 * must not block. UI updates must be posted to UiTask. */
typedef void (*sdk_storage_completion_fn)(const sdk_storage_result_t *,
					  void *context);
int sdk_storage_submit(const sdk_storage_request_t *request,
		       sdk_storage_completion_fn completion, void *context);
int sdk_storage_validate(const sdk_storage_request_t *request);
#ifdef __cplusplus
}
#endif
#endif
