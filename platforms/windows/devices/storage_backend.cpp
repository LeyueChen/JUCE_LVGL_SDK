#include "storage_backend.h"
#include "sdk_config.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <array>
#ifdef CONFIG_SDK_LITTLEFS
#include <lfs.h>
#endif
#ifdef CONFIG_SDK_FLASHDB
#include <flashdb.h>
#endif
namespace
{
struct Backend {
	std::string directory;
#ifdef CONFIG_SDK_LITTLEFS
	FILE *flash = nullptr;
	lfs_t fs{};
	lfs_config cfg{};
	bool mounted = false;
#endif
#ifdef CONFIG_SDK_FLASHDB
	fdb_kvdb database{};
	bool kv_ready = false;
	std::string kv_directory;
#endif
};
#ifdef CONFIG_SDK_LITTLEFS
constexpr long block_size = 4096, block_count = CONFIG_SDK_LITTLEFS_BLOCK_COUNT;
bool range_ok(lfs_block_t block, lfs_off_t offset, lfs_size_t size)
{
	return block < block_count && offset <= block_size &&
	       size <= block_size - offset;
}
int read_block(const lfs_config *cfg, lfs_block_t block, lfs_off_t offset,
	       void *data, lfs_size_t size)
{
	auto *b = static_cast<Backend *>(cfg->context);
	if (!range_ok(block, offset, size) ||
	    std::fseek(b->flash, block * block_size + offset, SEEK_SET))
		return LFS_ERR_IO;
	return std::fread(data, 1, size, b->flash) == size ? 0 : LFS_ERR_IO;
}
int program_block(const lfs_config *cfg, lfs_block_t block, lfs_off_t offset,
		  const void *data, lfs_size_t size)
{
	auto *b = static_cast<Backend *>(cfg->context);
	std::array<unsigned char, 512> before{};
	auto *bytes = static_cast<const unsigned char *>(data);
	if (!range_ok(block, offset, size))
		return LFS_ERR_IO;
	for (lfs_size_t done = 0; done < size;) {
		const auto count = (size - done) < before.size() ?
					   size - done :
					   before.size();
		if (read_block(cfg, block, offset + done, before.data(),
			       (lfs_size_t)count))
			return LFS_ERR_IO;
		for (size_t i = 0; i < count; ++i)
			if ((before[i] & bytes[done + i]) != bytes[done + i])
				return LFS_ERR_CORRUPT;
		done += (lfs_size_t)count;
	}
	if (std::fseek(b->flash, block * block_size + offset, SEEK_SET))
		return LFS_ERR_IO;
	return std::fwrite(data, 1, size, b->flash) == size ? 0 : LFS_ERR_IO;
}
int erase_block(const lfs_config *cfg, lfs_block_t block)
{
	auto *b = static_cast<Backend *>(cfg->context);
	std::array<unsigned char, block_size> empty;
	empty.fill(0xff);
	if (block >= block_count ||
	    std::fseek(b->flash, block * block_size, SEEK_SET))
		return LFS_ERR_IO;
	return std::fwrite(empty.data(), 1, empty.size(), b->flash) ==
			       empty.size() ?
		       0 :
		       LFS_ERR_IO;
}
int sync_blocks(const lfs_config *cfg)
{
	return std::fflush(static_cast<Backend *>(cfg->context)->flash) ?
		       LFS_ERR_IO :
		       0;
}
#endif
}
extern "C" void sdk_backend_close(void *opaque)
{
	auto *b = static_cast<Backend *>(opaque);
	if (!b)
		return;
#ifdef CONFIG_SDK_FLASHDB
	if (b->kv_ready)
		fdb_kvdb_deinit(&b->database);
#endif
#ifdef CONFIG_SDK_LITTLEFS
	if (b->mounted)
		lfs_unmount(&b->fs);
	if (b->flash)
		std::fclose(b->flash);
#endif
	delete b;
}
extern "C" void *sdk_backend_open(const char *directory)
{
	auto *b = new Backend;
	b->directory = directory;
	std::error_code error;
	std::filesystem::create_directories(b->directory, error);
	if (error) {
		sdk_backend_close(b);
		return nullptr;
	}
#ifdef CONFIG_SDK_LITTLEFS
	const auto image = std::filesystem::path(directory) / "littlefs.img";
	const bool created = !std::filesystem::exists(image);
	b->flash = std::fopen(image.string().c_str(), created ? "w+b" : "r+b");
	if (!b->flash) {
		sdk_backend_close(b);
		return nullptr;
	}
	b->cfg.context = b;
	b->cfg.read = read_block;
	b->cfg.prog = program_block;
	b->cfg.erase = erase_block;
	b->cfg.sync = sync_blocks;
	b->cfg.read_size = 16;
	b->cfg.prog_size = 16;
	b->cfg.block_size = block_size;
	b->cfg.block_count = block_count;
	b->cfg.block_cycles = 500;
	b->cfg.cache_size = 256;
	b->cfg.lookahead_size = 16;
	if (created) {
		for (lfs_block_t block = 0; block < block_count; ++block)
			if (erase_block(&b->cfg, block)) {
				sdk_backend_close(b);
				return nullptr;
			}
		if (lfs_format(&b->fs, &b->cfg)) {
			sdk_backend_close(b);
			return nullptr;
		}
	} else {
		std::fseek(b->flash, 0, SEEK_END);
		if (std::ftell(b->flash) != block_size * block_count) {
			sdk_backend_close(b);
			return nullptr;
		}
	}
	// Existing corrupt images are reported; never auto-format user data.
	if (lfs_mount(&b->fs, &b->cfg)) {
		sdk_backend_close(b);
		return nullptr;
	}
	b->mounted = true;
#endif
#ifdef CONFIG_SDK_FLASHDB
	b->kv_directory =
		(std::filesystem::path(directory) / "flashdb").string();
	std::filesystem::create_directories(b->kv_directory, error);
	if (error) {
		sdk_backend_close(b);
		return nullptr;
	}
	bool file_mode = true;
	uint32_t sector = 4096, maximum = sector * CONFIG_SDK_FLASHDB_SECTORS;
	fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_SEC_SIZE, &sector);
	fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_MAX_SIZE, &maximum);
	fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_FILE_MODE, &file_mode);
	if (fdb_kvdb_init(&b->database, "settings", b->kv_directory.c_str(),
			  nullptr, nullptr) != FDB_NO_ERR) {
		sdk_backend_close(b);
		return nullptr;
	}
	b->kv_ready = true;
#endif
	return b;
}
extern "C" int sdk_backend_file_write(void *opaque, const char *path,
				      const void *data, size_t size)
{
#ifdef CONFIG_SDK_LITTLEFS
	auto *b = static_cast<Backend *>(opaque);
	lfs_file_t file{};
	if (!b || !b->mounted ||
	    lfs_file_open(&b->fs, &file, path,
			  LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC))
		return -1;
	auto result = lfs_file_write(&b->fs, &file, data, (lfs_size_t)size);
	int close_result = lfs_file_close(&b->fs, &file);
	return result == (lfs_ssize_t)size && !close_result ? 0 : -1;
#else
	(void)opaque;
	(void)path;
	(void)data;
	(void)size;
	return -1;
#endif
}
extern "C" int sdk_backend_file_read(void *opaque, const char *path, void *data,
				     size_t capacity)
{
#ifdef CONFIG_SDK_LITTLEFS
	auto *b = static_cast<Backend *>(opaque);
	lfs_file_t file{};
	if (!b || !b->mounted ||
	    lfs_file_open(&b->fs, &file, path, LFS_O_RDONLY))
		return -1;
	int result = lfs_file_read(&b->fs, &file, data, (lfs_size_t)capacity);
	return lfs_file_close(&b->fs, &file) ? -1 : result;
#else
	(void)opaque;
	(void)path;
	(void)data;
	(void)capacity;
	return -1;
#endif
}
extern "C" int sdk_backend_kv_set(void *opaque, const char *key,
				  const void *data, size_t size)
{
#ifdef CONFIG_SDK_FLASHDB
	auto *b = static_cast<Backend *>(opaque);
	fdb_blob blob{};
	return b && b->kv_ready &&
			       fdb_kv_set_blob(
				       &b->database, key,
				       fdb_blob_make(&blob,
						     const_cast<void *>(data),
						     size)) == FDB_NO_ERR ?
		       0 :
		       -1;
#else
	(void)opaque;
	(void)key;
	(void)data;
	(void)size;
	return -1;
#endif
}
extern "C" int sdk_backend_kv_get(void *opaque, const char *key, void *data,
				  size_t capacity)
{
#ifdef CONFIG_SDK_FLASHDB
	auto *b = static_cast<Backend *>(opaque);
	fdb_blob blob{};
	return b && b->kv_ready ?
		       (int)fdb_kv_get_blob(&b->database, key,
					    fdb_blob_make(&blob, data,
							  capacity)) :
		       -1;
#else
	(void)opaque;
	(void)key;
	(void)data;
	(void)capacity;
	return -1;
#endif
}
namespace
{
#ifdef CONFIG_SDK_LITTLEFS
int storage_error(int code)
{
	if (code >= 0)
		return 0;
	if (code == LFS_ERR_NOENT)
		return SDK_STORAGE_NOT_FOUND;
	if (code == LFS_ERR_NOSPC)
		return SDK_STORAGE_NO_SPACE;
	if (code == LFS_ERR_INVAL || code == LFS_ERR_NAMETOOLONG)
		return SDK_STORAGE_INVALID;
	return SDK_STORAGE_IO;
}
#endif
}
extern "C" void sdk_backend_execute(void *opaque,
				    const sdk_storage_request_t *r,
				    sdk_storage_result_t *out)
{
	*out = {};
	out->result = sdk_storage_validate(r);
	if (out->result)
		return;
	auto *b = static_cast<Backend *>(opaque);
	out->result = SDK_STORAGE_UNAVAILABLE;
	if (!b)
		return;
	const bool kv = r->operation == SDK_STORAGE_KV_SET ||
			r->operation == SDK_STORAGE_KV_GET ||
			r->operation == SDK_STORAGE_KV_DELETE ||
			r->operation == SDK_STORAGE_KV_SIZE;
	if (kv) {
#ifdef CONFIG_SDK_FLASHDB
		if (!b->kv_ready)
			return;
		fdb_kv object{};
		if (r->operation != SDK_STORAGE_KV_SET &&
		    !fdb_kv_get_obj(&b->database, r->key, &object)) {
			out->result = SDK_STORAGE_NOT_FOUND;
			return;
		}
		out->result = 0;
		if (r->operation == SDK_STORAGE_KV_SIZE)
			out->size = object.value_len;
		if (r->operation == SDK_STORAGE_KV_SET)
			out->result =
				sdk_backend_kv_set(b, r->key, r->data, r->size);
		if (r->operation == SDK_STORAGE_KV_DELETE)
			out->result = fdb_kv_del(&b->database, r->key) ==
						      FDB_NO_ERR ?
					      0 :
					      SDK_STORAGE_IO;
		if (r->operation == SDK_STORAGE_KV_GET) {
			out->size = object.value_len;
			if (object.value_len > r->size)
				out->result = SDK_STORAGE_BUFFER_TOO_SMALL;
			else {
				int n = sdk_backend_kv_get(b, r->key, out->data,
							   r->size);
				if (n != (int)object.value_len)
					out->result = SDK_STORAGE_IO;
			}
		}
#endif
		return;
	}
#ifdef CONFIG_SDK_LITTLEFS
	if (!b->mounted)
		return;
	int status = 0;
	if (r->operation == SDK_STORAGE_FILE_SIZE) {
		lfs_info info{};
		status = lfs_stat(&b->fs, r->key, &info);
		if (!status) {
			if (info.type != LFS_TYPE_REG)
				status = LFS_ERR_INVAL;
			else
				out->size = info.size;
		}
	} else if (r->operation == SDK_STORAGE_FILE_REMOVE)
		status = lfs_remove(&b->fs, r->key);
	else if (r->operation == SDK_STORAGE_FILE_MKDIR) {
		status = lfs_mkdir(&b->fs, r->key);
		if (status == LFS_ERR_EXIST) {
			lfs_info info{};
			status = lfs_stat(&b->fs, r->key, &info);
			if (!status && info.type != LFS_TYPE_DIR)
				status = LFS_ERR_INVAL;
		}
	} else if (r->operation == SDK_STORAGE_FILE_RENAME)
		status = lfs_rename(&b->fs, r->key, r->destination);
	else {
		const bool write = r->operation == SDK_STORAGE_FILE_WRITE ||
				   r->operation == SDK_STORAGE_FILE_WRITE_AT;
		int flags = write ? LFS_O_WRONLY | LFS_O_CREAT : LFS_O_RDONLY;
		if (r->operation == SDK_STORAGE_FILE_WRITE)
			flags |= LFS_O_TRUNC;
		lfs_file_t file{};
		status = lfs_file_open(&b->fs, &file, r->key, flags);
		if (!status) {
			if (r->operation == SDK_STORAGE_FILE_READ_AT ||
			    r->operation == SDK_STORAGE_FILE_WRITE_AT)
				status = (int)lfs_file_seek(
					&b->fs, &file, r->offset, LFS_SEEK_SET);
			if (status >= 0) {
				status = write ? (int)lfs_file_write(&b->fs,
								     &file,
								     r->data,
								     r->size) :
						 (int)lfs_file_read(&b->fs,
								    &file,
								    out->data,
								    r->size);
				if (status >= 0)
					out->size = (uint32_t)status;
				if (write && status >= 0 &&
				    status != (int)r->size)
					status = LFS_ERR_NOSPC;
			}
			const int closed = lfs_file_close(&b->fs, &file);
			if (status >= 0 && closed)
				status = closed;
		}
	}
	out->result = storage_error(status);
#endif
}
