#pragma once

// 云盘 devoptab 挂载基础设施。
// 百度网盘 / 谷歌网盘 / 夸克网盘 / 阿里云盘 / 光鸭云盘 等 HTTP API 类网盘，
// 都通过继承 CloudDiskDevice 并实现少量 provider 专属虚函数来完成挂载，
// 从而直接复用 sphaira 文件浏览器 + 在线安装游戏的完整 UI 与操作逻辑。

#include "utils/devoptab_common.hpp"

#include <switch.h>
#include <curl/curl.h>
#include <yyjson.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>

namespace sphaira::devoptab::cloud {

// 单个远端条目（文件或目录）。
struct CloudEntry {
    std::string name{};   // 显示名（文件名 / 目录名）
    std::string id{};     // 远端主 ID（用于下载 / 导航，视 provider 而定）
    std::string extra{};  // 远端辅助 ID（如百度 fs_id、阿里 drive_id 等）
    u64 size{};
    u64 mtime{};
    bool is_dir{};
};

// devoptab 打开的文件句柄内存。
// 注意：该结构由 newlib 通过 calloc 分配，必须是 POD（不能有 std::string / std::vector 等成员）。
struct File {
    std::string* path{};
    CloudEntry* entry{};
    std::string* dl_url{};
    curl_slist* headers{};
    bool dl_resolved{};
    common::PushPullThreadData* pdata{};
    u64 off{};
    u64 last_off{};
    bool write_mode{};
};

// devoptab 目录迭代状态（同样必须是 POD）。
struct Dir {
    std::vector<CloudEntry>* entries{};
    size_t index{};
};

// ---- yyjson 小工具 ----
struct Json {
    yyjson_doc* doc{};
    yyjson_val* root{};

    explicit Json(const std::string& data) {
        doc = yyjson_read(data.c_str(), data.size(), 0);
        root = doc ? yyjson_doc_get_root(doc) : nullptr;
    }

    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;

    ~Json() {
        if (doc) {
            yyjson_doc_free(doc);
        }
    }

    explicit operator bool() const {
        return root != nullptr;
    }

    yyjson_val* get() const {
        return root;
    }
};

inline yyjson_val* js_get(yyjson_val* obj, const char* key) {
    return obj ? yyjson_obj_get(obj, key) : nullptr;
}

inline const char* js_str(yyjson_val* obj, const char* key, const char* def = "") {
    auto v = js_get(obj, key);
    if (!v || !yyjson_is_str(v)) {
        return def;
    }
    return yyjson_get_str(v);
}

inline u64 js_u64(yyjson_val* obj, const char* key, u64 def = 0) {
    auto v = js_get(obj, key);
    if (!v) {
        return def;
    }
    if (yyjson_is_uint(v)) {
        return yyjson_get_uint(v);
    }
    if (yyjson_is_sint(v)) {
        const auto s = yyjson_get_sint(v);
        return s < 0 ? def : (u64)s;
    }
    if (yyjson_is_str(v)) {
        return (u64)strtoull(yyjson_get_str(v), nullptr, 10);
    }
    return def;
}

inline s64 js_i64(yyjson_val* obj, const char* key, s64 def = 0) {
    auto v = js_get(obj, key);
    if (!v) {
        return def;
    }
    if (yyjson_is_sint(v)) {
        return yyjson_get_sint(v);
    }
    if (yyjson_is_uint(v)) {
        return (s64)yyjson_get_uint(v);
    }
    if (yyjson_is_str(v)) {
        return (s64)strtoll(yyjson_get_str(v), nullptr, 10);
    }
    return def;
}

inline int js_int(yyjson_val* obj, const char* key, int def = 0) {
    return (int)js_i64(obj, key, def);
}

inline bool js_bool(yyjson_val* obj, const char* key, bool def = false) {
    auto v = js_get(obj, key);
    if (!v || !yyjson_is_bool(v)) {
        return def;
    }
    return yyjson_get_bool(v);
}

// 所有云盘 devoptab 设备的共享基类。
struct CloudDiskDevice : common::MountCurlDevice {
    using MountCurlDevice::MountCurlDevice;

    ~CloudDiskDevice() override;

    // ---- provider 必须实现 ----
    // 确保鉴权有效（必要时刷新 token）。
    virtual bool auth() = 0;
    // 列出目录 `path`（"" 表示根目录）下的条目。
    virtual int list_dir(const std::string& path, std::vector<CloudEntry>& out) = 0;
    // 解析下载直链；若需要额外请求头，用 curl_slist 构造并通过 `headers` 返回（所有权交给 File）。
    virtual int resolve_download_url(const CloudEntry& e, std::string& url, curl_slist*& headers) = 0;
    virtual int make_dir(const std::string& path) = 0;
    virtual int remove_entry(const std::string& path, bool is_dir) = 0;
    virtual int rename_entry(const std::string& old_path, const std::string& new_path) = 0;

    // ---- 通用 devoptab 实现 ----
    int devoptab_open(void* fs, const char* path, int flags, int mode) override;
    int devoptab_close(void* fd) override;
    ssize_t devoptab_read(void* fd, char* ptr, size_t len) override;
    ssize_t devoptab_seek(void* fd, off_t pos, int dir) override;
    int devoptab_fstat(void* fd, struct stat* st) override;
    int devoptab_diropen(void* fd, const char* path) override;
    int devoptab_dirreset(void* fd) override;
    int devoptab_dirnext(void* fd, char* filename, struct stat* filestat) override;
    int devoptab_dirclose(void* fd) override;
    int devoptab_lstat(const char* path, struct stat* st) override;
    int devoptab_mkdir(const char* path, int mode) override;
    int devoptab_unlink(const char* path) override;
    int devoptab_rmdir(const char* path) override;
    int devoptab_rename(const char* old_name, const char* new_name) override;

protected:
    // 读取 mount ini 里的额外键值（鉴权信息）。
    std::string config_extra(const char* key, const std::string& def = {}) const;

    // HTTP 请求（复用 this->curl 单例，供元数据查询）。
    std::string http_get(const std::string& url, curl_slist* headers = nullptr);
    std::string http_post(const std::string& url, const std::string& body, curl_slist* headers = nullptr);
    std::string http_request(const std::string& url, const std::string& method, const std::string& body, curl_slist* headers);

    // 通过 curl 生成一个带 Range 的流式下载任务。
    common::PushThreadData* create_download(const std::string& url, size_t offset, curl_slist* headers);

    static std::string url_encode(const std::string& s);
    static std::string norm_path(const char* path);
    std::string child_path(const std::string& parent, const std::string& name) const;

    // 返回 `path` 对应远端父 ID（根目录返回空串）。
    std::string parent_id(const std::string& path) const;
    void cache_store(const std::string& path, const CloudEntry& e);
    bool cache_lookup(const std::string& path, CloudEntry& out) const;
    // 列目录并写入缓存。
    int fetch_dir(const std::string& path, std::vector<CloudEntry>& out);
    // 确保 `path` 已在缓存中（必要时列其父目录）。
    int ensure_cached(const std::string& path, CloudEntry& out);

    long m_last_http_code{};
    std::unordered_map<std::string, CloudEntry> m_cache{};
};

} // namespace sphaira::devoptab::cloud
