#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sphaira::devoptab {
namespace {

// 光鸭云盘 guangyapan API。
// 鉴权：Bearer access_token（可选 refresh_token 自动刷新）。
struct GuangyaDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* API_BASE = "https://api.guangyapan.com";
    static constexpr const char* ACCOUNT_BASE = "https://account.guangyapan.com";

    GuangyaDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        m_access_token = config_extra("access_token");
        m_refresh_token = config_extra("refresh_token");
        m_device_id = config_extra("device_id");
        if (m_device_id.empty()) {
            m_device_id = generate_did();
        }
    }

    bool auth() override {
        if (!m_access_token.empty()) {
            return true;
        }
        if (m_refresh_token.empty()) {
            return false;
        }

        const std::string body = "{\"client_id\":\"aMe-8VSlkrbQXpUR\",\"grant_type\":\"refresh_token\",\"refresh_token\":\"" + m_refresh_token + "\"}";
        curl_slist* h = base_headers(false);
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(ACCOUNT_BASE) + "/v1/auth/token", body, h);

        cloud::Json j(resp);
        if (!j) {
            return false;
        }
        m_access_token = cloud::js_str(j.get(), "access_token", "");
        return !m_access_token.empty();
    }

    int list_dir(const std::string& path, std::vector<cloud::CloudEntry>& out) override {
        out.clear();
        if (!auth()) {
            return -EIO;
        }

        const std::string pid = parent_id(path);
        // 与光鸭官方客户端一致：orderBy=3、sortType=1，且必须携带 fileTypes。
        std::string body = "{\"parentId\":\"" + pid + "\"";
        body += ",\"page\":0,\"pageSize\":100,\"orderBy\":3,\"sortType\":1,\"fileTypes\":[]}";

        const auto resp = do_api_post("/userres/v1/file/get_file_list", body);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        // 成功信号：msg 为空或 "success"；否则 data 为 null，记录错误便于排查。
        const std::string msg = cloud::js_str(j.get(), "msg", "");
        auto data = cloud::js_get(j.get(), "data");
        if (!data && !msg.empty()) {
            log_write("[GUANGYA] get_file_list failed: %s\n", msg.c_str());
            return -EIO;
        }

        yyjson_val* arr = nullptr;
        if (data && yyjson_is_arr(data)) {
            arr = data;
        } else if (data && yyjson_is_obj(data)) {
            arr = cloud::js_get(data, "list");
        }
        if (!arr || !yyjson_is_arr(arr)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(arr, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "fileName", "");
            if (e.name.empty()) {
                e.name = cloud::js_str(item, "name", "");
            }
            e.id = cloud::js_str(item, "fileId", "");
            if (e.id.empty()) {
                e.id = cloud::js_str(item, "resId", "");
            }
            if (e.id.empty()) {
                e.id = cloud::js_str(item, "id", "");
            }
            e.size = cloud::js_u64(item, "fileSize", 0);
            e.is_dir = cloud::js_int(item, "resType", 0) == 2;
            if (!e.name.empty()) {
                out.push_back(std::move(e));
            }
        }
        return 0;
    }

    int resolve_download_url(const cloud::CloudEntry& e, std::string& url, curl_slist*& headers) override {
        (void)headers;
        if (!auth()) {
            return -EIO;
        }

        const std::string body = "{\"fileId\":\"" + e.id + "\"}";

        const auto resp = do_api_post("/userres/v1/get_res_download_url", body);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        auto data = cloud::js_get(j.get(), "data");
        if (data && yyjson_is_str(data)) {
            url = yyjson_get_str(data);
        } else if (data && yyjson_is_obj(data)) {
            url = cloud::js_str(data, "signedURL", "");
            if (url.empty()) {
                url = cloud::js_str(data, "url", "");
            }
            if (url.empty()) {
                url = cloud::js_str(data, "downloadUrl", "");
            }
            if (url.empty()) {
                url = cloud::js_str(data, "download_url", "");
            }
        }
        return url.empty() ? -EIO : 0;
    }

    int make_dir(const std::string& path) override {
        if (!auth()) {
            return -EIO;
        }
        const auto slash = path.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "" : path.substr(0, slash);
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);

        const std::string body = "{\"dirName\":\"" + name + "\",\"parentId\":\"" + parent_id(parent) + "\"}";

        curl_slist* h = base_headers(true);
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/nd.bizuserres.s/v1/file/create_dir", body, h);
        return resp.empty() ? -EIO : 0;
    }

    int remove_entry(const std::string& path, bool is_dir) override {
        (void)is_dir;
        if (!auth()) {
            return -EIO;
        }
        const std::string fid = parent_id(path);
        if (fid.empty()) {
            return -ENOENT;
        }

        const std::string body = "{\"fileIds\":[\"" + fid + "\"]}";

        curl_slist* h = base_headers(true);
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/nd.bizuserres.s/v1/file/delete_file", body, h);
        return resp.empty() ? -EIO : 0;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        (void)old_path;
        (void)new_path;
        // 光鸭 API 未提供重命名接口。
        return -ENOTSUP;
    }

private:
    static std::string generate_did() {
        std::string out;
        out.reserve(32);
        for (int i = 0; i < 32; i++) {
            out += "0123456789abcdef"[std::rand() % 16];
        }
        return out;
    }

    static std::string generate_traceparent() {
        char buf[60];
        char trace[33], parent[17];
        for (int i = 0; i < 32; i++) {
            trace[i] = "0123456789abcdef"[std::rand() % 16];
        }
        trace[32] = 0;
        for (int i = 0; i < 16; i++) {
            parent[i] = "0123456789abcdef"[std::rand() % 16];
        }
        parent[16] = 0;
        std::snprintf(buf, sizeof(buf), "00-%s-%s-01", trace, parent);
        return std::string(buf);
    }

    // POST 到 API 主机；若 access_token 过期（401）则用 refresh_token 刷新一次并重试。
    std::string do_api_post(const std::string& path, const std::string& body) {
        curl_slist* h = base_headers(true);
        std::string resp = http_post(std::string(API_BASE) + path, body, h);
        curl_slist_free_all(h);

        if (m_last_http_code == 401) {
            log_write("[GUANGYA] %s 返回 401，尝试刷新 token\n", path.c_str());
            m_access_token.clear();
            if (auth()) {
                h = base_headers(true);
                resp = http_post(std::string(API_BASE) + path, body, h);
                curl_slist_free_all(h);
            }
        }
        return resp;
    }

    curl_slist* base_headers(bool with_auth) {
        curl_slist* h = nullptr;
        h = curl_slist_append(h, "Content-Type: application/json");
        h = curl_slist_append(h, ("did: " + m_device_id).c_str());
        h = curl_slist_append(h, "dt: 4");
        h = curl_slist_append(h, "origin: https://www.guangyapan.com");
        h = curl_slist_append(h, "referer: https://www.guangyapan.com/");
        h = curl_slist_append(h, "user-agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36");
        h = curl_slist_append(h, ("traceparent: " + generate_traceparent()).c_str());
        h = curl_slist_append(h, ("x-device-id: " + m_device_id).c_str());
        if (with_auth && !m_access_token.empty()) {
            h = curl_slist_append(h, ("authorization: Bearer " + m_access_token).c_str());
        }
        return h;
    }

    std::string m_access_token{};
    std::string m_refresh_token{};
    std::string m_device_id{};
};

} // namespace

Result MountGuangyaAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<GuangyaDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "GUANGYA"
    );
}

} // namespace sphaira::devoptab
