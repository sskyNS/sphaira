#include "utils/devoptab_common.hpp"
#include "utils/thread.hpp"

#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "download.hpp"
#include "i18n.hpp"

#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <minIni.h>
#include <curl/curl.h>

namespace sphaira::devoptab {
namespace {

#define MOUNT_PATH "/config/sphaira/mount/"

using namespace sphaira::ui;
using namespace sphaira::devoptab::common;

// todo: support for disabling some / all mounts.
enum class DevoptabType {
    HTTP,
    FTP,
#ifdef ENABLE_DEVOPTAB_SFTP
    SFTP,
#endif
    NFS,
    SMB,
    WEBDAV,
#ifdef ENABLE_DEVOPTAB_CLOUD
    BAIDU,
    GOOGLEDRIVE,
    QUARK,
    ALIYUN,
    GUANGYA,
    FENGLING,
#endif
};

struct TypeEntry {
    const char* name;
    const char* scheme;
    long port;
    DevoptabType type;
};

const TypeEntry TYPE_ENTRIES[] = {
    {"HTTP", "http://", 80, DevoptabType::HTTP},
    {"FTP", "ftp://", 21, DevoptabType::FTP},
#ifdef ENABLE_DEVOPTAB_SFTP
    {"SFTP", "sftp://", 22, DevoptabType::SFTP},
#endif
    {"NFS", "nfs://", 2049, DevoptabType::NFS},
    {"SMB", "smb://", 445, DevoptabType::SMB},
    {"WEBDAV", "webdav://", 80, DevoptabType::WEBDAV},
#ifdef ENABLE_DEVOPTAB_CLOUD
    {"BAIDU", "https://", 443, DevoptabType::BAIDU},
    {"GOOGLEDRIVE", "https://", 443, DevoptabType::GOOGLEDRIVE},
    {"QUARK", "https://", 443, DevoptabType::QUARK},
    {"ALIYUN", "https://", 443, DevoptabType::ALIYUN},
    {"GUANGYA", "https://", 443, DevoptabType::GUANGYA},
    {"FENGLING", "https://", 443, DevoptabType::FENGLING},
#endif
};

struct TypeConfig {
    TypeEntry type;
    MountConfig config;
};
using TypeConfigs = std::vector<TypeConfig>;

auto BuildIniPathFromType(DevoptabType type) -> fs::FsPath {
    switch (type) {
        case DevoptabType::HTTP: return MOUNT_PATH "/http.ini";
        case DevoptabType::FTP: return MOUNT_PATH "/ftp.ini";
#ifdef ENABLE_DEVOPTAB_SFTP
        case DevoptabType::SFTP: return MOUNT_PATH "/sftp.ini";
#endif
        case DevoptabType::NFS: return MOUNT_PATH "/nfs.ini";
        case DevoptabType::SMB: return MOUNT_PATH "/smb.ini";
        case DevoptabType::WEBDAV: return MOUNT_PATH "/webdav.ini";
#ifdef ENABLE_DEVOPTAB_CLOUD
        case DevoptabType::BAIDU: return MOUNT_PATH "/baidu.ini";
        case DevoptabType::GOOGLEDRIVE: return MOUNT_PATH "/googledrive.ini";
        case DevoptabType::QUARK: return MOUNT_PATH "/quark.ini";
        case DevoptabType::ALIYUN: return MOUNT_PATH "/aliyun.ini";
        case DevoptabType::GUANGYA: return MOUNT_PATH "/guangya.ini";
        case DevoptabType::FENGLING: return MOUNT_PATH "/fengling.ini";
#endif
    }

    std::unreachable();
}

auto GetTypeName(const TypeConfig& type_config) -> std::string {
    char name[128]{};
    std::snprintf(name, sizeof(name), "[%s] %s", type_config.type.name, type_config.config.name.c_str());
    return name;
}

void LoadAllConfigs(TypeConfigs& out_configs) {
    out_configs.clear();

    for (const auto& e : TYPE_ENTRIES) {
        const auto ini_path = BuildIniPathFromType(e.type);

        MountConfigs configs{};
        LoadConfigsFromIni(ini_path, configs);

        for (const auto& config : configs) {
            out_configs.emplace_back(e, config);
        }
    }
}

struct DevoptabForm final : public FormSidebar {
    // create new.
    explicit DevoptabForm();
    // modify existing.
    explicit DevoptabForm(DevoptabType type, const MountConfig& config);

private:
    void SetupButtons(bool type_change);
    void UpdateSchemeURL();

private:
    DevoptabType m_type{};
    MountConfig m_config{};

    SidebarEntryTextInput* m_name{};
    SidebarEntryTextInput* m_url{};
    SidebarEntryTextInput* m_port{};
    // SidebarEntryTextInput* m_timeout{};
    SidebarEntryTextInput* m_user{};
    SidebarEntryTextInput* m_pass{};
    SidebarEntryTextInput* m_dump_path{};

    // 云盘鉴权字段（作为 extra 键写入 ini，非云盘类型留空即可）。
    SidebarEntryTextInput* m_client_id{};
    SidebarEntryTextInput* m_client_secret{};
    SidebarEntryTextInput* m_refresh_token{};
    SidebarEntryTextInput* m_access_token{};
    SidebarEntryTextInput* m_cookie{};
    SidebarEntryTextInput* m_drive_id{};
    SidebarEntryTextInput* m_device_id{};
};

DevoptabForm::DevoptabForm(DevoptabType type, const MountConfig& config)
: FormSidebar{"Mount Creator"_i18n}
, m_type{type}
, m_config{config} {
    SetupButtons(false);
}

DevoptabForm::DevoptabForm() : FormSidebar{"Mount Creator"_i18n} {
    SetupButtons(true);
}

void DevoptabForm::UpdateSchemeURL() {
    for (const auto& e : TYPE_ENTRIES) {
        if (e.type == m_type) {
            const auto scheme_start = m_url->GetValue().find("://");
            if (scheme_start != std::string::npos) {
                m_url->SetValue(e.scheme + m_url->GetValue().substr(scheme_start + 3));
            } else if (m_url->GetValue().starts_with("://")) {
                m_url->SetValue(e.scheme + m_url->GetValue().substr(3));
            } else if (m_url->GetValue().empty()) {
                m_url->SetValue(e.scheme);
            }

            m_port->SetNumValue(e.port);
            break;
        }
    }
}

void DevoptabForm::SetupButtons(bool type_change) {
    if (type_change) {
        SidebarEntryArray::Items items;
        for (const auto& e : TYPE_ENTRIES) {
            items.emplace_back(e.name);
        }

        this->Add<SidebarEntryArray>(
            "Type"_i18n, items, [this](s64& index) {
                m_type = TYPE_ENTRIES[index].type;
                UpdateSchemeURL();
            },
            (s64)m_type,
            "Select the type of the forwarder."_i18n
        );
    }

    m_name = this->Add<SidebarEntryTextInput>(
        "Name"_i18n, m_config.name, "", "", -1, 32,
        "Set the name of the application"_i18n
    );

    m_url = this->Add<SidebarEntryTextInput>(
        "URL"_i18n, m_config.url, "", "", -1, PATH_MAX,
        "Set the URL of the application"_i18n
    );

    m_port = this->Add<SidebarEntryTextInput>(
        "Port"_i18n, m_config.port, "", "", 1, 5,
        "Optional: Set the port of the server. If left empty, the default port for the protocol will be used."_i18n
    );

    #if 0
    m_timeout = this->Add<SidebarEntryTextInput>(
        "Timeout"_i18n, m_config.timeout, "Timeout in milliseconds", 1, 5,
        "Optional: Set the timeout in seconds."_i18n
    );
    #endif

    m_user = this->Add<SidebarEntryTextInput>(
        "User"_i18n, m_config.user, "", "", -1, PATH_MAX,
        "Optional: Set the username of the application"_i18n
    );

    m_pass = this->Add<SidebarEntryTextInput>(
        "Pass"_i18n, m_config.pass, "", "", -1, PATH_MAX,
        "Optional: Set the password of the application"_i18n
    );

    m_dump_path = this->Add<SidebarEntryTextInput>(
        "Dump path"_i18n, m_config.dump_path, "", "", -1, PATH_MAX,
        "Optional: Set the dump path used when exporting games and saves."_i18n
    );

    m_client_id = this->Add<SidebarEntryTextInput>(
        "Client ID"_i18n, m_config.extra["client_id"], "", "", -1, PATH_MAX,
        "Cloud disk OAuth client_id (Baidu / Google Drive)."_i18n
    );

    m_client_secret = this->Add<SidebarEntryTextInput>(
        "Client Secret"_i18n, m_config.extra["client_secret"], "", "", -1, PATH_MAX,
        "Cloud disk OAuth client_secret (Baidu / Google Drive)."_i18n
    );

    m_refresh_token = this->Add<SidebarEntryTextInput>(
        "Refresh Token"_i18n, m_config.extra["refresh_token"], "", "", -1, PATH_MAX,
        "Cloud disk OAuth refresh_token (Baidu / Google / Aliyun / Guangya)."_i18n
    );

    m_access_token = this->Add<SidebarEntryTextInput>(
        "Access Token"_i18n, m_config.extra["access_token"], "", "", -1, PATH_MAX,
        "Cloud disk cached access_token (optional)."_i18n
    );

    m_cookie = this->Add<SidebarEntryTextInput>(
        "Cookie"_i18n, m_config.extra["cookie"], "", "", -1, PATH_MAX,
        "Quark cloud drive cookie."_i18n
    );

    m_drive_id = this->Add<SidebarEntryTextInput>(
        "Drive ID"_i18n, m_config.extra["drive_id"], "", "", -1, PATH_MAX,
        "Aliyun drive_id (optional)."_i18n
    );

    m_device_id = this->Add<SidebarEntryTextInput>(
        "Device ID"_i18n, m_config.extra["device_id"], "", "", -1, PATH_MAX,
        "Guangya device_id (optional)."_i18n
    );

    this->Add<SidebarEntryBool>(
        "Read only"_i18n, m_config.read_only,
        i18n::get("mount_readonly_info",
            "Mount the filesystem as read only.\n\n"
            "Setting this option also hidens the mount from being show as an export option.")
    );

    this->Add<SidebarEntryBool>(
        "No stat file"_i18n, m_config.no_stat_file,
        i18n::get("filecheck_disable_info",
            "Enabling stops the file browser from checking the file size and timestamp of each file. "
            "This improves browsing performance.")
    );

    this->Add<SidebarEntryBool>(
        "No stat dir"_i18n, m_config.no_stat_dir,
        i18n::get("dircheck_disable_info",
            "Enabling stops the file browser from checking how many files and folders are in a folder. "
            "This improves browsing performance, especially for servers that has slow directory listing.")
    );

    this->Add<SidebarEntryBool>(
        "FS hidden"_i18n, m_config.fs_hidden,
        "Hide the mount from being visible in the file browser."_i18n
    );

    this->Add<SidebarEntryBool>(
        "Export hidden"_i18n, m_config.dump_hidden,
        "Hide the mount from being visible as a export option for games and saves."_i18n
    );

    // set default scheme when creating a new entry.
    if (type_change) {
        UpdateSchemeURL();
    }

    const auto callback = this->Add<SidebarEntryCallback>("Save"_i18n, [this](){
        m_config.name = m_name->GetValue();
        m_config.url = m_url->GetValue();
        m_config.user = m_user->GetValue();
        m_config.pass = m_pass->GetValue();
        m_config.dump_path = m_dump_path->GetValue();
        m_config.port = std::stoul(m_port->GetValue());
        // m_config.timeout = m_timeout->GetValue();

        const auto ini_path = BuildIniPathFromType(m_type);

        fs::FsNativeSd().CreateDirectoryRecursively(MOUNT_PATH);
        ini_puts(m_config.name.c_str(), "url", m_config.url.c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "user", m_config.user.c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "pass", m_config.pass.c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "dump_path", m_config.dump_path.c_str(), ini_path);
        ini_putl(m_config.name.c_str(), "port", m_config.port, ini_path);
        ini_putl(m_config.name.c_str(), "timeout", m_config.timeout, ini_path);
        // todo: update minini to have put_bool.
        ini_puts(m_config.name.c_str(), "read_only", m_config.read_only ? "true" : "false", ini_path);
        ini_puts(m_config.name.c_str(), "no_stat_file", m_config.no_stat_file ? "true" : "false", ini_path);
        ini_puts(m_config.name.c_str(), "no_stat_dir", m_config.no_stat_dir ? "true" : "false", ini_path);
        ini_puts(m_config.name.c_str(), "fs_hidden", m_config.fs_hidden ? "true" : "false", ini_path);
        ini_puts(m_config.name.c_str(), "dump_hidden", m_config.dump_hidden ? "true" : "false", ini_path);

        // 云盘鉴权字段（extra 键）。
        ini_puts(m_config.name.c_str(), "client_id", m_client_id->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "client_secret", m_client_secret->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "refresh_token", m_refresh_token->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "access_token", m_access_token->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "cookie", m_cookie->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "drive_id", m_drive_id->GetValue().c_str(), ini_path);
        ini_puts(m_config.name.c_str(), "device_id", m_device_id->GetValue().c_str(), ini_path);

        App::Notify("Mount entry saved. Restart Sphaira to apply changes."_i18n);

        this->SetPop();
    },  "Saves the mount entry.\n\n"
        "NOTE: You must restart Sphaira for changes to take effect!"_i18n);

    // ensure that all fields are valid.
    callback->Depends([this](){
        return
            !m_name->GetValue().empty() &&
            !m_url->GetValue().empty() &&
            !m_url->GetValue().ends_with("://");
    }, "Name and URL must be set!"_i18n);
}

} // namespace

void DisplayDevoptabSideBar() {
    auto options = std::make_unique<Sidebar>("Devoptab Options"_i18n, Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>("Create New Entry"_i18n, [](){
        App::Push<DevoptabForm>();
    }, "Creates a new mount option.\n\n"
        "NOTE: You must restart Sphaira for changes to take effect!"_i18n);

    options->Add<SidebarEntryCallback>("Modify Existing Entry"_i18n, [](){
        PopupList::Items items;
        TypeConfigs configs;
        LoadAllConfigs(configs);

        for (const auto& e : configs) {
            items.emplace_back(GetTypeName(e));
        }

        if (items.empty()) {
            App::Notify("No mount entries found."_i18n);
            return;
        }

        App::Push<PopupList>("Modify Entry"_i18n, items, [configs](std::optional<s64> index){
            if (!index.has_value()) {
                return;
            }

            const auto& entry = configs[index.value()];
            App::Push<DevoptabForm>(entry.type.type, entry.config);
        });
    },  "Modify an existing mount option.\n\n"
        "NOTE: You must restart Sphaira for changes to take effect!"_i18n);

    options->Add<SidebarEntryCallback>("Delete Existing Entry"_i18n, [](){
        PopupList::Items items;
        TypeConfigs configs;
        LoadAllConfigs(configs);

        for (const auto& e : configs) {
            items.emplace_back(GetTypeName(e));
        }

        if (items.empty()) {
            App::Notify("No mount entries found."_i18n);
            return;
        }

        App::Push<PopupList>("Delete Entry"_i18n, items, [configs](std::optional<s64> index){
            if (!index.has_value()) {
                return;
            }

            const auto& entry = configs[index.value()];
            const auto ini_path = BuildIniPathFromType(entry.type.type);
            ini_puts(entry.config.name.c_str(), nullptr, nullptr, ini_path);
        });
    },  "Delete an existing mount option.\n\n"
        "NOTE: You must restart Sphaira for changes to take effect!"_i18n);
}

} // namespace sphaira::devoptab
