#include "utils/cloud_disk.hpp"

#include "defines.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace sphaira::devoptab::cloud {

CloudDiskDevice::~CloudDiskDevice() = default;

std::string CloudDiskDevice::config_extra(const char* key, const std::string& def) const {
    auto it = config.extra.find(key);
    return it == config.extra.end() ? def : it->second;
}

std::string CloudDiskDevice::url_encode(const std::string& s) {
    auto* encoded = curl_easy_escape(nullptr, s.c_str(), (int)s.size());
    if (!encoded) {
        return s;
    }
    std::string out(encoded);
    curl_free(encoded);
    return out;
}

std::string CloudDiskDevice::norm_path(const char* path) {
    if (!path) {
        return {};
    }
    std::string p(path);
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    if (p == "/") {
        return {};
    }
    return p;
}

std::string CloudDiskDevice::child_path(const std::string& parent, const std::string& name) const {
    if (parent.empty()) {
        return "/" + name;
    }
    return parent + "/" + name;
}

std::string CloudDiskDevice::parent_id(const std::string& path) const {
    auto it = m_cache.find(path);
    return it == m_cache.end() ? std::string{} : it->second.id;
}

void CloudDiskDevice::cache_store(const std::string& path, const CloudEntry& e) {
    m_cache[path] = e;
}

bool CloudDiskDevice::cache_lookup(const std::string& path, CloudEntry& out) const {
    auto it = m_cache.find(path);
    if (it == m_cache.end()) {
        return false;
    }
    out = it->second;
    return true;
}

int CloudDiskDevice::fetch_dir(const std::string& path, std::vector<CloudEntry>& out) {
    out.clear();
    if (!auth()) {
        log_write_feature("[CLOUD] auth failed for dir %s\n", path.c_str());
        return -EIO;
    }

    const int rc = list_dir(path, out);
    if (rc < 0) {
        return rc;
    }

    for (const auto& e : out) {
        cache_store(child_path(path, e.name), e);
    }
    return 0;
}

int CloudDiskDevice::ensure_cached(const std::string& path, CloudEntry& out) {
    if (path.empty()) {
        out = CloudEntry{};
        out.is_dir = true;
        return 0;
    }

    if (cache_lookup(path, out)) {
        return 0;
    }

    const auto slash = path.find_last_of('/');
    const std::string parent = (slash == std::string::npos) ? std::string{} : path.substr(0, slash);

    std::vector<CloudEntry> entries;
    const int rc = fetch_dir(parent, entries);
    if (rc < 0) {
        return rc;
    }

    if (cache_lookup(path, out)) {
        return 0;
    }
    return -ENOENT;
}

std::string CloudDiskDevice::http_request(const std::string& url, const std::string& method, const std::string& body, curl_slist* headers) {
    std::vector<char> buf;
    curl_set_common_options(this->curl, url);
    if (headers) {
        curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, headers);
    }
    curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, MountCurlDevice::write_memory_callback);
    curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, (void*)&buf);

    if (method == "POST") {
        curl_easy_setopt(this->curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        }
    } else if (!method.empty() && method != "GET") {
        curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        }
    }

    const auto res = curl_easy_perform(this->curl);
    long code = 0;
    curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &code);
    m_last_http_code = code;

    if (res != CURLE_OK) {
        log_write_feature("[CLOUD] %s %s failed: %s\n", method.c_str(), url.c_str(), curl_easy_strerror(res));
        return {};
    }

    return std::string(buf.data(), buf.size());
}

std::string CloudDiskDevice::http_get(const std::string& url, curl_slist* headers) {
    return http_request(url, "GET", {}, headers);
}

std::string CloudDiskDevice::http_post(const std::string& url, const std::string& body, curl_slist* headers) {
    return http_request(url, "POST", body, headers);
}

common::PushThreadData* CloudDiskDevice::create_download(const std::string& url, size_t offset, curl_slist* headers) {
    auto* data = new common::PushThreadData{this->transfer_curl};
    if (!data) {
        return nullptr;
    }

    curl_set_common_options(this->transfer_curl, url);
    // 云盘下载直链需要固定的 Referer（由 provider 的 headers 提供），不能跟随重定向自动改写；
    // 同时关闭自动 Accept-Encoding，避免 CDN 因压缩头返回 412。
    curl_easy_setopt(this->transfer_curl, CURLOPT_AUTOREFERER, 0L);
    curl_easy_setopt(this->transfer_curl, CURLOPT_ACCEPT_ENCODING, (char*)nullptr);
    // 取消下载时 curl 可能因 buffer 满而暂停，设置低速超时兜底，确保最终能中止传输。
    curl_easy_setopt(this->transfer_curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(this->transfer_curl, CURLOPT_LOW_SPEED_TIME, 10L);
    curl_easy_setopt(this->transfer_curl, CURLOPT_BUFFERSIZE, 1024L * 256L);
    curl_easy_setopt(this->transfer_curl, CURLOPT_WRITEFUNCTION, common::PushThreadData::push_thread_callback);
    curl_easy_setopt(this->transfer_curl, CURLOPT_WRITEDATA, (void*)data);
    if (headers) {
        curl_easy_setopt(this->transfer_curl, CURLOPT_HTTPHEADER, headers);
    }

    if (offset > 0) {
        char range[64];
        std::snprintf(range, sizeof(range), "%zu-", offset);
        curl_easy_setopt(this->transfer_curl, CURLOPT_RANGE, range);
    }

    if (R_FAILED(data->CreateAndStart())) {
        delete data;
        return nullptr;
    }

    return data;
}

int CloudDiskDevice::devoptab_open(void* fs, const char* path, int flags, int mode) {
    (void)mode;
    auto* f = static_cast<File*>(fs);

    if (flags & O_APPEND) {
        return -E2BIG;
    }

    const auto p = norm_path(path);
    CloudEntry e;
    const int rc = ensure_cached(p, e);
    if (rc < 0) {
        return rc;
    }

    if (e.is_dir) {
        return -EISDIR;
    }

    f->path = new std::string(p);
    f->entry = new CloudEntry(e);
    f->write_mode = (flags & (O_WRONLY | O_RDWR));
    return 0;
}

int CloudDiskDevice::devoptab_close(void* fd) {
    auto* f = static_cast<File*>(fd);
    delete f->pdata;
    f->pdata = nullptr;
    if (f->headers) {
        curl_slist_free_all(f->headers);
        f->headers = nullptr;
    }
    delete f->path;
    delete f->entry;
    delete f->dl_url;
    f->path = nullptr;
    f->entry = nullptr;
    f->dl_url = nullptr;
    return 0;
}

ssize_t CloudDiskDevice::devoptab_read(void* fd, char* ptr, size_t len) {
    auto* f = static_cast<File*>(fd);
    auto* entry = f->entry;

    if (f->write_mode) {
        return -EBADF;
    }
    if (!len || f->off >= entry->size) {
        return 0;
    }

    len = (size_t)std::min<u64>(len, entry->size - f->off);

    if (f->off != f->last_off) {
        f->last_off = f->off;
        delete f->pdata;
        f->pdata = nullptr;
    }

    if (!f->pdata) {
        if (!f->dl_resolved) {
            std::string url;
            curl_slist* headers = nullptr;
            const int rc = resolve_download_url(*entry, url, headers);
            if (rc < 0) {
                if (headers) {
                    curl_slist_free_all(headers);
                }
                log_write_feature("[CLOUD] resolve_download_url failed rc=%d name=%s\n", rc, entry->name.c_str());
                return rc;
            }
            f->dl_url = new std::string(std::move(url));
            f->headers = headers;
            f->dl_resolved = true;
            log_write_feature("[CLOUD] download start name=%s size=%llu\n", entry->name.c_str(), (unsigned long long)entry->size);
        }
        if (f->dl_url->empty()) {
            return -EIO;
        }
        f->pdata = create_download(*f->dl_url, f->off, f->headers);
        if (!f->pdata) {
            return -EIO;
        }
    }

    const auto ret = f->pdata->PullData(ptr, len);
    f->off += ret;
    f->last_off = f->off;

    // 下载提前结束（curl 已完成但字节数不足），记录诊断信息便于定位缓存文件不完整。
    if (ret == 0 && f->off < entry->size) {
        log_write_feature("[CLOUD] download truncated name=%s off=%llu expected=%llu\n",
            entry->name.c_str(), (unsigned long long)f->off, (unsigned long long)entry->size);
    }

    return (ssize_t)ret;
}

ssize_t CloudDiskDevice::devoptab_seek(void* fd, off_t pos, int dir) {
    auto* f = static_cast<File*>(fd);
    auto* entry = f->entry;

    if (dir == SEEK_CUR) {
        pos += (off_t)f->off;
    } else if (dir == SEEK_END) {
        pos = (off_t)entry->size;
    }

    if (f->write_mode && (u64)pos != f->off) {
        return (ssize_t)f->off;
    }

    f->off = (u64)std::clamp<off_t>(pos, 0, (off_t)entry->size);
    return (ssize_t)f->off;
}

int CloudDiskDevice::devoptab_fstat(void* fd, struct stat* st) {
    auto* f = static_cast<File*>(fd);
    auto* entry = f->entry;
    std::memset(st, 0, sizeof(*st));

    if (entry->is_dir) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    } else {
        st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
        st->st_size = (off_t)entry->size;
    }
    st->st_nlink = 1;
    return 0;
}

int CloudDiskDevice::devoptab_diropen(void* fd, const char* path) {
    auto* d = static_cast<Dir*>(fd);
    const auto p = norm_path(path);

    auto* entries = new std::vector<CloudEntry>();
    const int rc = fetch_dir(p, *entries);
    if (rc < 0) {
        delete entries;
        return rc;
    }

    d->entries = entries;
    d->index = 0;
    return 0;
}

int CloudDiskDevice::devoptab_dirreset(void* fd) {
    static_cast<Dir*>(fd)->index = 0;
    return 0;
}

int CloudDiskDevice::devoptab_dirnext(void* fd, char* filename, struct stat* filestat) {
    auto* d = static_cast<Dir*>(fd);

    if (d->index >= d->entries->size()) {
        return -ENOENT;
    }

    auto& e = (*d->entries)[d->index];
    std::memset(filestat, 0, sizeof(*filestat));
    if (e.is_dir) {
        filestat->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    } else {
        filestat->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
        filestat->st_size = (off_t)e.size;
    }
    filestat->st_nlink = 1;
    std::strcpy(filename, e.name.c_str());

    d->index++;
    return 0;
}

int CloudDiskDevice::devoptab_dirclose(void* fd) {
    auto* d = static_cast<Dir*>(fd);
    delete d->entries;
    d->entries = nullptr;
    d->index = 0;
    return 0;
}

int CloudDiskDevice::devoptab_lstat(const char* path, struct stat* st) {
    const auto p = norm_path(path);
    std::memset(st, 0, sizeof(*st));

    CloudEntry e;
    const int rc = ensure_cached(p, e);
    if (rc < 0) {
        return rc;
    }

    if (e.is_dir) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    } else {
        st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
        st->st_size = (off_t)e.size;
    }
    st->st_nlink = 1;
    if (e.mtime) {
        st->st_mtime = (time_t)e.mtime;
        st->st_atime = (time_t)e.mtime;
        st->st_ctime = (time_t)e.mtime;
    }
    return 0;
}

int CloudDiskDevice::devoptab_mkdir(const char* path, int mode) {
    (void)mode;
    if (!auth()) {
        return -EIO;
    }
    const auto p = norm_path(path);
    const int rc = make_dir(p);
    if (rc == 0) {
        m_cache.erase(p);
    }
    return rc;
}

int CloudDiskDevice::devoptab_unlink(const char* path) {
    if (!auth()) {
        return -EIO;
    }
    const auto p = norm_path(path);
    const int rc = remove_entry(p, false);
    if (rc == 0) {
        m_cache.erase(p);
    }
    return rc;
}

int CloudDiskDevice::devoptab_rmdir(const char* path) {
    if (!auth()) {
        return -EIO;
    }
    const auto p = norm_path(path);
    const int rc = remove_entry(p, true);
    if (rc == 0) {
        m_cache.erase(p);
    }
    return rc;
}

int CloudDiskDevice::devoptab_rename(const char* old_name, const char* new_name) {
    if (!auth()) {
        return -EIO;
    }
    const auto op = norm_path(old_name);
    const auto np = norm_path(new_name);
    const int rc = rename_entry(op, np);
    if (rc == 0) {
        m_cache.erase(op);
        m_cache.erase(np);
    }
    return rc;
}

} // namespace sphaira::devoptab::cloud
