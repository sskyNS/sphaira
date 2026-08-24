#pragma once

// 移植自 SwitchThemeInjector 的 SwitchThemesCommon/Common.hpp。
// 公共类型、固件版本、主题目标（TitleId）映射。

#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <unordered_map>
#include <compare>
#include <switch.h>

namespace sphaira::theme {

using FileData = std::vector<u8>;
using FileContainer = std::unordered_map<std::string, FileData>;

// 布局兼容级别，不严格对应 HOS 版本，仅在新固件引入破坏性布局变更时新增。
enum class ConsoleFirmware : int {
    Invariant = 0,
    Fw5_0 = 5'0'0,
    Fw6_0 = 6'0'0,
    Fw8_0 = 8'0'0,
    Fw9_0 = 9'0'0,
    Fw11_0 = 11'0'0,
    Fw20_0 = 20'0'0,
};

struct SystemVersion {
    u32 major, minor, micro;

    constexpr auto operator<=>(const SystemVersion& other) const {
        auto m = major <=> other.major;
        if (m == std::strong_ordering::equal) m = minor <=> other.minor;
        if (m == std::strong_ordering::equal) m = micro <=> other.micro;
        return m;
    }

    ConsoleFirmware ToFirmwareEnum() const {
        if (major < 5) return ConsoleFirmware::Invariant;
        if (major == 5) return ConsoleFirmware::Fw5_0;
        if (major == 6 || major == 7) return ConsoleFirmware::Fw6_0;
        if (major == 8) return ConsoleFirmware::Fw8_0;
        if (major == 9 || major == 10) return ConsoleFirmware::Fw9_0;
        if (major >= 11 && major < 20) return ConsoleFirmware::Fw11_0;
        if (major >= 20) return ConsoleFirmware::Fw20_0;
        return ConsoleFirmware::Invariant;
    }
};

struct ThemeTargetInfo {
    u64 TitleId;
    std::string PartName;
    std::string SzsFile;

    std::string StringContentId() const;

    static constexpr u64 QlaunchID = 0x0100000000001000;
    static constexpr u64 PslID = 0x0100000000001007;
    static constexpr u64 UserPageID = 0x0100000000001013;

    // 不在主题目标名中，仅用于解包。
    static const ThemeTargetInfo QlaunchCommon;

    // 若 part 名无效返回 nullptr。
    static const ThemeTargetInfo* Find(std::string nxThemeName);
    static const ThemeTargetInfo* FindBySzsName(std::string szsName, std::string& outNxPartName);
    static std::vector<std::string> GetTargetsForTitleId(u64 tid);
    static std::string TitleIdToString(u64 tid);
};

namespace hos {
    extern SystemVersion Version;
    // 从 setsys 读取当前系统版本并填充 hos::Version。
    void InitializeVersion();
}

} // namespace sphaira::theme
