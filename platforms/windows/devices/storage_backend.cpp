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
namespace {
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
constexpr long block_size = 4096, block_count = 128;
bool range_ok(lfs_block_t block, lfs_off_t offset, lfs_size_t size) {
    return block < block_count && offset <= block_size && size <= block_size - offset;
}
int read_block(const lfs_config *cfg, lfs_block_t block, lfs_off_t offset, void *data, lfs_size_t size) {
    auto *b = static_cast<Backend *>(cfg->context);
    if (!range_ok(block, offset, size) || std::fseek(b->flash, block * block_size + offset, SEEK_SET)) return LFS_ERR_IO;
    return std::fread(data, 1, size, b->flash) == size ? 0 : LFS_ERR_IO;
}
int program_block(const lfs_config *cfg, lfs_block_t block, lfs_off_t offset, const void *data, lfs_size_t size) {
    auto *b = static_cast<Backend *>(cfg->context);
    std::array<unsigned char, 512> before{};
    auto *bytes = static_cast<const unsigned char *>(data);
    if (!range_ok(block, offset, size)) return LFS_ERR_IO;
    for (lfs_size_t done = 0; done < size;) {
        const auto count = (size - done) < before.size() ? size - done : before.size();
        if (read_block(cfg, block, offset + done, before.data(), (lfs_size_t)count)) return LFS_ERR_IO;
        for (size_t i = 0; i < count; ++i) if ((before[i] & bytes[done + i]) != bytes[done + i]) return LFS_ERR_CORRUPT;
        done += (lfs_size_t)count;
    }
    if (std::fseek(b->flash, block * block_size + offset, SEEK_SET)) return LFS_ERR_IO;
    return std::fwrite(data, 1, size, b->flash) == size ? 0 : LFS_ERR_IO;
}
int erase_block(const lfs_config *cfg, lfs_block_t block) {
    auto *b = static_cast<Backend *>(cfg->context);
    std::array<unsigned char, block_size> empty; empty.fill(0xff);
    if (block >= block_count || std::fseek(b->flash, block * block_size, SEEK_SET)) return LFS_ERR_IO;
    return std::fwrite(empty.data(), 1, empty.size(), b->flash) == empty.size() ? 0 : LFS_ERR_IO;
}
int sync_blocks(const lfs_config *cfg) { return std::fflush(static_cast<Backend *>(cfg->context)->flash) ? LFS_ERR_IO : 0; }
#endif
}
extern "C" void sdk_backend_close(void *opaque) {
    auto *b = static_cast<Backend *>(opaque);
    if (!b) return;
#ifdef CONFIG_SDK_FLASHDB
    if (b->kv_ready) fdb_kvdb_deinit(&b->database);
#endif
#ifdef CONFIG_SDK_LITTLEFS
    if (b->mounted) lfs_unmount(&b->fs);
    if (b->flash) std::fclose(b->flash);
#endif
    delete b;
}
extern "C" void *sdk_backend_open(const char *directory) {
    auto *b = new Backend;
    b->directory = directory;
    std::error_code error;
    std::filesystem::create_directories(b->directory, error);
    if (error) { sdk_backend_close(b); return nullptr; }
#ifdef CONFIG_SDK_LITTLEFS
    const auto image = std::filesystem::path(directory) / "littlefs.img";
    const bool created = !std::filesystem::exists(image);
    b->flash = std::fopen(image.string().c_str(), created ? "w+b" : "r+b");
    if (!b->flash) { sdk_backend_close(b); return nullptr; }
    b->cfg.context = b; b->cfg.read = read_block; b->cfg.prog = program_block;
    b->cfg.erase = erase_block; b->cfg.sync = sync_blocks;
    b->cfg.read_size = 16; b->cfg.prog_size = 16;
    b->cfg.block_size = block_size; b->cfg.block_count = block_count;
    b->cfg.block_cycles = 500; b->cfg.cache_size = 256; b->cfg.lookahead_size = 16;
    if (created) {
        for (lfs_block_t block = 0; block < block_count; ++block) if (erase_block(&b->cfg, block)) { sdk_backend_close(b); return nullptr; }
        if (lfs_format(&b->fs, &b->cfg)) { sdk_backend_close(b); return nullptr; }
    } else {
        std::fseek(b->flash, 0, SEEK_END);
        if (std::ftell(b->flash) != block_size * block_count) { sdk_backend_close(b); return nullptr; }
    }
    // Existing corrupt images are reported; never auto-format user data.
    if (lfs_mount(&b->fs, &b->cfg)) { sdk_backend_close(b); return nullptr; }
    b->mounted = true;
#endif
#ifdef CONFIG_SDK_FLASHDB
    b->kv_directory = (std::filesystem::path(directory) / "flashdb").string();
    std::filesystem::create_directories(b->kv_directory, error);
    if (error) { sdk_backend_close(b); return nullptr; }
    bool file_mode = true;
    uint32_t sector = 4096, maximum = sector * 16;
    fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_SEC_SIZE, &sector);
    fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_MAX_SIZE, &maximum);
    fdb_kvdb_control(&b->database, FDB_KVDB_CTRL_SET_FILE_MODE, &file_mode);
    if (fdb_kvdb_init(&b->database, "settings", b->kv_directory.c_str(), nullptr, nullptr) != FDB_NO_ERR) {
        sdk_backend_close(b); return nullptr;
    }
    b->kv_ready = true;
#endif
    return b;
}
extern "C" int sdk_backend_file_write(void *opaque, const char *path, const void *data, size_t size) {
#ifdef CONFIG_SDK_LITTLEFS
    auto *b = static_cast<Backend *>(opaque);
    lfs_file_t file{};
    if (!b || !b->mounted || lfs_file_open(&b->fs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC)) return -1;
    auto result = lfs_file_write(&b->fs, &file, data, (lfs_size_t)size);
    int close_result = lfs_file_close(&b->fs, &file);
    return result == (lfs_ssize_t)size && !close_result ? 0 : -1;
#else
    (void)opaque; (void)path; (void)data; (void)size; return -1;
#endif
}
extern "C" int sdk_backend_file_read(void *opaque, const char *path, void *data, size_t capacity) {
#ifdef CONFIG_SDK_LITTLEFS
    auto *b = static_cast<Backend *>(opaque);
    lfs_file_t file{};
    if (!b || !b->mounted || lfs_file_open(&b->fs, &file, path, LFS_O_RDONLY)) return -1;
    int result = lfs_file_read(&b->fs, &file, data, (lfs_size_t)capacity);
    return lfs_file_close(&b->fs, &file) ? -1 : result;
#else
    (void)opaque; (void)path; (void)data; (void)capacity; return -1;
#endif
}
extern "C" int sdk_backend_kv_set(void *opaque, const char *key, const void *data, size_t size) {
#ifdef CONFIG_SDK_FLASHDB
    auto *b = static_cast<Backend *>(opaque); fdb_blob blob{};
    return b && b->kv_ready && fdb_kv_set_blob(&b->database, key, fdb_blob_make(&blob, const_cast<void *>(data), size)) == FDB_NO_ERR ? 0 : -1;
#else
    (void)opaque; (void)key; (void)data; (void)size; return -1;
#endif
}
extern "C" int sdk_backend_kv_get(void *opaque, const char *key, void *data, size_t capacity) {
#ifdef CONFIG_SDK_FLASHDB
    auto *b = static_cast<Backend *>(opaque); fdb_blob blob{};
    return b && b->kv_ready ? (int)fdb_kv_get_blob(&b->database, key, fdb_blob_make(&blob, data, capacity)) : -1;
#else
    (void)opaque; (void)key; (void)data; (void)capacity; return -1;
#endif
}
