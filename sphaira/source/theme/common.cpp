#include "theme/common.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstring>

namespace sphaira::theme {

SystemVersion hos::Version = { 0, 0, 0 };

// Info for fw 6.0+
const std::unordered_map<std::string, ThemeTargetInfo> ThemeTargetList6 {
    { "home", { ThemeTargetInfo::QlaunchID, "Home menu",        "/lyt/ResidentMenu.szs" } },
    { "lock", { ThemeTargetInfo::QlaunchID, "Lock screen",      "/lyt/Entrance.szs" } },
    { "apps", { ThemeTargetInfo::QlaunchID, "All apps menu",    "/lyt/Flaunch.szs" } },
    { "set",  { ThemeTargetInfo::QlaunchID, "Settings applet",  "/lyt/Set.szs" } },
    { "news", { ThemeTargetInfo::QlaunchID, "News applet",      "/lyt/Notification.szs" } },
    { "user", { ThemeTargetInfo::UserPageID, "User page",       "/lyt/MyPage.szs" } },
    { "psl",  { ThemeTargetInfo::PslID, "Player selection",     "/lyt/Psl.szs" } },
};

// Info for fw <=5.x
const std::unordered_map<std::string, ThemeTargetInfo> ThemeTargetList5 {
    { "home", { ThemeTargetInfo::QlaunchID, "Home menu",        "/lyt/ResidentMenu.szs" } },
    { "lock", { ThemeTargetInfo::QlaunchID, "Lock screen",      "/lyt/Entrance.szs" } },
    { "apps", { ThemeTargetInfo::QlaunchID, "All applets",      "/lyt/Flaunch.szs" } },
    { "set",  { ThemeTargetInfo::QlaunchID, "All applets",      "/lyt/Set.szs" } },
    { "news", { ThemeTargetInfo::QlaunchID, "All applets",      "/lyt/Notification.szs" } },
    { "user", { ThemeTargetInfo::UserPageID, "User page",       "/lyt/MyPage.szs" } },
    { "psl",  { ThemeTargetInfo::PslID, "Player selection",     "/lyt/Psl.szs" } },
};

const ThemeTargetInfo ThemeTargetInfo::QlaunchCommon = {
    QlaunchID, "Home menu common layout", "/lyt/common.szs"
};

std::string ThemeTargetInfo::TitleIdToString(u64 tid) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)tid);
    return buf;
}

std::string ThemeTargetInfo::StringContentId() const {
    return TitleIdToString(TitleId);
}

const ThemeTargetInfo* ThemeTargetInfo::Find(std::string nxThemeName) {
    if (hos::Version.major <= 5) {
        if (ThemeTargetList5.count(nxThemeName))
            return &ThemeTargetList5.at(nxThemeName);
    } else {
        if (ThemeTargetList6.count(nxThemeName))
            return &ThemeTargetList6.at(nxThemeName);
    }
    return nullptr;
}

std::vector<std::string> ThemeTargetInfo::GetTargetsForTitleId(u64 tid) {
    std::vector<std::string> res = {};
    if (tid == QlaunchID)
        res.push_back(QlaunchCommon.SzsFile);

    for (const auto& [name, info] : ThemeTargetList6) {
        if (info.TitleId == tid)
            res.push_back(info.SzsFile);
    }
    return res;
}

static std::string GetFileName(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

const ThemeTargetInfo* ThemeTargetInfo::FindBySzsName(std::string szsName, std::string& outNxPartName) {
    for (const auto& [name, info] : ThemeTargetList6) {
        if (GetFileName(info.SzsFile) == szsName) {
            outNxPartName = name;
            return &info;
        }
    }
    outNxPartName = "";
    return nullptr;
}

void hos::InitializeVersion() {
    if (R_FAILED(setsysInitialize())) {
        log_write("[theme] setsys init failed\n");
        return;
    }
    ON_SCOPE_EXIT(setsysExit());

    SetSysFirmwareVersion fw{};
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
        // display_version 形如 "19.0.1"。
        u32 major = 0, minor = 0, micro = 0;
        std::sscanf(fw.display_version, "%u.%u.%u", &major, &minor, &micro);
        Version = { major, minor, micro };
        log_write("[theme] hos version: %u.%u.%u\n", major, minor, micro);
    }
}

} // namespace sphaira::theme
