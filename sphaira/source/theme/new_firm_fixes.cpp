#include "theme/patches.hpp"

#include <algorithm>
#include <cctype>

namespace sphaira::theme {
namespace {

// 自动生成的兼容性修复 JSON（来自 NxThemeTool/Compatibility/*.json）。
constexpr std::string_view Fix_11_NoMoveApplets = "{ \"PatchName\": \"G_SystemNoMove11.0\", \"Anims\": [ { \"FileName\": \"anim/RdtBase_SystemAppletPos.bflan\", \"AnimJson\": \"{\\u0022LittleEndian\\u0022:true,\\u0022Version\\u0022:150994944,\\u0022pat1\\u0022:{\\u0022AnimationOrder\\u0022:6,\\u0022Name\\u0022:\\u0022SystemAppletPos\\u0022,\\u0022ChildBinding\\u0022:190,\\u0022Groups\\u0022:[\\u0022G_System\\u0022],\\u0022Unk_StartOfFile\\u0022:0,\\u0022Unk_EndOfFile\\u0022:0,\\u0022Unk_EndOfHeader\\u0022:\\u0022AL8AAAAAAA==\\u0022},\\u0022pai1\\u0022:{\\u0022FrameSize\\u0022:1,\\u0022Flags\\u0022:0,\\u0022Textures\\u0022:[],\\u0022Entries\\u0022:[{\\u0022Name\\u0022:\\u0022L_BtnLR\\u0022,\\u0022Target\\u0022:0,\\u0022Tags\\u0022:[{\\u0022Unknown\\u0022:0,\\u0022TagType\\u0022:\\u0022FLVI\\u0022,\\u0022Entries\\u0022:[{\\u0022Index\\u0022:0,\\u0022AnimationTarget\\u0022:0,\\u0022DataType\\u0022:1,\\u0022KeyFrames\\u0022:[{\\u0022Frame\\u0022:0.0,\\u0022Value\\u0022:0.0,\\u0022Blend\\u0022:0.0},{\\u0022Frame\\u0022:1.0,\\u0022Value\\u0022:1.0,\\u0022Blend\\u0022:0.0}],\\u0022FLEUUnknownInt\\u0022:0,\\u0022FLEUEntryName\\u0022:\\u0022\\u0022}]}],\\u0022UnkwnownData\\u0022:\\u0022\\u0022}]}}\" } ] }";

constexpr std::string_view Fix_11_NoOnlineButton = "{ \"PatchName\": \"NoOnline11\", \"Anims\": [ { \"FileName\": \"anim/RdtBase_SystemAppletPos.bflan\", \"AnimJson\": \"{\\u0022LittleEndian\\u0022:true,\\u0022Version\\u0022:150994944,\\u0022pat1\\u0022:{\\u0022AnimationOrder\\u0022:6,\\u0022Name\\u0022:\\u0022SystemAppletPos\\u0022,\\u0022ChildBinding\\u0022:190,\\u0022Groups\\u0022:[\\u0022G_System\\u0022],\\u0022Unk_StartOfFile\\u0022:0,\\u0022Unk_EndOfFile\\u0022:0,\\u0022Unk_EndOfHeader\\u0022:\\u0022AL8AAAAAAA==\\u0022},\\u0022pai1\\u0022:{\\u0022FrameSize\\u0022:1,\\u0022Flags\\u0022:0,\\u0022Textures\\u0022:[],\\u0022Entries\\u0022:[]}}\" } ] }";

constexpr std::string_view Fix_20_CarefulLayout = "{ \"PatchName\": \"Careful layout 20.0 fix\", \"Files\": [ { \"FileName\": \"blyt/RdtBtnNtf.bflyt\", \"Patches\": [ { \"PaneName\": \"P_PictBase\", \"Scale\": { \"X\": 12.7, \"Y\": 1 } } ] }, { \"FileName\": \"blyt/RdtBtnPvr.bflyt\", \"Patches\": [ { \"PaneName\": \"P_PictBase\", \"Scale\": { \"X\": 2.2, \"Y\": 0.9 } } ] } ] }";

constexpr std::string_view Fix_20_DowngradeTo19 = "{ \"PatchName\": \"Downgrade 20.x layout\", \"AuthorName\": \"autoDiff\", \"Files\": [ { \"FileName\": \"blyt/RdtBase.bflyt\", \"Patches\": [ { \"PaneName\": \"L_BtnSplay\", \"Visible\": false }, { \"PaneName\": \"L_BtnVgc\", \"Visible\": false }, { \"PaneName\": \"N_System\", \"Position\": { \"Y\": -186, \"X\": 54 } }, { \"PaneName\": \"L_BtnLR\", \"Position\": { \"X\": -378 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnNoti\", \"Position\": { \"X\": -270 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnShop\", \"Position\": { \"X\": -162 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnPvr\", \"Position\": { \"X\": -54 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnCtrl\", \"Position\": { \"X\": 54 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnSet\", \"Position\": { \"X\": 162 }, \"Size\": { \"X\": 508, \"Y\": 180 } }, { \"PaneName\": \"L_BtnPow\", \"Position\": { \"X\": 270 }, \"Size\": { \"X\": 508, \"Y\": 180 } } ] } ] }";

constexpr std::string_view Fix_20_FlowLayout = "{ \"PatchName\": \"Flow layout 20.0 fix\", \"Files\": [ { \"FileName\": \"blyt/RdtBtnNtf.bflyt\", \"Patches\": [ { \"PaneName\": \"P_PictBase\", \"Scale\": { \"X\": 21.5, \"Y\": 1.25 } } ] } ] }";

constexpr std::string_view Fix_20_LegacyAppletButtons = "{ \"PatchName\": \"No online button 20.0\", \"AuthorName\": \"autoDiff\", \"Files\": [ { \"FileName\": \"blyt/RdtBase.bflyt\", \"Patches\": [ { \"PaneName\": \"L_BtnLR\", \"Visible\": false }, { \"PaneName\": \"N_System\", \"Position\": { \"Y\": -186 } } ] } ] }";

constexpr std::string_view Fix_Legacy_ClearLock = "{ \"PatchName\": \"clearlayout 9.x fix\", \"AuthorName\": \"exelix\", \"Files\": [ { \"FileName\": \"blyt/EntMain.bflyt\", \"Patches\": [ { \"PaneName\": \"L_BtnResume\", \"Position\": { \"X\": -180, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_CntHud\", \"Position\": { \"X\": 0, \"Y\": 0, \"Z\": 0 } } ] } ] }";

constexpr std::string_view Fix_Legacy_Compact = "{ \"PatchName\": \"Compact 8 fix\", \"AuthorName\": \"akai\", \"Files\": [ { \"FileName\": \"blyt/RdtBtnFullLauncher.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Root\", \"Rotation\": { \"X\": 0, \"Y\": 0, \"Z\": 45 } } ] }, { \"FileName\": \"blyt/RdtBtnIconGame.bflyt\", \"Patches\": [ { \"PaneName\": \"RootPane\", \"Scale\": { \"X\": 0.37, \"Y\": 0.37 } }, { \"PaneName\": \"B_Hit\", \"Scale\": { \"X\": 3, \"Y\": 3 }, \"Size\": { \"X\": 97.68, \"Y\": 97.68 } } ] }, { \"FileName\": \"blyt/RdtBase.bflyt\", \"Patches\": [ { \"PaneName\": \"N_ScrollArea\", \"Position\": { \"X\": 0, \"Y\": -280, \"Z\": 0 }, \"Scale\": { \"X\": 1, \"Y\": 0.5 }, \"Size\": { \"X\": 1300, \"Y\": 322 } }, { \"PaneName\": \"N_ScrollWindow\", \"Position\": { \"X\": 0, \"Y\": -280, \"Z\": 0 }, \"Scale\": { \"X\": 1, \"Y\": 0.5 }, \"Size\": { \"X\": 1080, \"Y\": 322 } }, { \"PaneName\": \"N_GameRoot\", \"Position\": { \"X\": -50, \"Y\": -230, \"Z\": 0 }, \"Scale\": { \"X\": 0.000001, \"Y\": 1 } }, { \"PaneName\": \"N_Game\", \"Position\": { \"X\": 0, \"Y\": 0, \"Z\": 0 }, \"Scale\": { \"X\": 1000000, \"Y\": 1 } }, { \"PaneName\": \"N_Icon_01\", \"Position\": { \"X\": 100, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_02\", \"Position\": { \"X\": 200, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_03\", \"Position\": { \"X\": 300, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_04\", \"Position\": { \"X\": 400, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_05\", \"Position\": { \"X\": 500, \"Y\": 0, \"Z\": 0 }, \"Visible\": true }, { \"PaneName\": \"N_Icon_06\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_07\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_08\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_09\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_10\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_11\", \"Position\": { \"X\": 1, \"Y\": -9999999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_12\", \"Position\": { \"X\": 600, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnFlc\", \"Scale\": { \"X\": 0.37, \"Y\": 0.37 } }, { \"PaneName\": \"L_BtnAccount_00\", \"Position\": { \"X\": -247, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_01\", \"Position\": { \"X\": -175, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_02\", \"Position\": { \"X\": -103, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_03\", \"Position\": { \"X\": -31, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_04\", \"Position\": { \"X\": 41, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_05\", \"Position\": { \"X\": 113, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_06\", \"Position\": { \"X\": 185, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_Hud\", \"Position\": { \"X\": -290, \"Y\": -325, \"Z\": 0 } } ] }, { \"FileName\": \"blyt/Hud.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Time\", \"Position\": { \"X\": 10, \"Y\": 0, \"Z\": 0 } } ] } ] }";

constexpr std::string_view Fix_Legacy_Diamond = "{ \"PatchName\": \"Diamond 8 fix\", \"AuthorName\": \"akai\", \"Files\": [ { \"FileName\": \"blyt/RdtBtnFullLauncher.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Root\", \"Rotation\": { \"X\": 0, \"Y\": 0, \"Z\": 45 } } ] }, { \"FileName\": \"blyt/RdtBtnIconGame.bflyt\", \"Patches\": [ { \"PaneName\": \"RootPane\", \"Scale\": { \"X\": 0.37, \"Y\": 0.37 } }, { \"PaneName\": \"B_Hit\", \"Scale\": { \"X\": 3, \"Y\": 3 }, \"Size\": { \"X\": 97.68, \"Y\": 97.68 } } ] }, { \"FileName\": \"blyt/RdtBase.bflyt\", \"Patches\": [ { \"PaneName\": \"N_ScrollArea\", \"Position\": { \"X\": -30, \"Y\": -200, \"Z\": 0 }, \"Size\": { \"X\": 1300, \"Y\": 322 } }, { \"PaneName\": \"N_ScrollWindow\", \"Position\": { \"X\": -30, \"Y\": -200, \"Z\": 0 }, \"Size\": { \"X\": 1080, \"Y\": 322 } }, { \"PaneName\": \"N_GameRoot\", \"Position\": { \"X\": 200, \"Y\": -195, \"Z\": 0 }, \"Scale\": { \"X\": 0.00001, \"Y\": 1 } }, { \"PaneName\": \"N_Game\", \"Position\": { \"X\": 0, \"Y\": 0, \"Z\": 0 }, \"Scale\": { \"X\": 100000, \"Y\": 1 } }, { \"PaneName\": \"N_Icon_01\", \"Position\": { \"X\": 140, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_02\", \"Position\": { \"X\": 280, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_03\", \"Position\": { \"X\": 68, \"Y\": 73, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_04\", \"Position\": { \"X\": 208, \"Y\": 73, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_05\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_06\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_07\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_08\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_09\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_10\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_11\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_12\", \"Position\": { \"X\": 348, \"Y\": 73, \"Z\": 0 } }, { \"PaneName\": \"L_BtnFlc\", \"Scale\": { \"X\": 0.37, \"Y\": 0.37 } }, { \"PaneName\": \"N_System\", \"Position\": { \"X\": -600, \"Y\": -250, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_00\", \"Position\": { \"X\": -247, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_01\", \"Position\": { \"X\": -175, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_02\", \"Position\": { \"X\": -103, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_03\", \"Position\": { \"X\": -31, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_04\", \"Position\": { \"X\": 41, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_05\", \"Position\": { \"X\": 113, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_BtnAccount_06\", \"Position\": { \"X\": 185, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_Hud\", \"Position\": { \"X\": -300, \"Y\": -325, \"Z\": 0 } } ] }, { \"FileName\": \"blyt/Hud.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Time\", \"Position\": { \"X\": 10, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"L_Time\", \"Position\": { \"X\": 0, \"Y\": 0, \"Z\": 0 } } ] } ] }";

constexpr std::string_view Fix_Legacy_DogeLayout = "{ \"PatchName\": \"DogeLayout 8.x fix\", \"AuthorName\": \"autoDiff\", \"Files\": [ { \"FileName\": \"blyt/HudTime.bflyt\", \"Patches\": [ { \"PaneName\": \"N_AMPM\", \"Position\": { \"X\": 30, \"Y\": -1, \"Z\": 0 }, \"Scale\": { \"X\": 0.9, \"Y\": 0.9 } } ] }, { \"FileName\": \"blyt/RdtBtnFullLauncher.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Tip\", \"Scale\": { \"X\": 1.1, \"Y\": 1.1 } }, { \"PaneName\": \"B_Hit\", \"Size\": { \"X\": 80, \"Y\": 80 } } ] }, { \"FileName\": \"blyt/Cursor3.bflyt\", \"Patches\": [ { \"PaneName\": \"P_Main\", \"UsdPatches\": [ { \"PropName\": \"S_BorderSize\", \"PropValues\": [ \"7\" ], \"type\": 2 } ] }, { \"PaneName\": \"P_Grow\", \"UsdPatches\": [ { \"PropName\": \"S_BorderSize\", \"PropValues\": [ \"7\" ], \"type\": 2 } ] } ] }, { \"FileName\": \"blyt/RdtBtnMyPage.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Tip\", \"Position\": { \"X\": 125, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"B_Hit\", \"Scale\": { \"X\": 1.428571, \"Y\": 1.428571 }, \"Size\": { \"X\": 40, \"Y\": 40 } } ] }, { \"FileName\": \"blyt/RdtBtnIconGame.bflyt\", \"Patches\": [ { \"PaneName\": \"RootPane\", \"Scale\": { \"X\": 0.5, \"Y\": 0.5 } }, { \"PaneName\": \"P_InnerCursor\", \"Visible\": false }, { \"PaneName\": \"N_BtnFocusKey\", \"Size\": { \"X\": 259, \"Y\": 259 } }, { \"PaneName\": \"N_Tip\", \"Scale\": { \"X\": 1.1, \"Y\": 1.1 } }, { \"PaneName\": \"B_Hit\", \"Scale\": { \"X\": 2, \"Y\": 2 }, \"Size\": { \"X\": 132, \"Y\": 132 } } ] }, { \"FileName\": \"blyt/RdtBase.bflyt\", \"Patches\": [ { \"PaneName\": \"N_ScrollArea\", \"Position\": { \"X\": 0, \"Y\": -218, \"Z\": 0 }, \"Scale\": { \"X\": 1, \"Y\": 0.5 }, \"Size\": { \"X\": 1300, \"Y\": 322 } }, { \"PaneName\": \"N_ScrollWindow\", \"Position\": { \"X\": 0, \"Y\": -218, \"Z\": 0 }, \"Size\": { \"X\": 100000, \"Y\": 322 } }, { \"PaneName\": \"T_Blank\", \"Position\": { \"X\": 0, \"Y\": 197, \"Z\": 0 } }, { \"PaneName\": \"N_GameRoot\", \"Position\": { \"X\": -530, \"Y\": -218, \"Z\": 0 }, \"Scale\": { \"X\": 0.00001, \"Y\": 1 } }, { \"PaneName\": \"N_Game\", \"Position\": { \"X\": 0, \"Y\": 0, \"Z\": 0 }, \"Scale\": { \"X\": 100000, \"Y\": 1 } }, { \"PaneName\": \"N_Icon_01\", \"Position\": { \"X\": 135, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_02\", \"Position\": { \"X\": 270, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_03\", \"Position\": { \"X\": 405, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_04\", \"Position\": { \"X\": 540, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_05\", \"Position\": { \"X\": 675, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_06\", \"Position\": { \"X\": 810, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_07\", \"Position\": { \"X\": 945, \"Y\": 0, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_08\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_09\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_10\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_11\", \"Position\": { \"X\": 1, \"Y\": 99999, \"Z\": 0 } }, { \"PaneName\": \"N_Icon_12\", \"Position\": { \"X\": 1080, \"Y\": 0, \"Z\": 0 }, \"Scale\": { \"X\": 1, \"Y\": 1 } }, { \"PaneName\": \"L_BtnFlc\", \"Scale\": { \"X\": 0.5, \"Y\": 0.5 } } ] }, { \"FileName\": \"blyt/Hud.bflyt\", \"Patches\": [ { \"PaneName\": \"N_Time\", \"Size\": { \"X\": 12, \"Y\": 30 } }, { \"PaneName\": \"L_Time\", \"Position\": { \"X\": -18, \"Y\": 0, \"Z\": 0 } } ] } ] }";

} // namespace

static bool contains_ignore_case(const std::string& strHaystack, const std::string& strNeedle) {
    auto it = std::search(
        strHaystack.begin(), strHaystack.end(),
        strNeedle.begin(), strNeedle.end(),
        [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );
    return (it != strHaystack.end());
}

static bool themezer_name_check(const std::string& layout_id, const std::string& themezer_name) {
    return layout_id == themezer_name || layout_id.starts_with(themezer_name + "|");
}

std::optional<LayoutPatch> NewFirmFixes::GetFixLegacy(const std::string& LayoutName, ConsoleFirmware fw, const std::string& nxName) {
    if (fw >= ConsoleFirmware::Fw9_0 && nxName == "lock") {
        if (contains_ignore_case(LayoutName, "clear lockscreen") && !Fix_Legacy_ClearLock.empty())
            return Patches::LoadLayout(Fix_Legacy_ClearLock);
    }

    if (fw >= ConsoleFirmware::Fw8_0 && nxName == "home") {
        if (!Fix_Legacy_DogeLayout.empty() && (contains_ignore_case(LayoutName, "dogelayout") || contains_ignore_case(LayoutName, "clearlayout")))
            return Patches::LoadLayout(Fix_Legacy_DogeLayout);
        else if (!Fix_Legacy_Diamond.empty() && contains_ignore_case(LayoutName, "diamond layout"))
            return Patches::LoadLayout(Fix_Legacy_Diamond);
        else if (!Fix_Legacy_Compact.empty() && contains_ignore_case(LayoutName, "small compact"))
            return Patches::LoadLayout(Fix_Legacy_Compact);
    }

    return std::nullopt;
}

std::optional<LayoutPatch> NewFirmFixes::GetFix(LayoutPatch& layout, ConsoleFirmware fw) {
    if (fw >= ConsoleFirmware::Fw9_0 && layout.ID == "builtin_ClearLock" && !Fix_Legacy_ClearLock.empty())
        return Patches::LoadLayout(Fix_Legacy_ClearLock);

    const auto apply20Fix = fw >= ConsoleFirmware::Fw20_0 && layout.TargetFirmware < (int)ConsoleFirmware::Fw20_0;

    if (apply20Fix) {
        if (layout.ID == "builtin_FlowLayout" || themezer_name_check(layout.ID, "Themezer:5"))
            return Fix_20_FlowLayout.empty() ? std::nullopt : std::optional(Patches::LoadLayout(Fix_20_FlowLayout));

        if (layout.ID == "builtin_CarefulLayout" || themezer_name_check(layout.ID, "Themezer:6"))
            return Fix_20_CarefulLayout.empty() ? std::nullopt : std::optional(Patches::LoadLayout(Fix_20_CarefulLayout));
    }

    return std::nullopt;
}

std::optional<LayoutPatch> NewFirmFixes::GetLegacyAppletButtonsFix(ConsoleFirmware fw) {
    if (fw >= ConsoleFirmware::Fw20_0 && !Fix_20_LegacyAppletButtons.empty())
        return Patches::LoadLayout(Fix_20_LegacyAppletButtons);
    if (fw >= ConsoleFirmware::Fw11_0 && !Fix_11_NoOnlineButton.empty())
        return Patches::LoadLayout(Fix_11_NoOnlineButton);
    return std::nullopt;
}

bool NewFirmFixes::ShouldApplyAppletPositionFix(const LayoutPatch& layout, ConsoleFirmware consoleFw) {
    if (consoleFw <= ConsoleFirmware::Fw11_0) {
        auto found = std::find_if(layout.Anims.begin(), layout.Anims.end(),
            [](const auto& x) { return x.FileName == "anim/RdtBase_SystemAppletPos.bflan"; });
        return found == layout.Anims.end();
    }

    if (consoleFw >= ConsoleFirmware::Fw20_0) {
        if (layout.TargetFirmware < (int)ConsoleFirmware::Fw20_0)
            return true;
        if (layout.HideOnlineBtn)
            return true;
    }

    return false;
}

std::optional<LayoutPatch> NewFirmFixes::GetAppletsPositionFix(ConsoleFirmware fw) {
    if (fw >= ConsoleFirmware::Fw20_0 && !Fix_20_DowngradeTo19.empty())
        return Patches::LoadLayout(Fix_20_DowngradeTo19);

    if (fw >= ConsoleFirmware::Fw11_0 && !Fix_11_NoMoveApplets.empty())
        return Patches::LoadLayout(Fix_11_NoMoveApplets);

    return std::nullopt;
}

} // namespace sphaira::theme
