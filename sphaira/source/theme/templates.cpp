#include "theme/patches.hpp"

namespace sphaira::theme {

// ============================ 默认补丁模板 ============================
// 数据来自 SwitchThemeInjector 的 DefaultTemplates（C# PatchTemplate.Templates）。

std::vector<PatchTemplate> Patches::DefaultTemplates = {
    { "home and applets", "common.szs", "0100000000001000", "<= 5.X",
      { "blyt/SystemAppletFader.bflyt" }, { "blyt/DHdrSoft.bflyt" },
      "blyt/BgNml.bflyt", "White1x1_180^r", "exelixBG", { "P_Bg_00" },
      "White1x1^r", "home", false, false, false },

    { "home menu", "ResidentMenu.szs", "0100000000001000", ">= 6.0",
      { "blyt/IconError.bflyt" }, { "anim/RdtBtnShop_LimitB.bflan" },
      "blyt/BgNml.bflyt", "White1x1A128^s", "exelixBG", { "P_Bg_00" },
      "White1x1A64^t", "home", false, false, false },

    { "home menu only", "ResidentMenu.szs", "0100000000001000", "<= 5.X",
      { "anim/RdtBtnShop_LimitB.bflan", "blyt/IconError.bflyt" }, {},
      "blyt/RdtBase.bflyt", "White1x1A128^s", "exelixResBG", { "L_BgNml" },
      "White1x1A64^t", "home", false, false, false },

    { "lock screen", "Entrance.szs", "0100000000001000", ">= 9.0",
      { "blyt/PageindicatorAlarm.bflyt", "blyt/EntBtnResumeSystemApplet.bflyt" }, {},
      "blyt/EntMain.bflyt", "White1x1^s", "exelixLK", { "P_BgL", "P_BgR" },
      "White1x1^r", "lock", false, false, true },

    { "lock screen", "Entrance.szs", "0100000000001000", "<= 8.0",
      { "blyt/EntBtnResumeSystemApplet.bflyt" }, { "blyt/PageindicatorAlarm.bflyt" },
      "blyt/EntMain.bflyt", "White1x1^s", "exelixLK", { "P_BgL", "P_BgR" },
      "White1x1^r", "lock", false, false, false },

    { "user page", "MyPage.szs", "0100000000001013", "all firmwares",
      { "blyt/MypUserIconMini.bflyt", "blyt/BgNav_Root.bflyt" }, {},
      "blyt/BgNml.bflyt", "NavBg_03^d", "exelixMY", { "P_Bg_00" },
      "White1x1A0^t", "user", false, false, false },

    { "all apps menu", "Flaunch.szs", "0100000000001000", ">= 6.0",
      { "blyt/FlcBtnIconGame.bflyt", "anim/BaseBg_Loading.bflan", "blyt/BgNav_Root.bflyt" }, {},
      "blyt/BgNml.bflyt", "NavBg_03^d", "exelixFBG", { "P_Bg_00" },
      "White1x1^r", "apps", false, false, false },

    { "settings applet", "Set.szs", "0100000000001000", ">= 6.0",
      { "blyt/BgNav_Root.bflyt", "blyt/SetCntDataMngPhoto.bflyt", "blyt/SetSideStory.bflyt" }, {},
      "blyt/BgNml.bflyt", "NavBg_03^d", "exelixSET", { "P_Bg_00" },
      "White1x1A0^t", "set", false, false, false },

    { "news applet", "Notification.szs", "0100000000001000", ">= 6.0",
      { "blyt/BgNavNoHeader.bflyt", "blyt/BgNav_Root.bflyt", "blyt/NtfBase.bflyt", "blyt/NtfImage.bflyt" }, {},
      "blyt/BgNml.bflyt", "NavBg_03^d", "exelixNEW", { "P_Bg_00" },
      "White1x1^r", "news", false, false, false },

    { "player selection", "Psl.szs", "0100000000001007", "all firmwares",
      { "blyt/IconGame.bflyt", "blyt/BgNavNoHeader.bflyt" }, {},
      "blyt/PslSelectSinglePlayer.bflyt", "PselTopUserIcon_Bg^s", "exelixPSL", { "P_Bg" },
      "White1x1^r", "psl", false, false, false },
};

// ============================ 纹理替换（应用图标） ============================
// 数据来自 SwitchThemeInjector 的 PatchTemplate.cs（TextureReplacement）。

namespace {

constexpr u32 F_VISIBLE = (u32)PanePatch::Flags::Visible;
constexpr u32 F_POSITION = (u32)PanePatch::Flags::Position;
constexpr u32 F_SCALE = (u32)PanePatch::Flags::Scale;
constexpr u32 F_SIZE = (u32)PanePatch::Flags::Size;
constexpr u32 F_USD = (u32)PanePatch::Flags::UsdPatches;

UsdPatch C_W() {
    return { "C_W", { "100", "100", "100", "100" }, 1 };
}

LayoutFilePatch MakePatch(const std::string& file, std::vector<PanePatch> patches) {
    return { file, std::move(patches), {}, {}, {}, {} };
}

} // namespace

namespace Patches::textureReplacement {

std::unordered_map<std::string, std::vector<TextureReplacement>> NxNameToList = {
    { "home", {
        { "album", { "RdtIcoPvr_00^s" }, 0x5050505, "blyt/RdtBtnPvr.bflyt", "P_Pict_00", 64, 56,
          MakePatch("blyt/RdtBtnPvr.bflyt", {
              PanePatch{ "P_Pict_00", {22,13,0}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_POSITION | F_USD, { C_W() }, {} },
              PanePatch{ "N_02", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "N_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Pict_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Color", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Invariant },

        { "news", { "RdtIcoNews_00^s", "RdtIcoNews_00_Home^s" }, 0x5050505, "blyt/RdtBtnNtf.bflyt", "P_PictNtf_00", 64, 56,
          MakePatch("blyt/RdtBtnNtf.bflyt", {
              PanePatch{ "P_PictNtf_00", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
              PanePatch{ "P_PictNtf_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Invariant },

        { "shop", { "RdtIcoShop^s" }, 0x5050505, "blyt/RdtBtnShop.bflyt", "P_Pict", 64, 56,
          MakePatch("blyt/RdtBtnShop.bflyt", {
              PanePatch{ "P_Pict", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
          }),
          ConsoleFirmware::Invariant },

        { "controller", { "RdtIcoCtrl_00^s" }, 0x5050505, "blyt/RdtBtnCtrl.bflyt", "P_Form", 64, 56,
          MakePatch("blyt/RdtBtnCtrl.bflyt", {
              PanePatch{ "P_Form", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
              PanePatch{ "P_Stick", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Y", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_X", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_A", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_B", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Invariant },

        { "settings", { "RdtIcoSet^s" }, 0x5050505, "blyt/RdtBtnSet.bflyt", "P_Pict", 64, 56,
          MakePatch("blyt/RdtBtnSet.bflyt", {
              PanePatch{ "P_Pict", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
          }),
          ConsoleFirmware::Invariant },

        { "power", { "RdtIcoPwrForm^s" }, 0x5050505, "blyt/RdtBtnPow.bflyt", "P_Pict_00", 64, 56,
          MakePatch("blyt/RdtBtnPow.bflyt", {
              PanePatch{ "P_Pict_00", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
          }),
          ConsoleFirmware::Invariant },

        { "nso", { "RdtIcoLR_00^s" }, 0x5050505, "blyt/RdtBtnLR.bflyt", "P_LR_00", 64, 56,
          MakePatch("blyt/RdtBtnLR.bflyt", {
              PanePatch{ "P_LR_00", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE, {}, {} },
              PanePatch{ "P_LR_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Fw11_0 },

        { "card", { "RdtIcoHomeVgc^s" }, 0x5050505, "blyt/RdtBtnVgc.bflyt", "P_Pict_00", 64, 56,
          MakePatch("blyt/RdtBtnVgc.bflyt", {
              PanePatch{ "P_Pict_00", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
              PanePatch{ "P_00", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Fw20_0 },

        { "share", { "RdtIcoHomeSplayFrame^s" }, 0x5050505, "blyt/RdtBtnSplay.bflyt", "P_Pict_00", 64, 56,
          MakePatch("blyt/RdtBtnSplay.bflyt", {
              PanePatch{ "P_Pict_00", {}, {}, {}, {64,56}, false, 0,0,0,0, F_SIZE | F_USD, { C_W() }, {} },
              PanePatch{ "N_Wave", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Pict_01", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Pict_02", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
              PanePatch{ "P_Pict_03", {}, {}, {}, {}, false, 0,0,0,0, F_VISIBLE, {}, {} },
          }),
          ConsoleFirmware::Fw20_0 },
    } },

    { "lock", {
        { "lock", { "EntIcoHome^s" }, 0x5040302, "blyt/EntBtnResumeSystemApplet.bflyt", "P_PictHome", 184, 168,
          MakePatch("blyt/EntBtnResumeSystemApplet.bflyt", {
              PanePatch{ "P_PictHome", {0,0,0}, {}, {}, {184,168}, false, 0,0,0,0, F_SIZE | F_POSITION, {}, {} },
          }),
          ConsoleFirmware::Invariant },
    } },
};

} // namespace Patches::textureReplacement

} // namespace sphaira::theme
