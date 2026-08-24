#pragma once

// 移植自 SwitchThemeInjector 的 Patcher.hpp + Layouts/LayoutCompatibility.hpp。
// SzsPatcher 调度层 + 动画兼容性检查。

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "theme/common.hpp"
#include "theme/patches.hpp"
#include "theme/sarc.hpp"
#include "theme/bntx.hpp"
#include "theme/bflyt.hpp"
#include "theme/bflan.hpp"

namespace sphaira::theme {

struct BntxTexAttribPatch {
    std::string TargetTexutre;
    u32 ChannelData;
};

enum class LayoutCompatibilityOption : int {
    Default,       // 根据版本检测自动应用布局修复
    DisableFixes,  // 禁用所有布局修复
    Firmware10,    // 强制 pre-11.0 布局
    Firmware11,    // 强制 11.0 布局
};

namespace Compatibility {

    enum class ProblemType {
        MissingFile,
        MissingPane,
        MissingGroup,
        MissingTexture,
        Uncertain
    };

    enum class ProblemSeverity {
        AutoIgnored,
        Critical
    };

    struct CompatIssue {
        std::string FileName;
        std::string ItemName;
        std::string AdditionalInfo;
        ProblemType Type;
        ProblemSeverity Severity;

        static CompatIssue MissingPane(std::string_view fileName, std::string_view paneName, std::string_view additional = "", bool critical = false);
        static CompatIssue Uncertain(std::string_view fileName, std::string_view itemName, std::string_view additional);
        static CompatIssue MissingGroup(std::string_view fileName, std::string_view groupName);
    };

    std::string LayoutNameForAnimation(std::string_view animationName);
    void CheckAnimationCompatibility(std::vector<CompatIssue>& res, const LayoutPatch& layout, const SARC::SarcData& szs, std::string_view animName, const Bflan& bflan);
}

class SzsPatcher {
public:
    SzsPatcher(SARC::SarcData&& s);
    SzsPatcher(SARC::SarcData& s);
    ~SzsPatcher();

    LayoutCompatibilityOption CompatFixes = LayoutCompatibilityOption::Default;

    bool PatchLayouts();
    bool PatchLayouts(const LayoutPatch& patch);
    bool PatchLayouts(const LayoutPatch& patch, const std::string& PartName);

    bool PatchMainBG(const std::vector<u8>& DDS);
    bool PatchAppletIcon(const std::vector<u8>& DDS, const std::string& texName);
    bool PatchBntxTexture(const std::vector<u8>& DDS, const std::vector<std::string>& texNames, u32 ChannelData = 0xFFFFFFFF);
    bool PatchBntxTextureAttribs(const std::vector<BntxTexAttribPatch>& patches);
    static std::optional<PatchTemplate> DetectSarc(const SARC::SarcData&);

    const std::optional<PatchTemplate>& DetectedSarc();

    const SARC::SarcData& GetSarc();
    SARC::SarcData& GetFinalSarc();

    int TotalNonCompatibleFixes = 0;

private:
    SARC::SarcData sarc;
    ConsoleFirmware currentFirmware;
    std::optional<PatchTemplate> currentTemplate;
    std::string nxthemePartName;

    void Initialize();

    QuickBntx* bntx = nullptr;
    QuickBntx& OpenBntx();
    void SaveBntx();

    bool EnableAnimations = true;

    void ApplyRawPatch(const std::optional<LayoutPatch>& p);
    void ApplyRawPatch(const LayoutPatch* p);

    std::optional<uint32_t> FirmwareTargetBflanVersion = std::nullopt;
    bool ApplyAnimPatch(const AnimFilePatch& p);
    bool ApplyLayoutPatch(const LayoutFilePatch& p);

    int FilterIncompatibleAnimations(LayoutPatch& p);
};

std::string GeneratePatchListString(const std::vector<PatchTemplate>& templates);

} // namespace sphaira::theme
