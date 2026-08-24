#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

namespace sphaira::devoptab {
namespace {

// 阿里云盘 aliyundrive open API。
// 鉴权：refresh_token -> access_token（Bearer）。
struct AliyunDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* API_BASE = "https://api.aliyundrive.com";

    AliyunDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        m_refresh_token = config_extra("refresh_token");
        m_access_token = config_extra("access_token");
        m_drive_id = config_extra("drive_id");
    }

    bool auth() override {
        if (!m_access_token.empty()) {
            return true;
        }
        if (m_refresh_token.empty()) {
            return false;
        }

        const std::string body = "{\"refresh_token\":\"" + m_refresh_token + "\",\"grant_type\":\"refresh_token\"}";

        curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
        curl_slist_append(h, "Referer: https://aliyundrive.com");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/v2/account/token", body, h);

        cloud::Json j(resp);
        if (!j) {
            return false;
        }
        m_access_token = cloud::js_str(j.get(), "access_token", "");
        if (m_drive_id.empty()) {
            m_drive_id = cloud::js_str(j.get(), "default_drive_id", "");
        }
        if (!m_access_token.empty()) {
            if (const auto rt = cloud::js_str(j.get(), "refresh_token", ""); *rt) {
                m_refresh_token = rt;
            }
        }
        return !m_access_token.empty();
    }

    int list_dir(const std::string& path, std::vector<cloud::CloudEntry>& out) override {
        out.clear();
        if (!auth()) {
            return -EIO;
        }

        const std::string pid = parent_id(path).empty() ? "root" : parent_id(path);

        std::string body = "{\"drive_id\":\"" + m_drive_id + "\"";
        body += ",\"parent_file_id\":\"" + pid + "\"";
        body += ",\"limit\":100,\"order_by\":\"name\",\"order_direction\":\"ASC\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/adrive/v3/file/list", body, h);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        auto items = cloud::js_get(j.get(), "items");
        if (!items || !yyjson_is_arr(items)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(items, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "name", "");
            e.id = cloud::js_str(item, "file_id", "");
            e.extra = cloud::js_str(item, "drive_id", m_drive_id.c_str());
            e.size = cloud::js_u64(item, "size", 0);
            const std::string type = cloud::js_str(item, "type", "file");
            e.is_dir = (type == "folder");
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

        const std::string did = e.extra.empty() ? m_drive_id : e.extra;
        std::string body = "{\"drive_id\":\"" + did + "\"";
        body += ",\"file_id\":\"" + e.id + "\"";
        body += ",\"expire_sec\":14400}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/v2/file/get_download_url", body, h);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        url = cloud::js_str(j.get(), "url", "");
        if (url.empty()) {
            url = cloud::js_str(j.get(), "cdn_url", "");
        }
        if (url.empty()) {
            url = cloud::js_str(j.get(), "internal_url", "");
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
        const std::string pid = parent_id(parent).empty() ? "root" : parent_id(parent);

        std::string body = "{\"drive_id\":\"" + m_drive_id + "\"";
        body += ",\"parent_file_id\":\"" + pid + "\"";
        body += ",\"name\":\"" + name + "\",\"type\":\"folder\",\"check_name_mode\":\"refuse\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/adrive/v2/file/createWithFolders", body, h);

        cloud::Json j(resp);
        return (j && cloud::js_get(j.get(), "file_id")) ? 0 : -EIO;
    }

    int remove_entry(const std::string& path, bool is_dir) override {
        (void)is_dir;
        if (!auth()) {
            return -EIO;
        }
        cloud::CloudEntry e;
        if (!cache_lookup(path, e) || e.id.empty()) {
            return -ENOENT;
        }
        const std::string did = e.extra.empty() ? m_drive_id : e.extra;

        std::string body = "{\"drive_id\":\"" + did + "\",\"file_id\":\"" + e.id + "\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/v2/recyclebin/trash", body, h);
        return resp.empty() ? -EIO : 0;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        if (!auth()) {
            return -EIO;
        }
        cloud::CloudEntry e;
        if (!cache_lookup(old_path, e) || e.id.empty()) {
            return -ENOENT;
        }
        const std::string did = e.extra.empty() ? m_drive_id : e.extra;
        const auto slash = new_path.find_last_of('/');
        const std::string name = slash == std::string::npos ? new_path : new_path.substr(slash + 1);

        std::string body = "{\"drive_id\":\"" + did + "\",\"file_id\":\"" + e.id + "\",\"name\":\"" + name + "\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/adrive/v3/file/update", body, h);

        cloud::Json j(resp);
        return (j && !cloud::js_get(j.get(), "code")) ? 0 : -EIO;
    }

private:
    curl_slist* headers() {
        curl_slist* h = nullptr;
        h = curl_slist_append(h, "Content-Type: application/json");
        if (!m_access_token.empty()) {
            h = curl_slist_append(h, ("Authorization: Bearer " + m_access_token).c_str());
        }
        h = curl_slist_append(h, "Referer: https://aliyundrive.com");
        return h;
    }

    std::string m_refresh_token{};
    std::string m_access_token{};
    std::string m_drive_id{};
};

} // namespace

Result MountAliyunAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<AliyunDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "ALIYUN"
    );
}

} // namespace sphaira::devoptab
