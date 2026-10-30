#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *sdk_backend_open(const char *directory);
void sdk_backend_close(void *backend);
int sdk_backend_file_write(void *backend, const char *path, const void *data,
			   size_t size);
int sdk_backend_file_read(void *backend, const char *path, void *data,
			  size_t capacity);
int sdk_backend_kv_set(void *backend, const char *key, const void *data,
		       size_t size);
int sdk_backend_kv_get(void *backend, const char *key, void *data,
		       size_t capacity);
#ifdef __cplusplus
}
#endif
