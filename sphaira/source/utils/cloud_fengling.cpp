#include "utils/cloud_disk.hpp"
#include "utils/devoptab.hpp"

#include "defines.hpp"
#include "log.hpp"

#include <cstring>

namespace sphaira::devoptab {
namespace {

// 风灵月影公共服务器 (share.sskyswitch.cn)。
// 鉴权：设备身份 Header（X-Device-Serial + X-User-ID），无需 OAuth token。
struct FenglingDevice final : cloud::CloudDiskDevice {
    using cloud::CloudDiskDevice::CloudDiskDevice;

    static constexpr const char* BASE_URL = "https://share.sskyswitch.cn";

    FenglingDevice(const common::MountConfig& config) : CloudDiskDevice(config) {
        if (R_SUCCEEDED(setsysInitialize())) {
            SetSysSerialNumber serial{};
            if (R_SUCCEEDED(setsysGetSerialNumber(&serial))) {
                m_serial = serial.number;
            }

            SetSysDeviceNickName nick{};
            if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
                m_nickname = nick.nickname;
            }

            setsysExit();
        }

        if (m_nickname.empty()) {
            m_nickname = "Switch_Console";
        }
    }

    bool auth() override {
        // 通过设备身份 Header 鉴权，无需 token。
        return true;
    }

    int list_dir(const std::string& path, std::vector<cloud::CloudEntry>& out) override {
        out.clear();

        const std::string remote = path.empty() ? "." : path;
        std::string url = std::string(BASE_URL) + "/list?path=" + url_encode(remote);
        url += "&mine=true";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_get(url, h);

        cloud::Json j(resp);
        if (!j) {
            return -EIO;
        }

        yyjson_val* arr = j.get();
        if (yyjson_is_obj(arr)) {
            arr = cloud::js_get(arr, "files");
        }
        if (!arr || !yyjson_is_arr(arr)) {
            return 0;
        }

        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(arr, idx, max, item) {
            cloud::CloudEntry e;
            e.name = cloud::js_str(item, "name", "");
            e.id = cloud::js_str(item, "path", "");
            e.size = cloud::js_u64(item, "size", 0);
            e.is_dir = cloud::js_bool(item, "is_dir", false);
            if (!e.name.empty()) {
                out.push_back(std::move(e));
            }
        }
        return 0;
    }

    int resolve_download_url(const cloud::CloudEntry& e, std::string& url, curl_slist*& headers) override {
        std::string encoded = url_encode(e.id);
        size_t pos = 0;
        while ((pos = encoded.find("%2F", pos)) != std::string::npos) {
            encoded.replace(pos, 3, "/");
            pos += 1;
        }

        url = std::string(BASE_URL) + "/download/" + encoded;
        headers = this->headers(); // 所有权交给 File，close 时释放
        return 0;
    }

    int make_dir(const std::string& path) override {
        (void)path;
        // 公共服务器未提供创建目录接口。
        return -ENOTSUP;
    }

    int remove_entry(const std::string& path, bool is_dir) override {
        (void)is_dir;
        cloud::CloudEntry e;
        if (!cache_lookup(path, e) || e.id.empty()) {
            return -ENOENT;
        }

        const std::string body = "{\"path\":\"" + e.id + "\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(BASE_URL) + "/sync/delete_own", body, h);
        return resp.empty() ? -EIO : 0;
    }

    int rename_entry(const std::string& old_path, const std::string& new_path) override {
        cloud::CloudEntry e;
        if (!cache_lookup(old_path, e) || e.id.empty()) {
            return -ENOENT;
        }

        const auto slash = new_path.find_last_of('/');
        const std::string name = slash == std::string::npos ? new_path : new_path.substr(slash + 1);

        const std::string body = "{\"path\":\"" + e.id + "\",\"new_name\":\"" + name + "\"}";

        curl_slist* h = headers();
        ON_SCOPE_EXIT(curl_slist_free_all(h));
        const auto resp = http_post(std::string(BASE_URL) + "/sync/rename_own", body, h);
        return resp.empty() ? -EIO : 0;
    }

private:
    curl_slist* headers() {
        curl_slist* h = nullptr;
        if (!m_serial.empty()) {
            h = curl_slist_append(h, ("X-Device-Serial: " + m_serial).c_str());
        }
        h = curl_slist_append(h, ("X-User-ID: " + m_nickname).c_str());
        h = curl_slist_append(h, "User-Agent: Switch-Overlay-Public/1.1");
        return h;
    }

    std::string m_serial{};
    std::string m_nickname{};
};

} // namespace

Result MountFenglingAll() {
    return common::MountNetworkDevice([](const common::MountConfig& config) {
            return std::make_unique<FenglingDevice>(config);
        },
        sizeof(cloud::File), sizeof(cloud::Dir),
        "FENGLING"
    );
}

} // namespace sphaira::devoptab
