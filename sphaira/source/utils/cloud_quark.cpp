#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

#include <chrono>
#include <thread>

namespace sphaira::devoptab {
namespace {

// 夸克网盘 quark cloud drive API。
// 鉴权：浏览器 Cookie。
struct QuarkDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* API_BASE = "https://drive.quark.cn/1/clouddrive";

    QuarkDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        m_cookie = config_extra("cookie");
    }

    bool auth() override {
        return !m_cookie.empty();
    }

    int list_dir(const std::string& path, std::vector<cloud::CloudEntry>& out) override {
        out.clear();
        if (!auth()) {
            return -EIO;
        }

        const std::string pid = parent_id(path).empty() ? "0" : parent_id(path);

        std::string url = std::string(API_BASE) + "/file/sort?pr=ucpro&fr=pc&uc_param_str=";
        url += "&pdir_fid=" + url_encode(pid);
        url += "&_page=1&_size=100&_fetch_total=0&_sort=file_type:asc,updated_at:desc";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_get(url, h);

        cloud::Json j(resp);
        if (!j || cloud::js_int(j.get(), "code", -1) != 0) {
            return -EIO;
        }

        auto data = cloud::js_get(j.get(), "data");
        auto list = cloud::js_get(data, "list");
        if (!list || !yyjson_is_arr(list)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(list, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "file_name", "");
            e.id = cloud::js_str(item, "fid", "");
            e.size = cloud::js_u64(item, "size", 0);
            e.mtime = cloud::js_u64(item, "updated_at", 0);

            auto ft = cloud::js_get(item, "file_type");
            if (ft && yyjson_is_str(ft)) {
                e.is_dir = (std::string(yyjson_get_str(ft)) == "folder");
            } else {
                e.is_dir = cloud::js_int(item, "file_type", 1) == 0;
            }

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

        const std::string body = "{\"fids\":[\"" + e.id + "\"]}";
        const std::string req = std::string(API_BASE) + "/file/download?pr=ucpro&fr=pc";

        curl_slist* h = this->headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(req, body, h);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        auto data = cloud::js_get(j.get(), "data");
        const int code = cloud::js_int(j.get(), "code", -1);

        // 同步：data 是数组，直接取 download_url。
        if (data && yyjson_is_arr(data) && yyjson_arr_size(data) > 0) {
            auto first = yyjson_arr_get_first(data);
            url = cloud::js_str(first, "download_url", "");
            if (!url.empty()) {
                return 0;
            }
        }

        // 异步：data 含 task_id，需要轮询。
        if (data && yyjson_is_obj(data)) {
            const std::string task_id = cloud::js_str(data, "task_id", "");
            if (!task_id.empty()) {
                return poll_download_task(task_id, url);
            }
        }

        (void)code;
        return -EIO;
    }

    int make_dir(const std::string& path) override {
        if (!auth()) {
            return -EIO;
        }
        const auto slash = path.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "" : path.substr(0, slash);
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::string pid = parent_id(parent).empty() ? "0" : parent_id(parent);

        const std::string body =
            "{\"pdir_fid\":\"" + pid + "\","
            "\"file_name\":\"" + name + "\","
            "\"dir_init_lock\":false,\"dir_path\":\"\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/file?pr=ucpro&fr=pc&uc_param_str=", body, h);

        cloud::Json j(resp);
        return (j && (cloud::js_int(j.get(), "code", -1) == 0 || cloud::js_int(j.get(), "status", -1) == 200)) ? 0 : -EIO;
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

        const std::string body = "{\"action_type\":2,\"filelist\":[\"" + fid + "\"],\"exclude_fids\":[]}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/file/delete?pr=ucpro&fr=pc&uc_param_str=", body, h);

        cloud::Json j(resp);
        return (j && (cloud::js_int(j.get(), "code", -1) == 0 || cloud::js_int(j.get(), "status", -1) == 200)) ? 0 : -EIO;
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

        const std::string body = "{\"fid\":\"" + fid + "\",\"file_name\":\"" + name + "\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(API_BASE) + "/file/rename?pr=ucpro&fr=pc&uc_param_str=", body, h);

        cloud::Json j(resp);
        return (j && (cloud::js_int(j.get(), "code", -1) == 0 || cloud::js_int(j.get(), "status", -1) == 200)) ? 0 : -EIO;
    }

private:
    int poll_download_task(const std::string& task_id, std::string& url) {
        for (int i = 0; i < 30; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            const std::string req = std::string(API_BASE) + "/task?pr=ucpro&fr=pc&uc_param_str=&task_id="
                                  + url_encode(task_id) + "&retry_index=" + std::to_string(i);

            curl_slist* h = headers();
            ON_SCOPE_EXIT(curl_slist_free_all(h));
            const auto resp = http_get(req, h);

            cloud::Json j(resp);
            if (!j || cloud::js_int(j.get(), "code", -1) != 0) {
                continue;
            }

            auto data = cloud::js_get(j.get(), "data");
            if (data && yyjson_is_arr(data) && yyjson_arr_size(data) > 0) {
                url = cloud::js_str(yyjson_arr_get_first(data), "download_url", "");
                if (!url.empty()) {
                    return 0;
                }
            }
            if (data && yyjson_is_obj(data)) {
                const int status = cloud::js_int(data, "status", 0);
                if (status == 3) {
                    return -EIO;
                }
                url = cloud::js_str(data, "download_url", "");
                if (!url.empty()) {
                    return 0;
                }
            }
        }
        return -EIO;
    }

    curl_slist* headers() {
        curl_slist* h = nullptr;
        if (!m_cookie.empty()) {
            h = curl_slist_append(h, ("Cookie: " + m_cookie).c_str());
        }
        h = curl_slist_append(h, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) quark-cloud-drive/2.5.20 Chrome/100.0.4896.160 Safari/537.36");
        h = curl_slist_append(h, "Accept: application/json, text/plain, */*");
        h = curl_slist_append(h, "Content-Type: application/json");
        h = curl_slist_append(h, "Origin: https://pan.quark.cn");
        h = curl_slist_append(h, "Referer: https://pan.quark.cn/");
        return h;
    }

    std::string m_cookie{};
};

} // namespace

Result MountQuarkAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<QuarkDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "QUARK"
    );
}

} // namespace sphaira::devoptab
