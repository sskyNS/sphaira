#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "swkbd.hpp"
#include "fs.hpp"

#include "utils/devoptab_common.hpp"
#include "utils/cloud_disk.hpp"

#include "ui/menus/cloud_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/nvg_util.hpp"

#include <minIni.h>
#include <chrono>
#include <cstring>

namespace sphaira::ui::menu::cloud {

// 避免与本菜单命名空间 sphaira::ui::menu::cloud 重名。
namespace devcloud = sphaira::devoptab::cloud;

namespace {

const Provider PROVIDERS[] = {
    { "百度网盘",       "/config/sphaira/mount/baidu.ini",       "BAIDU",       "refresh_token", "粘贴百度网盘 refresh_token" },
    { "谷歌网盘",       "/config/sphaira/mount/googledrive.ini", "GOOGLEDRIVE", "refresh_token", "粘贴 Google Drive refresh_token" },
    { "夸克网盘",       "/config/sphaira/mount/quark.ini",       "QUARK",       "cookie",        "粘贴夸克网盘 cookie" },
    { "阿里云盘",       "/config/sphaira/mount/aliyun.ini",      "ALIYUN",      "refresh_token", "粘贴阿里云盘 refresh_token" },
    { "光鸭云盘",       "/config/sphaira/mount/guangya.ini",     "GUANGYA",     "access_token",  "粘贴光鸭云盘 access_token" },
    { "风灵月影服务器", "/config/sphaira/mount/fengling.ini",    "FENGLING",    "",              "设备身份自动鉴权，无需配置" },
};

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

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"网盘浏览器", flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"返回", [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"登录 / 粘贴凭证", [this](){ Login(); }}),
        std::make_pair(Button::Y, Action{"扫码登录", [this](){ StartDeviceCodeLogin(); }}),
        std::make_pair(Button::X, Action{"打开文件浏览器", [](){ App::Push<filebrowser::Menu>(MenuFlag_None); }})
    );

    for (const auto& p : PROVIDERS) {
        m_entries.push_back(p);
    }

    RefreshStatus();
    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_List);
}

Menu::~Menu() = default;

void Menu::RefreshStatus() {
    m_authed.clear();

    for (const auto& p : m_entries) {
        bool authed = false;

        if (!p.auth_key || !*p.auth_key) {
            // 风灵月影：设备身份自动鉴权。
            authed = true;
        } else {
            common::MountConfigs configs;
            common::LoadConfigsFromIni(fs::FsPath{p.ini_path}, configs);

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

void Menu::StartDeviceCodeLogin() {
    if (m_login_active) {
        return;
    }

    if (m_index >= m_entries.size() || std::strcmp(m_entries[m_index].section, "GUANGYA") != 0) {
        App::Notify("当前网盘暂不支持扫码登录，请用粘贴凭证");
        return;
    }

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

    m_login_active = true;
    m_login_next_poll_ms = NowMs() + 2000;

    // 把登录链接显示在副标题（滚动文本），并提示用户在手机上打开。
    this->SetSubHeading(m_login_url);
    App::Notify("请在手机浏览器打开上方链接完成登录");
}

void Menu::PollDeviceCodeLogin() {
    if (!m_login_active) {
        return;
    }

    if (NowMs() < m_login_next_poll_ms) {
        return;
    }
    m_login_next_poll_ms = NowMs() + 2000;

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
        // 尚未授权或授权中，继续轮询。
        return;
    }

    fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira/mount");
    ini_puts("GUANGYA", "url", "https://example.com", "/config/sphaira/mount/guangya.ini");
    ini_puts("GUANGYA", "access_token", access.c_str(), "/config/sphaira/mount/guangya.ini");
    ini_puts("GUANGYA", "refresh_token", refresh.c_str(), "/config/sphaira/mount/guangya.ini");

    m_login_active = false;
    this->SetSubHeading("");
    RefreshStatus();
    App::Notify("光鸭登录成功，重启后生效");
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

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, auto v, auto pos) {
        const auto& p = m_entries[pos];
        const bool selected = pos == m_index;

        const char* status = (pos < m_authed.size() && m_authed[pos]) ? "已配置" : "未配置";
        if (!p.auth_key || !*p.auth_key) {
            status = "自动";
        }

        DrawEntryNoImage(vg, theme, grid::LayoutType_List, v, selected, p.name, "", status);
    });
}

} // namespace sphaira::ui::menu::cloud
