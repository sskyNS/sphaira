#pragma once

// 移植自 SwitchThemeInjector 的 Layouts/Patches.hpp。
// 布局补丁数据模型 + 补丁/纹理替换模板声明。

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include "theme/common.hpp"

namespace sphaira::theme {

struct Vector3 { float X, Y, Z; bool operator==(Vector3 const&) const = default; };
struct Vector2 { float X, Y; bool operator==(Vector2 const&) const = default; };

struct UsdPatch {
    std::string PropName;
    std::vector<std::string> PropValues;
    int type;
    bool operator==(UsdPatch const&) const = default;
};

struct PanePatch {
    std::string PaneName;
    Vector3 Position, Rotation;
    Vector2 Scale, Size;
    bool Visible;

    u8 OriginX;
    u8 OriginY;
    u8 ParentOriginX;
    u8 ParentOriginY;

    u32 ApplyFlags; // 置位表示对应属性在 JSON 中已设置，需要应用

    enum class Flags : u32 {
        Visible = 1,
        Position = 1 << 1,
        Rotation = 1 << 2,
        Scale = 1 << 3,
        Size = 1 << 4,
        PaneSpecific0 = 1 << 5,
        PaneSpecific1 = 1 << 6,
        PaneSpecific2 = 1 << 7,
        PaneSpecific3 = 1 << 8,
        UsdPatches = 1 << 9,
        OriginX = 1 << 10,
        OriginY = 1 << 11,
        ParentOriginX = 1 << 12,
        ParentOriginY = 1 << 13,
    };

    std::vector<UsdPatch> UsdPatches;
    std::string PaneSpecific[4];

    inline std::string& PaneSpecific0() { return PaneSpecific[0]; } // PIC1: 左上色 / TXT1: 顶部字体色
    inline std::string& PaneSpecific1() { return PaneSpecific[1]; } // PIC1: 右上色 / TXT1: 顶部阴影色
    inline std::string& PaneSpecific2() { return PaneSpecific[2]; } // PIC1: 左下色 / TXT1: 底部字体色
    inline std::string& PaneSpecific3() { return PaneSpecific[3]; } // PIC1: 右下色 / TXT1: 底部阴影色

    bool operator==(PanePatch const&) const = default;
};

struct ExtraGroup {
    std::string GroupName;
    std::vector<std::string> Panes;
    bool operator==(ExtraGroup const&) const = default;
};

struct MaterialPatch {
    struct TexReference {
        std::string Name;
        std::optional<u8> WrapS;
        std::optional<u8> WrapT;
        bool operator==(TexReference const&) const = default;
    };

    struct TexTransform {
        std::string Name;
        std::optional<float> X;
        std::optional<float> Y;
        std::optional<float> Rotation;
        std::optional<float> ScaleX;
        std::optional<float> ScaleY;
        bool operator==(TexTransform const&) const = default;
    };

    std::string MaterialName;
    std::string ForegroundColor;
    std::string BackgroundColor;

    std::vector<TexReference> Refs;
    std::vector<TexTransform> Transforms;

    bool operator==(MaterialPatch const&) const = default;
};

struct LayoutFilePatch {
    std::string FileName;
    std::vector<PanePatch> Patches;
    std::vector<ExtraGroup> AddGroups;
    std::vector<MaterialPatch> Materials;

    std::vector<std::string> PushBackPanes;
    std::vector<std::string> PullFrontPanes;

    bool operator==(LayoutFilePatch const&) const = default;
};

struct AnimFilePatch {
    std::string FileName;
    std::string AnimJson;
    bool operator==(AnimFilePatch const&) const = default;
};

struct LayoutPatch {
    std::string PatchName;
    std::string AuthorName;
    std::vector<LayoutFilePatch> Files;
    std::vector<AnimFilePatch> Anims;
    bool PatchAppletColorAttrib = false;
    std::string ID;
    bool HideOnlineBtn;

    int TargetFirmware;

    bool Obsolete_Ready8X = false;
    bool UsesOldFixes() const { return ID == "" && !Obsolete_Ready8X; }

    bool operator==(LayoutPatch const&) const = default;
};

struct PatchTemplate {
    std::string TemplateName;
    std::string SzsName;
    std::string TitleId;
    std::string FirmName;

    std::vector<std::string> FnameIdentifier;
    std::vector<std::string> FnameNotIdentifier;

    std::string MainLayoutName;
    std::string MaintextureName;
    std::string PatchIdentifier;
    std::vector<std::string> targetPanels;
    std::string SecondaryTexReplace;

    std::string NXThemeName;

    bool DirectPatchPane = false;
    bool NoRemovePanel = false;
    bool RequiresCodePatch = false;
};

struct TextureReplacement {
    std::string NxThemeName;
    std::vector<std::string> BntxNames;
    u32 NewColorFlags;
    std::string FileName;
    std::string PaneName;
    s32 W, H;
    LayoutFilePatch Patch;
    ConsoleFirmware MinFirmware = ConsoleFirmware::Invariant;
};

namespace Patches {
    extern std::vector<PatchTemplate> DefaultTemplates;

    namespace textureReplacement {
        extern std::unordered_map<std::string, std::vector<TextureReplacement>> NxNameToList;
    }

    LayoutPatch LoadLayout(const std::string_view json);
}

namespace NewFirmFixes {
    std::optional<LayoutPatch> GetFixLegacy(const std::string& LayoutName, ConsoleFirmware fw, const std::string& NXThemeName);
    std::optional<LayoutPatch> GetFix(LayoutPatch& layout, ConsoleFirmware fw);
    bool ShouldApplyAppletPositionFix(const LayoutPatch& layout, ConsoleFirmware consoleFw);
    std::optional<LayoutPatch> GetLegacyAppletButtonsFix(ConsoleFirmware fw);
    std::optional<LayoutPatch> GetAppletsPositionFix(ConsoleFirmware fw);
}

} // namespace sphaira::theme
