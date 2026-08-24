#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "hasher.hpp"
#include "log.hpp"

#include <cctype>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <unordered_map>

namespace sphaira::devoptab {
namespace {

// 大小写不敏感地从 ini 的 extra 键值里查找（兼容 AppKey/SecretKey/SignKey 等写法）。
std::string FindExtraCI(const std::unordered_map<std::string, std::string>& extra, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const size_t n = std::strlen(key);
        for (const auto& [k, v] : extra) {
            if (k.size() == n && !strncasecmp(k.c_str(), key, n)) {
                return v;
            }
        }
    }
    return {};
}

// 从形如 "xxx;BDUSS=yyy;..." 的 cookie 里提取 BDUSS。
std::string ExtractBduss(const std::string& cookie) {
    std::string upper = cookie;
    for (auto& c : upper) {
        c = (char)std::toupper((unsigned char)c);
    }
    const size_t pos = upper.find("BDUSS=");
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + 6; // strlen("BDUSS=")
    const size_t end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string md5_hex(const std::string& in) {
    std::string out;
    if (R_FAILED(hash::Hash(nullptr, hash::Type::Md5, std::span<const u8>{(const u8*)in.data(), in.size()}, out))) {
        return {};
    }
    return out;
}

// 百度网盘：优先用「扫码登录」得到的 BDUSS（网页 API），否则回退到 OAuth（xpan API）。
struct BaiduDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* XPAN_BASE = "https://pan.baidu.com/rest/2.0/xpan";
    static constexpr const char* WEB_BASE = "https://pan.baidu.com";
    static constexpr const char* OAUTH_TOKEN = "https://openapi.baidu.com/oauth/2.0/token";

    BaiduDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        m_access_token = FindExtraCI(config.extra, {"access_token"});
        m_refresh_token = FindExtraCI(config.extra, {"refresh_token"});
        m_client_id = FindExtraCI(config.extra, {"client_id", "app_key", "appkey"});
        m_client_secret = FindExtraCI(config.extra, {"client_secret", "secret_key", "secretkey"});
        m_sign_key = FindExtraCI(config.extra, {"sign_key", "signkey"});
        m_bduss = ExtractBduss(FindExtraCI(config.extra, {"cookie", "bduss"}));
        m_bdstoken = FindExtraCI(config.extra, {"bdstoken"});
    }

    bool auth() override {
        if (!m_access_token.empty() || !m_bduss.empty()) {
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

    bool use_web() const {
        return !m_bduss.empty();
    }

    // 网页 API 需要 bdstoken；从 gettemplatevariable 接口获取。
    bool ensure_bdstoken() {
        if (!m_bdstoken.empty()) {
            return true;
        }
        const std::string url = std::string(WEB_BASE) + "/api/gettemplatevariable?fields=%5B%22bdstoken%22%5D";
        const auto resp = web_get(url);
        cloud::Json j(resp);
        if (!j) {
            return false;
        }
        auto result = cloud::js_get(j.get(), "result");
        m_bdstoken = cloud::js_str(result, "bdstoken", "");
        return !m_bdstoken.empty();
    }

    int list_dir(const std::string& path, std::vector<cloud::CloudEntry>& out) override {
        out.clear();
        if (!auth()) {
            return -EIO;
        }
        return use_web() ? list_dir_web(path, out) : list_dir_xpan(path, out);
    }

    int list_dir_xpan(const std::string& path, std::vector<cloud::CloudEntry>& out) {
        const std::string remote_dir = path.empty() ? "/" : path;
        std::string url = std::string(XPAN_BASE) + "/file?method=list";
        url += "&dir=" + url_encode(remote_dir);
        url += "&order=name&desc=0&limit=1000&web=1";
        url += "&access_token=" + url_encode(m_access_token);

        const auto resp = xpan_get(url);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }

        return parse_xpan_list(j.get(), out);
    }

    int list_dir_web(const std::string& path, std::vector<cloud::CloudEntry>& out) {
        if (!ensure_bdstoken()) {
            return -EIO;
        }
        const std::string remote_dir = path.empty() ? "/" : path;
        std::string url = std::string(WEB_BASE) + "/api/list";
        url += "?dir=" + url_encode(remote_dir);
        url += "&order=time&desc=1&num=1000&page=1&showempty=0&web=1";
        url += "&bdstoken=" + url_encode(m_bdstoken);

        const auto resp = web_get(url);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
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

    int parse_xpan_list(yyjson_val* root, std::vector<cloud::CloudEntry>& out) {
        auto list = cloud::js_get(root, "list");
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
        if (!auth()) {
            return -EIO;
        }
        const int rc = use_web() ? resolve_download_url_web(e, url) : resolve_download_url_xpan(e, url);
        if (rc < 0) {
            return rc;
        }
        // 百度下载直链需要 User-Agent: pan.baidu.com（否则 >20M 文件报错）。
        headers = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        return 0;
    }

    int resolve_download_url_xpan(const cloud::CloudEntry& e, std::string& url) {
        std::string req = std::string(XPAN_BASE) + "/multimedia?method=filemetas";
        req += "&fsids=[" + url_encode(e.extra) + "]";
        req += "&dlink=1";
        req += "&access_token=" + url_encode(m_access_token);

        const auto resp = xpan_get(req);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }
        auto list = cloud::js_get(j.get(), "list");
        if (!list || !yyjson_is_arr(list) || yyjson_arr_size(list) == 0) {
            return -EIO;
        }
        std::string dlink = cloud::js_str(yyjson_arr_get_first(list), "dlink", "");
        if (dlink.empty()) {
            return -EIO;
        }
        dlink += (dlink.find('?') == std::string::npos ? '?' : '&');
        dlink += "access_token=" + url_encode(m_access_token);
        url = std::move(dlink);
        return 0;
    }

    int resolve_download_url_web(const cloud::CloudEntry& e, std::string& url) {
        if (!ensure_bdstoken()) {
            return -EIO;
        }
        const std::string fidlist = "[" + e.extra + "]";
        const std::string ts = std::to_string(time(nullptr));

        // 网页 API 下载签名。
        const std::string to_sign = "fidlist=" + fidlist + "&time=" + ts + "&sign_key=" + m_sign_key + "&type=dlink&web=1";
        const std::string sign = md5_hex(to_sign);
        if (sign.empty()) {
            return -EIO;
        }

        std::string req = std::string(WEB_BASE) + "/api/download";
        req += "?sign=" + url_encode(sign);
        req += "&timestamp=" + ts;
        req += "&fidlist=" + url_encode(fidlist);
        req += "&type=dlink&vip=2&channel=chunlei&clienttype=0&web=1&app_id=250528";
        req += "&bdstoken=" + url_encode(m_bdstoken);

        const auto resp = web_get(req);
        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "errno", -1) != 0) {
            return -EIO;
        }
        std::string dlink = cloud::js_str(j.get(), "dlink", "");
        if (dlink.empty()) {
            return -EIO;
        }
        url = std::move(dlink);
        return 0;
    }

    int make_dir(const std::string& path) override {
        if (!auth()) {
            return -EIO;
        }
        if (use_web()) {
            if (!ensure_bdstoken()) {
                return -EIO;
            }
            const std::string body = "path=" + url_encode(path.empty() ? "/" : path) + "&isdir=1&block_list=%5B%5D&bdstoken=" + url_encode(m_bdstoken);
            const auto resp = web_post(std::string(WEB_BASE) + "/api/create?a=commit", body);
            cloud::Json j(resp);
            return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
        }
        const std::string body = "path=" + url_encode(path.empty() ? "/" : path) + "&isdir=1&rtype=1";
        const auto resp = xpan_post(std::string(XPAN_BASE) + "/file?method=create", body);
        cloud::Json j(resp);
        return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
    }

    int remove_entry(const std::string& path, bool is_dir) override {
        (void)is_dir;
        if (!auth()) {
            return -EIO;
        }
        const std::string filelist = "[\"" + path + "\"]";
        if (use_web()) {
            if (!ensure_bdstoken()) {
                return -EIO;
            }
            const std::string body = "filelist=" + url_encode(filelist) + "&bdstoken=" + url_encode(m_bdstoken);
            const auto resp = web_post(std::string(WEB_BASE) + "/api/filemanager?opera=delete&async=0&onnest=fail", body);
            cloud::Json j(resp);
            return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
        }
        const std::string body = "async=0&ondup=overwrite&filelist=" + url_encode(filelist);
        const auto resp = xpan_post(std::string(XPAN_BASE) + "/file?method=filemanager&opera=delete", body);
        cloud::Json j(resp);
        return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        if (!auth()) {
            return -EIO;
        }
        const auto slash = new_path.find_last_of('/');
        const std::string new_name = slash == std::string::npos ? new_path : new_path.substr(slash + 1);
        const std::string filelist = "[{\"path\":\"" + old_path + "\",\"newname\":\"" + new_name + "\"}]";
        if (use_web()) {
            if (!ensure_bdstoken()) {
                return -EIO;
            }
            const std::string body = "filelist=" + url_encode(filelist) + "&bdstoken=" + url_encode(m_bdstoken);
            const auto resp = web_post(std::string(WEB_BASE) + "/api/filemanager?opera=rename&async=0&onnest=fail", body);
            cloud::Json j(resp);
            return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
        }
        const std::string body = "async=0&ondup=overwrite&filelist=" + url_encode(filelist);
        const auto resp = xpan_post(std::string(XPAN_BASE) + "/file?method=filemanager&opera=rename", body);
        cloud::Json j(resp);
        return (!j || cloud::js_int(j.get(), "errno", -1) != 0) ? -EIO : 0;
    }

private:
    std::string xpan_get(const std::string& url) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_get(url, h);
    }

    std::string xpan_post(const std::string& url, const std::string& body) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_post(url, body, h);
    }

    std::string web_get(const std::string& url) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        if (!m_bduss.empty()) {
            h = curl_slist_append(h, ("Cookie: BDUSS=" + m_bduss).c_str());
        }
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_get(url, h);
    }

    std::string web_post(const std::string& url, const std::string& body) {
        curl_slist* h = curl_slist_append(nullptr, "User-Agent: pan.baidu.com");
        if (!m_bduss.empty()) {
            h = curl_slist_append(h, ("Cookie: BDUSS=" + m_bduss).c_str());
        }
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        return http_post(url, body, h);
    }

    std::string m_access_token{};
    std::string m_refresh_token{};
    std::string m_client_id{};
    std::string m_client_secret{};
    std::string m_sign_key{};
    std::string m_bduss{};
    std::string m_bdstoken{};
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
