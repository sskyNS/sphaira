#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "swkbd.hpp"
#include "fs.hpp"

#include "utils/devoptab_common.hpp"

#include "ui/menus/cloud_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/nvg_util.hpp"

#include <minIni.h>

namespace sphaira::ui::menu::cloud {
namespace {

const Provider PROVIDERS[] = {
    { "百度网盘",       "/config/sphaira/mount/baidu.ini",       "BAIDU",       "refresh_token", "粘贴百度网盘 refresh_token" },
    { "谷歌网盘",       "/config/sphaira/mount/googledrive.ini", "GOOGLEDRIVE", "refresh_token", "粘贴 Google Drive refresh_token" },
    { "夸克网盘",       "/config/sphaira/mount/quark.ini",       "QUARK",       "cookie",        "粘贴夸克网盘 cookie" },
    { "阿里云盘",       "/config/sphaira/mount/aliyun.ini",      "ALIYUN",      "refresh_token", "粘贴阿里云盘 refresh_token" },
    { "光鸭云盘",       "/config/sphaira/mount/guangya.ini",     "GUANGYA",     "access_token",  "粘贴光鸭云盘 access_token" },
    { "风灵月影服务器", "/config/sphaira/mount/fengling.ini",    "FENGLING",    "",              "设备身份自动鉴权，无需配置" },
};

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"网盘浏览器", flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"返回", [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"登录 / 粘贴凭证", [this](){ Login(); }}),
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

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

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
