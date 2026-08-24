#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

namespace sphaira::devoptab {
namespace {

// 谷歌网盘 Google Drive API v3。
// 鉴权：OAuth2（client_id / client_secret / refresh_token -> access_token）。
struct GoogleDriveDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* API_BASE = "https://www.googleapis.com/drive/v3";
    static constexpr const char* TOKEN_URI = "https://oauth2.googleapis.com/token";

    GoogleDriveDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        m_access_token = config_extra("access_token");
        m_refresh_token = config_extra("refresh_token");
        m_client_id = config_extra("client_id");
        m_client_secret = config_extra("client_secret");
    }

    bool auth() override {
        if (!m_access_token.empty()) {
            return true;
        }
        if (m_refresh_token.empty() || m_client_id.empty() || m_client_secret.empty()) {
            return false;
        }

        const std::string body =
            "grant_type=refresh_token"
            "&refresh_token=" + url_encode(m_refresh_token) +
            "&client_id=" + url_encode(m_client_id) +
            "&client_secret=" + url_encode(m_client_secret);

        const auto resp = http_post(TOKEN_URI, body, nullptr);
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

        const std::string pid = parent_id(path).empty() ? "root" : parent_id(path);
        const std::string q = "'" + pid + "' in parents and trashed=false";

        std::string url = std::string(API_BASE) + "/files?q=" + url_encode(q);
        url += "&fields=files(id,name,mimeType,size)";
        url += "&pageSize=1000";
        url += "&supportsAllDrives=true&includeItemsFromAllDrives=true";

        curl_slist* h = auth_headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_get(url, h);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        auto files = cloud::js_get(j.get(), "files");
        if (!files || !yyjson_is_arr(files)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(files, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "name", "");
            e.id = cloud::js_str(item, "id", "");
            const std::string mime = cloud::js_str(item, "mimeType", "");
            e.is_dir = (mime == "application/vnd.google-apps.folder");
            e.size = cloud::js_u64(item, "size", 0);
            if (!e.name.empty()) {
                out.push_back(std::move(e));
            }
        }
        return 0;
    }

    int resolve_download_url(const cloud::CloudEntry& e, std::string& url, curl_slist*& headers) override {
        if (!auth()) {
            return -EIO;
        }
        url = std::string(API_BASE) + "/files/" + url_encode(e.id) + "?alt=media";
        headers = auth_headers(); // 所有权交给 File，close 时释放
        return 0;
    }

    int make_dir(const std::string& path) override {
        if (!auth()) {
            return -EIO;
        }
        const auto slash = path.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "" : path.substr(0, slash);
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::string pid = parent_id(parent).empty() ? "root" : parent_id(parent);

        const std::string body =
            "{\"name\":\"" + name + "\","
            "\"mimeType\":\"application/vnd.google-apps.folder\","
            "\"parents\":[\"" + pid + "\"]}";

        curl_slist* h = auth_headers();
        curl_slist_append(h, "Content-Type: application/json");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/files", body, h);

        cloud::Json j(resp);
        return (j && !cloud::js_get(j.get(), "error")) ? 0 : -EIO;
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

        curl_slist* h = auth_headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        http_request(std::string(API_BASE) + "/files/" + url_encode(fid), "DELETE", {}, h);
        return (m_last_http_code >= 200 && m_last_http_code < 300) ? 0 : -EIO;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        if (!auth()) {
            return -EIO;
        }
        const std::string fid = parent_id(old_path);
        if (fid.empty()) {
            return -ENOENT;
        }
        const auto slash = new_path.find_last_of('/');
        const std::string name = slash == std::string::npos ? new_path : new_path.substr(slash + 1);

        const std::string body = "{\"name\":\"" + name + "\"}";

        curl_slist* h = auth_headers();
        curl_slist_append(h, "Content-Type: application/json");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_request(std::string(API_BASE) + "/files/" + url_encode(fid), "PATCH", body, h);

        cloud::Json j(resp);
        return (j && !cloud::js_get(j.get(), "error")) ? 0 : -EIO;
    }

private:
    curl_slist* auth_headers() {
        curl_slist* h = nullptr;
        if (!m_access_token.empty()) {
            h = curl_slist_append(h, ("Authorization: Bearer " + m_access_token).c_str());
        }
        return h;
    }

    std::string m_access_token{};
    std::string m_refresh_token{};
    std::string m_client_id{};
    std::string m_client_secret{};
};

} // namespace

Result MountGoogleDriveAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<GoogleDriveDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "GOOGLEDRIVE"
    );
}

} // namespace sphaira::devoptab
