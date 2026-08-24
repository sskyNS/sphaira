#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

#include <cstring>

namespace sphaira::devoptab {
namespace {

// 百度网盘 PCS 开放平台 API。
// 鉴权：OAuth2（client_id / client_secret / refresh_token -> access_token）。
struct BaiduDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* API_BASE = "https://pan.baidu.com/rest/2.0/xpan";
    static constexpr const char* OAUTH_TOKEN = "https://openapi.baidu.com/oauth/2.0/token";

    BaiduDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
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

        const auto resp = http_post(OAUTH_TOKEN, body, nullptr);
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

        const std::string remote_dir = path.empty() ? "/" : path;
        std::string url = std::string(API_BASE) + "/file?method=list";
        url += "&dir=" + url_encode(remote_dir);
        url += "&order=name&desc=0&limit=1000&web=1";
        url += "&access_token=" + url_encode(m_access_token);

        const auto resp = get(url);
        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }
        if (cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }

        auto list = cloud::js_get(j.get(), "list");
        if (!list || !yyjson_is_arr(list)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(list, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "server_filename", "");
            e.id = cloud::js_str(item, "path", "");
            e.extra = std::to_string(cloud::js_u64(item, "fs_id", 0));
            e.size = cloud::js_u64(item, "size", 0);
            e.mtime = cloud::js_u64(item, "server_mtime", 0);
            e.is_dir = cloud::js_int(item, "isdir", 0) == 1;
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

        std::string req = std::string(API_BASE) + "/multimedia?method=filemetas";
        req += "&fsids=[" + url_encode(e.extra) + "]";
        req += "&dlink=1";
        req += "&access_token=" + url_encode(m_access_token);

        const auto resp = get(req);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }

        auto list = cloud::js_get(j.get(), "list");
        if (!list || !yyjson_is_arr(list) || yyjson_arr_size(list) == 0) {
            return -EIO;
        }

        auto first = yyjson_arr_get_first(list);
        std::string dlink = cloud::js_str(first, "dlink", "");
        if (dlink.empty()) {
            return -EIO;
        }

        dlink += (dlink.find('?') == std::string::npos ? '?' : '&');
        dlink += "access_token=" + url_encode(m_access_token);
        url = std::move(dlink);
        return 0;
    }

    int make_dir(const std::string& path) override {
        if (!auth()) {
            return -EIO;
        }
        const std::string body = "path=" + url_encode(path.empty() ? "/" : path) + "&isdir=1&rtype=1";
        const auto resp = post(std::string(API_BASE) + "/file?method=create", body);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }
        return 0;
    }

    int remove_entry(const std::string& path, bool is_dir) override {
        (void)is_dir;
        if (!auth()) {
            return -EIO;
        }
        const std::string filelist = "[\"" + path + "\"]";
        const std::string body = "async=0&ondup=overwrite&filelist=" + url_encode(filelist);
        const auto resp = post(std::string(API_BASE) + "/file?method=filemanager&opera=delete", body);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }
        return 0;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        if (!auth()) {
            return -EIO;
        }
        const auto slash = new_path.find_last_of('/');
        const std::string new_name = slash == std::string::npos ? new_path : new_path.substr(slash + 1);
        const std::string filelist = "[{\"path\":\"" + old_path + "\",\"newname\":\"" + new_name + "\"}]";
        const std::string body = "async=0&ondup=overwrite&filelist=" + url_encode(filelist);
        const auto resp = post(std::string(API_BASE) + "/file?method=filemanager&opera=rename", body);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }
        return 0;
    }

private:
    std::string get(const std::string& url) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_get(url, h);
    }

    std::string post(const std::string& url, const std::string& body) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_post(url, body, h);
    }

    std::string m_access_token{};
    std::string m_refresh_token{};
    std::string m_client_id{};
    std::string m_client_secret{};
};

} // namespace

Result MountBaiduAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<BaiduDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "BAIDU"
    );
}

} // namespace sphaira::devoptab
