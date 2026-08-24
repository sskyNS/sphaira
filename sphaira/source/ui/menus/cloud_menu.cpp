#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "swkbd.hpp"
#include "fs.hpp"
#include "download.hpp"

#include "utils/devoptab_common.hpp"
#include "utils/cloud_disk.hpp"
#include "utils/qr.hpp"
#include "location.hpp"

#include "ui/menus/cloud_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/nvg_util.hpp"

#include <minIni.h>
#include <chrono>
#include <cstring>

namespace sphaira::ui::menu::cloud {

// 避免与本菜单命名空间 sphaira::ui::menu::cloud 重名。
namespace devcloud = sphaira::devoptab::cloud;
namespace devcommon = sphaira::devoptab::common;

namespace {

const Provider PROVIDERS[] = {
    { "百度网盘",       "/config/sphaira/mount/baidu.ini",       "BAIDU",       "refresh_token", "粘贴百度网盘 refresh_token" },
    { "谷歌网盘",       "/config/sphaira/mount/googledrive.ini", "GOOGLEDRIVE", "refresh_token", "粘贴 Google Drive refresh_token" },
    { "夸克网盘",       "/config/sphaira/mount/quark.ini",       "QUARK",       "cookie",        "粘贴夸克网盘 cookie" },
    { "阿里云盘",       "/config/sphaira/mount/aliyun.ini",      "ALIYUN",      "refresh_token", "粘贴阿里云盘 refresh_token" },
    { "光鸭云盘",       "/config/sphaira/mount/guangya.ini",     "GUANGYA",     "access_token",  "粘贴光鸭云盘 access_token" },
    { "风灵月影服务器", "/config/sphaira/mount/fengling.ini",    "FENGLING",    "",              "设备身份自动鉴权，无需配置" },
};

constexpr const u8 LOGO_BAIDU[]{
    #embed <icons/cloud_baidu.png>
};

constexpr const u8 LOGO_GOOGLE[]{
    #embed <icons/cloud_google.png>
};

constexpr const u8 LOGO_QUARK[]{
    #embed <icons/cloud_quark.png>
};

constexpr const u8 LOGO_ALIYUN[]{
    #embed <icons/cloud_aliyun.png>
};

constexpr const u8 LOGO_GUANGYA[]{
    #embed <icons/cloud_guangya.png>
};

constexpr const u8 LOGO_FENGLING[]{
    #embed <icons/cloud_fengling.png>
};

// 按 section 名加载对应网盘的 logo，返回 nanovg 图像句柄；失败返回 0。
int LoadLogoForSection(const char* section, NVGcontext* vg) {
    if (!std::strcmp(section, "BAIDU")) return nvgCreateImageMem(vg, 0, LOGO_BAIDU, sizeof(LOGO_BAIDU));
    if (!std::strcmp(section, "GOOGLEDRIVE")) return nvgCreateImageMem(vg, 0, LOGO_GOOGLE, sizeof(LOGO_GOOGLE));
    if (!std::strcmp(section, "QUARK")) return nvgCreateImageMem(vg, 0, LOGO_QUARK, sizeof(LOGO_QUARK));
    if (!std::strcmp(section, "ALIYUN")) return nvgCreateImageMem(vg, 0, LOGO_ALIYUN, sizeof(LOGO_ALIYUN));
    if (!std::strcmp(section, "GUANGYA")) return nvgCreateImageMem(vg, 0, LOGO_GUANGYA, sizeof(LOGO_GUANGYA));
    if (!std::strcmp(section, "FENGLING")) return nvgCreateImageMem(vg, 0, LOGO_FENGLING, sizeof(LOGO_FENGLING));
    return 0;
}

u64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string HttpPostJson(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }

    std::string resp;
    curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
        auto* out = static_cast<std::string*>(u);
        out->append(p, s * n);
        return s * n;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return resp;
}

size_t WriteCb(char* p, size_t s, size_t n, void* u) {
    auto* out = static_cast<std::string*>(u);
    out->append(p, s * n);
    return s * n;
}

std::string UrlEncode(const std::string& s) {
    return sphaira::curl::EscapeString(s);
}

std::string Base64Decode(const std::string& encoded) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') {
            break;
        }
        const size_t idx = chars.find(c);
        if (idx == std::string::npos) {
            continue;
        }
        val = (val << 6) + (int)idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back((char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string HttpGetCookies(const std::string& url, const std::string& cookie_file) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    std::string resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return resp;
}

std::string HttpPostFormCookies(const std::string& url, const std::string& form, const std::string& cookie_file) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    std::string resp;
    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)form.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file.c_str());
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return resp;
}

std::string HttpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    std::string resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return resp;
}

std::string HttpPostForm(const std::string& url, const std::string& form) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    std::string resp;
    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)form.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return resp;
}

std::string ReadIniKey(const char* ini_path, const char* key) {
    devcommon::MountConfigs configs;
    devcommon::LoadConfigsFromIni(fs::FsPath{ini_path}, configs);
    for (const auto& c : configs) {
        const auto it = c.extra.find(key);
        if (it != c.extra.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return {};
}

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"网盘浏览器", flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"返回", [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"登录 / 粘贴凭证", [this](){ Login(); }}),
        std::make_pair(Button::Y, Action{"扫码登录", [this](){ StartDeviceCodeLogin(); }}),
        std::make_pair(Button::X, Action{"打开网盘", [this](){ OpenFileBrowser(); }})
    );

    for (const auto& p : PROVIDERS) {
        m_entries.push_back(p);
    }

    RefreshStatus();
    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_List);
}

Menu::~Menu() {
    if (m_qr_image) {
        nvgDeleteImage(App::GetVg(), m_qr_image);
    }
    for (const auto image : m_logo_images) {
        if (image) {
            nvgDeleteImage(App::GetVg(), image);
        }
    }
}

void Menu::ClearQr() {
    if (m_qr_image) {
        nvgDeleteImage(App::GetVg(), m_qr_image);
        m_qr_image = 0;
    }
    m_qr_rgba.clear();
    m_qr_size = 0;
}

void Menu::RefreshStatus() {
    m_authed.clear();

    for (const auto& p : m_entries) {
        bool authed = false;

        if (!p.auth_key || !*p.auth_key) {
            // 风灵月影：设备身份自动鉴权。
            authed = true;
        } else {
            devcommon::MountConfigs configs;
            devcommon::LoadConfigsFromIni(fs::FsPath{p.ini_path}, configs);

            for (const auto& c : configs) {
                const auto it = c.extra.find(p.auth_key);
                if (it != c.extra.end() && !it->second.empty()) {
                    authed = true;
                    break;
                }
            }
        }

        m_authed.push_back(authed);
    }
}

void Menu::Login() {
    const auto& p = m_entries[m_index];

    if (!p.auth_key || !*p.auth_key) {
        App::Notify("风灵月影无需配置，设备身份自动鉴权");
        return;
    }

    std::string out;
    if (R_FAILED(swkbd::ShowText(out, p.name, p.guide, nullptr, 1, 4096))) {
        return;
    }

    if (out.empty()) {
        return;
    }

    fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
    ini_puts(p.section, "url", "https://example.com", p.ini_path);
    ini_puts(p.section, p.auth_key, out.c_str(), p.ini_path);

    RefreshStatus();
    App::Notify("已保存，重启 Sphaira 后生效");
}

void Menu::OpenFileBrowser() {
    if (m_index >= m_entries.size()) {
        return;
    }

    const auto& p = m_entries[m_index];

    // 挂载点命名规则：`[SECTION] name:/`（见 devoptab_common.cpp MountNetworkDevice）。
    const std::string prefix = "[" + std::string(p.section) + "] ";

    for (const auto& e : location::GetStdio(false)) {
        if (!e.mount.starts_with(prefix)) {
            continue;
        }

        const fs::FsPath mount{e.mount};
        auto fs = std::make_shared<fs::FsStdio>(true, mount);

        const filebrowser::FsEntry fs_entry{
            .name = fs::FsPath{e.name},
            .root = mount,
            .type = filebrowser::FsType::Stdio,
            .flags = e.flags,
        };

        const auto options = filebrowser::FsOption_All & ~filebrowser::FsOption_LoadAssoc;
        App::Push<filebrowser::Menu>(fs, fs_entry, fs->Root(), options);
        return;
    }

    App::Notify("未挂载，请先登录配置并重启 Sphaira");
}

void Menu::StartDeviceCodeLogin() {
    if (m_login_active) {
        return;
    }

    if (m_index >= m_entries.size()) {
        return;
    }

    const std::string sec = m_entries[m_index].section;

    if (sec == "GUANGYA") {
        const std::string body = "{\"client_id\":\"aMe-8VSlkrbQXpUR\",\"scope\":\"user\"}";
        const auto resp = HttpPostJson("https://account.guangyapan.com/v1/auth/device/code", body);

        devcloud::Json j(resp);
        if (!j) {
            App::Notify("生成登录码失败");
            return;
        }

        m_login_code = devcloud::js_str(j.get(), "device_code", "");
        m_login_url = devcloud::js_str(j.get(), "verification_uri_complete", "");

        if (m_login_code.empty() || m_login_url.empty()) {
            App::Notify("生成登录码失败");
            return;
        }
        m_login_type = "guangya";
    } else if (sec == "ALIYUN") {
        fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
        m_qr_cookie_file = "/config/sphaira/mount/aliyun_qr_cookie.txt";

        // 1. 建立会话（拿 SESSIONID cookie）。
        HttpGetCookies(
            "https://auth.aliyundrive.com/v2/oauth/authorize?login_type=custom&response_type=code&redirect_uri=https%3A%2F%2Fwww.aliyundrive.com%2Fsign%2Fcallback&client_id=25dzX3vbYqktVxyX&state=%7B%22origin%22%3A%22file%3A%2F%2F%22%7D",
            m_qr_cookie_file);

        // 2. 生成二维码。
        m_qr_data_raw = HttpGetCookies(
            "https://passport.aliyundrive.com/newlogin/qrcode/generate.do?appName=aliyun_drive",
            m_qr_cookie_file);

        devcloud::Json gj(m_qr_data_raw);
        auto* gdata = devcloud::js_get(devcloud::js_get(gj.get(), "content"), "data");
        m_login_url = devcloud::js_str(gdata, "codeContent", "");

        if (m_login_url.empty()) {
            App::Notify("生成二维码失败");
            return;
        }
        m_login_type = "aliyun";
    } else if (sec == "GOOGLEDRIVE") {
        m_oauth_client_id = ReadIniKey("/config/sphaira/mount/googledrive.ini", "client_id");
        m_oauth_client_secret = ReadIniKey("/config/sphaira/mount/googledrive.ini", "client_secret");
        if (m_oauth_client_id.empty() || m_oauth_client_secret.empty()) {
            App::Notify("请先在挂载表单填写 Client ID/Secret");
            return;
        }

        const std::string form = "client_id=" + UrlEncode(m_oauth_client_id) +
            "&scope=" + UrlEncode("https://www.googleapis.com/auth/drive");
        const auto resp = HttpPostForm("https://oauth2.googleapis.com/device/code", form);
        devcloud::Json j(resp);
        if (!j) {
            App::Notify("生成登录码失败");
            return;
        }

        m_login_code = devcloud::js_str(j.get(), "device_code", "");
        m_login_user_code = devcloud::js_str(j.get(), "user_code", "");
        m_login_url = devcloud::js_str(j.get(), "verification_url", "");
        if (m_login_code.empty() || m_login_user_code.empty() || m_login_url.empty()) {
            App::Notify("生成登录码失败");
            return;
        }
        m_login_type = "google";
    } else if (sec == "BAIDU") {
        m_oauth_client_id = ReadIniKey("/config/sphaira/mount/baidu.ini", "client_id");
        m_oauth_client_secret = ReadIniKey("/config/sphaira/mount/baidu.ini", "client_secret");
        if (m_oauth_client_id.empty() || m_oauth_client_secret.empty()) {
            App::Notify("请先在挂载表单填写 Client ID/Secret");
            return;
        }

        const std::string url = "https://openapi.baidu.com/oauth/2.0/device/code?response_type=device_code&client_id=" +
            UrlEncode(m_oauth_client_id) + "&scope=" + UrlEncode("basic,netdisk");
        const auto resp = HttpGet(url);
        devcloud::Json j(resp);
        if (!j) {
            App::Notify("生成登录码失败");
            return;
        }

        m_login_code = devcloud::js_str(j.get(), "device_code", "");
        m_login_user_code = devcloud::js_str(j.get(), "user_code", "");
        m_login_url = devcloud::js_str(j.get(), "qrcode_url", "");
        if (m_login_url.empty()) {
            m_login_url = devcloud::js_str(j.get(), "verification_url", "");
        }
        if (m_login_code.empty() || m_login_user_code.empty() || m_login_url.empty()) {
            App::Notify("生成登录码失败");
            return;
        }
        m_login_type = "baidu";
    } else {
        App::Notify("当前网盘暂不支持扫码登录，请用粘贴凭证");
        return;
    }

    m_login_active = true;
    m_login_next_poll_ms = NowMs() + 2000;

    // 生成二维码；失败则退回显示文本链接。
    ClearQr();
    if (qr::Generate(m_login_url, m_qr_rgba, m_qr_size, 6)) {
        this->SetSubHeading("请用手机扫描二维码完成登录");
    } else {
        this->SetSubHeading(m_login_url);
    }
    App::Notify("请用手机扫码登录");
}

void Menu::PollDeviceCodeLogin() {
    if (!m_login_active) {
        return;
    }

    if (NowMs() < m_login_next_poll_ms) {
        return;
    }
    m_login_next_poll_ms = NowMs() + 2000;

    if (m_login_type == "aliyun") {
        PollAliyunLogin();
        return;
    }

    if (m_login_type == "google") {
        const std::string form = "client_id=" + UrlEncode(m_oauth_client_id) +
            "&client_secret=" + UrlEncode(m_oauth_client_secret) +
            "&code=" + UrlEncode(m_login_code) +
            "&grant_type=" + UrlEncode("urn:ietf:params:oauth:grant-type:device_code");
        const auto resp = HttpPostForm("https://oauth2.googleapis.com/token", form);
        devcloud::Json j(resp);
        if (!j) {
            return;
        }
        const std::string access = devcloud::js_str(j.get(), "access_token", "");
        const std::string refresh = devcloud::js_str(j.get(), "refresh_token", "");
        const std::string err = devcloud::js_str(j.get(), "error", "");
        if (!access.empty()) {
            fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
            ini_puts("GOOGLEDRIVE", "url", "https://example.com", "/config/sphaira/mount/googledrive.ini");
            ini_puts("GOOGLEDRIVE", "access_token", access.c_str(), "/config/sphaira/mount/googledrive.ini");
            ini_puts("GOOGLEDRIVE", "refresh_token", refresh.c_str(), "/config/sphaira/mount/googledrive.ini");
            m_login_active = false;
            ClearQr();
            this->SetSubHeading("");
            RefreshStatus();
            App::Notify("谷歌网盘登录成功，重启后生效");
        } else if (err == "expired_token" || err == "access_denied") {
            m_login_active = false;
            ClearQr();
            this->SetSubHeading("");
            App::Notify("登录码已过期，请重试");
        }
        // authorization_pending / slow_down：继续轮询。
        return;
    }

    if (m_login_type == "baidu") {
        const std::string url = "https://openapi.baidu.com/oauth/2.0/token?grant_type=device_token&code=" +
            UrlEncode(m_login_code) + "&client_id=" + UrlEncode(m_oauth_client_id) +
            "&client_secret=" + UrlEncode(m_oauth_client_secret);
        const auto resp = HttpGet(url);
        devcloud::Json j(resp);
        if (!j) {
            return;
        }
        const std::string access = devcloud::js_str(j.get(), "access_token", "");
        const std::string refresh = devcloud::js_str(j.get(), "refresh_token", "");
        const std::string err = devcloud::js_str(j.get(), "error", "");
        if (!access.empty()) {
            fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
            ini_puts("BAIDU", "url", "https://example.com", "/config/sphaira/mount/baidu.ini");
            ini_puts("BAIDU", "access_token", access.c_str(), "/config/sphaira/mount/baidu.ini");
            ini_puts("BAIDU", "refresh_token", refresh.c_str(), "/config/sphaira/mount/baidu.ini");
            m_login_active = false;
            ClearQr();
            this->SetSubHeading("");
            RefreshStatus();
            App::Notify("百度网盘登录成功，重启后生效");
        } else if (err == "expired_token" || err == "authorization_expired" || err == "access_denied") {
            m_login_active = false;
            ClearQr();
            this->SetSubHeading("");
            App::Notify("登录码已过期，请重试");
        }
        // authorization_pending：继续轮询。
        return;
    }

    // 光鸭设备码轮询。
    const std::string body =
        "{\"client_id\":\"aMe-8VSlkrbQXpUR\","
        "\"grant_type\":\"urn:ietf:params:oauth:grant-type:device_code\","
        "\"device_code\":\"" + m_login_code + "\"}";

    const auto resp = HttpPostJson("https://account.guangyapan.com/v1/auth/token", body);
    devcloud::Json j(resp);
    if (!j) {
        return;
    }

    const std::string access = devcloud::js_str(j.get(), "access_token", "");
    const std::string refresh = devcloud::js_str(j.get(), "refresh_token", "");

    if (access.empty()) {
        return;
    }

    fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
    ini_puts("GUANGYA", "url", "https://example.com", "/config/sphaira/mount/guangya.ini");
    ini_puts("GUANGYA", "access_token", access.c_str(), "/config/sphaira/mount/guangya.ini");
    ini_puts("GUANGYA", "refresh_token", refresh.c_str(), "/config/sphaira/mount/guangya.ini");

    m_login_active = false;
    ClearQr();
    this->SetSubHeading("");
    RefreshStatus();
    App::Notify("光鸭登录成功，重启后生效");
}

void Menu::PollAliyunLogin() {
    // 从 generate.do 的 content.data 构造 form 数据。
    devcloud::Json gj(m_qr_data_raw);
    auto* data = devcloud::js_get(devcloud::js_get(gj.get(), "content"), "data");

    std::string form;
    if (data && yyjson_is_obj(data)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(data, &iter);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);
            const char* k = yyjson_get_str(key);
            if (!k) {
                continue;
            }
            std::string v;
            if (yyjson_is_str(val)) {
                v = yyjson_get_str(val);
            } else if (yyjson_is_uint(val)) {
                v = std::to_string(yyjson_get_uint(val));
            } else if (yyjson_is_sint(val)) {
                v = std::to_string(yyjson_get_sint(val));
            } else if (yyjson_is_bool(val)) {
                v = yyjson_get_bool(val) ? "true" : "false";
            }
            if (!form.empty()) {
                form += "&";
            }
            form += std::string(k) + "=" + UrlEncode(v);
        }
    }

    const auto resp = HttpPostFormCookies(
        "https://passport.aliyundrive.com/newlogin/qrcode/query.do?appName=aliyun_drive",
        form, m_qr_cookie_file);

    devcloud::Json rj(resp);
    auto* d = devcloud::js_get(devcloud::js_get(rj.get(), "content"), "data");
    const std::string status = devcloud::js_str(d, "qrCodeStatus", "");
    const std::string biz = devcloud::js_str(d, "bizExt", "");

    if (status == "EXPIRED" || status == "CANCEL") {
        m_login_active = false;
        ClearQr();
        this->SetSubHeading("");
        App::Notify("登录码已过期，请重试");
        return;
    }

    // CONFIRMED 时会带 bizExt（内含 refreshToken）。
    std::string rt;
    if (!biz.empty()) {
        const std::string decoded = Base64Decode(biz);
        const size_t pos = decoded.find("\"refreshToken\"");
        if (pos != std::string::npos) {
            const size_t colon = decoded.find(':', pos);
            const size_t q1 = decoded.find('"', colon + 1);
            const size_t q2 = decoded.find('"', q1 + 1);
            if (colon != std::string::npos && q1 != std::string::npos && q2 != std::string::npos) {
                rt = decoded.substr(q1 + 1, q2 - q1 - 1);
            }
        }
    }

    if (rt.empty()) {
        // NEW / SCANED：继续轮询。
        return;
    }

    fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
    ini_puts("ALIYUN", "url", "https://example.com", "/config/sphaira/mount/aliyun.ini");
    ini_puts("ALIYUN", "refresh_token", rt.c_str(), "/config/sphaira/mount/aliyun.ini");

    m_login_active = false;
    ClearQr();
    this->SetSubHeading("");
    RefreshStatus();
    App::Notify("阿里云盘登录成功，重启后生效");
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    PollDeviceCodeLogin();

    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (!(touch && m_index == i)) {
            m_index = i;
        }
    });
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_login_active) {
        // 懒加载创建 nanovg 图像。
        if (!m_qr_image && !m_qr_rgba.empty()) {
            m_qr_image = nvgCreateImageRGBA(vg, m_qr_size, m_qr_size, NVG_IMAGE_NEAREST, m_qr_rgba.data());
        }

        if (m_qr_image) {
            const float s1 = GetW() - 240.f;
            const float s2 = GetH() - 240.f;
            const float size = s1 < s2 ? s1 : s2;
            const float x = GetX() + (GetW() - size) / 2.f;
            const float y = GetY() + (GetH() - size) / 2.f + 20.f;
            gfx::drawImage(vg, x, y, size, size, m_qr_image);
        }

        if (!m_login_user_code.empty()) {
            gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() - 80.f, 30.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "登录码：%s", m_login_user_code.c_str());
            gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() - 36.f, 20.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "请用手机打开链接并输入上面的登录码");
        } else {
            gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() - 70.f, 24.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "请用手机扫描二维码登录，等待授权...");
        }
        return;
    }

    if (m_logo_images.size() != m_entries.size()) {
        m_logo_images.resize(m_entries.size(), 0);
    }

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, auto v, auto pos) {
        const auto& p = m_entries[pos];
        const bool selected = pos == m_index;

        const char* status = (pos < m_authed.size() && m_authed[pos]) ? "已配置" : "未配置";
        if (!p.auth_key || !*p.auth_key) {
            status = "自动";
        }

        int image = 0;
        if (pos < m_logo_images.size()) {
            if (!m_logo_images[pos]) {
                m_logo_images[pos] = LoadLogoForSection(p.section, vg);
            }
            image = m_logo_images[pos];
        }

        DrawEntry(vg, theme, grid::LayoutType_List, v, selected, image, p.name, "", status);
    });
}

} // namespace sphaira::ui::menu::cloud
