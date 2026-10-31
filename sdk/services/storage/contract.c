#include "sdk/storage.h"
#include <string.h>
#include <limits.h>
static int valid_path(const char *path)
{
	if (!path[0] || !memchr(path, 0, 64))
		return 0;
	const char *segment = path;
	for (const char *p = path;; ++p) {
		if (*p == '\\' || *p == ':' || (*p && (unsigned char)*p < 32))
			return 0;
		if (*p == '/' || !*p) {
			size_t n = (size_t)(p - segment);
			if ((n == 1 && segment[0] == '.') ||
			    (n == 2 && segment[0] == '.' && segment[1] == '.'))
				return 0;
			segment = p + 1;
		}
		if (!*p)
			break;
	}
	return strcmp(path, "/") != 0;
}
int sdk_storage_validate(const sdk_storage_request_t *r)
{
	if (!r || r->size > sizeof(r->data) || !valid_path(r->key) ||
	    r->operation < SDK_STORAGE_KV_SET ||
	    r->operation > SDK_STORAGE_FILE_RENAME ||
	    r->offset > INT_MAX - sizeof(r->data))
		return SDK_STORAGE_INVALID;
	if (r->operation == SDK_STORAGE_FILE_RENAME &&
	    !valid_path(r->destination))
		return SDK_STORAGE_INVALID;
	if ((r->operation == SDK_STORAGE_KV_SET ||
	     r->operation == SDK_STORAGE_KV_GET ||
	     r->operation == SDK_STORAGE_KV_DELETE ||
	     r->operation == SDK_STORAGE_KV_SIZE) &&
	    strchr(r->key, '/'))
		return SDK_STORAGE_INVALID;
	return 0;
}
