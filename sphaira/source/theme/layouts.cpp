#include "theme/patches.hpp"
#include "theme/json.hpp"

#include <algorithm>
#include <cctype>

namespace sphaira::theme {

// ============================ 布局 JSON 反序列化 ============================

static void from_json(const nlohmann::json& j, Vector2& p) {
    p = {};
    if (j.count("X")) j.at("X").get_to(p.X);
    if (j.count("Y")) j.at("Y").get_to(p.Y);
}

static void from_json(const nlohmann::json& j, Vector3& p) {
    p = {};
    if (j.count("X")) j.at("X").get_to(p.X);
    if (j.count("Y")) j.at("Y").get_to(p.Y);
    if (j.count("Z")) j.at("Z").get_to(p.Z);
}

static void from_json(const nlohmann::json& j, UsdPatch& p) {
    p = {};
    j.at("PropName").get_to(p.PropName);
    if (j.count("PropValues")) j.at("PropValues").get_to(p.PropValues);
    if (j.count("type")) j.at("type").get_to(p.type);
}

static void from_json(const nlohmann::json& j, PanePatch& p) {
    p = {};
    j.at("PaneName").get_to(p.PaneName);

#define assign_member(jname, field, flag) do { \
        if (j.count(jname)) { j.at(jname).get_to(field); p.ApplyFlags |= (u32)PanePatch::Flags::flag; } \
    } while (0)

    assign_member("Position", p.Position, Position);
    assign_member("Rotation", p.Rotation, Rotation);
    assign_member("Scale", p.Scale, Scale);
    assign_member("Size", p.Size, Size);
    assign_member("Visible", p.Visible, Visible);
    assign_member("OriginX", p.OriginX, OriginX);
    assign_member("OriginY", p.OriginY, OriginY);
    assign_member("ParentOriginX", p.ParentOriginX, ParentOriginX);
    assign_member("ParentOriginY", p.ParentOriginY, ParentOriginY);

    assign_member("ColorTL", p.PaneSpecific0(), PaneSpecific0);
    assign_member("ColorTR", p.PaneSpecific1(), PaneSpecific1);
    assign_member("ColorBL", p.PaneSpecific2(), PaneSpecific2);
    assign_member("ColorBR", p.PaneSpecific3(), PaneSpecific3);

    assign_member("UsdPatches", p.UsdPatches, UsdPatches);

#undef assign_member
}

static void from_json(const nlohmann::json& j, ExtraGroup& p) {
    p = {};
    j.at("GroupName").get_to(p.GroupName);
    if (j.count("Panes")) j.at("Panes").get_to(p.Panes);
}

static void from_json(const nlohmann::json& j, MaterialPatch::TexReference& p) {
    p = {};
    j.at("Name").get_to(p.Name);
    if (j.count("WrapS")) p.WrapS = j.at("WrapS").get<u8>();
    if (j.count("WrapT")) p.WrapT = j.at("WrapT").get<u8>();
}

static void from_json(const nlohmann::json& j, MaterialPatch::TexTransform& p) {
    p = {};
    j.at("Name").get_to(p.Name);
    if (j.count("X")) p.X = j.at("X").get<float>();
    if (j.count("Y")) p.Y = j.at("Y").get<float>();
    if (j.count("Rotation")) p.Rotation = j.at("Rotation").get<float>();
    if (j.count("ScaleX")) p.ScaleX = j.at("ScaleX").get<float>();
    if (j.count("ScaleY")) p.ScaleY = j.at("ScaleY").get<float>();
}

static void from_json(const nlohmann::json& j, MaterialPatch& p) {
    p = {};
    j.at("MaterialName").get_to(p.MaterialName);
    if (j.count("ForegroundColor")) j.at("ForegroundColor").get_to(p.ForegroundColor);
    if (j.count("BackgroundColor")) j.at("BackgroundColor").get_to(p.BackgroundColor);
    if (j.count("Refs")) j.at("Refs").get_to(p.Refs);
    if (j.count("Transforms")) j.at("Transforms").get_to(p.Transforms);
}

static void from_json(const nlohmann::json& j, LayoutFilePatch& p) {
    p = {};
    j.at("FileName").get_to(p.FileName);
    if (j.count("Patches")) j.at("Patches").get_to(p.Patches);
    if (j.count("AddGroups")) j.at("AddGroups").get_to(p.AddGroups);
    if (j.count("Materials")) j.at("Materials").get_to(p.Materials);
    if (j.count("PushBackPanes")) j.at("PushBackPanes").get_to(p.PushBackPanes);
    if (j.count("PullFrontPanes")) j.at("PullFrontPanes").get_to(p.PullFrontPanes);
}

static void from_json(const nlohmann::json& j, AnimFilePatch& p) {
    p = {};
    j.at("FileName").get_to(p.FileName);
    j.at("AnimJson").get_to(p.AnimJson);
}

static void from_json(const nlohmann::json& j, LayoutPatch& p) {
    p = {};
    if (j.count("PatchName")) j.at("PatchName").get_to(p.PatchName);
    if (j.count("AuthorName")) j.at("AuthorName").get_to(p.AuthorName);
    if (j.count("Files")) j.at("Files").get_to(p.Files);
    if (j.count("Anims")) j.at("Anims").get_to(p.Anims);
    if (j.count("PatchAppletColorAttrib")) j.at("PatchAppletColorAttrib").get_to(p.PatchAppletColorAttrib);
    if (j.count("ID")) j.at("ID").get_to(p.ID);
    if (j.count("HideOnlineBtn")) j.at("HideOnlineBtn").get_to(p.HideOnlineBtn);
    else p.HideOnlineBtn = true;
    if (j.count("Ready8X")) j.at("Ready8X").get_to(p.Obsolete_Ready8X);
    if (j.count("TargetFirmware")) j.at("TargetFirmware").get_to(p.TargetFirmware);
    else p.TargetFirmware = (int)ConsoleFirmware::Fw11_0;
}

LayoutPatch Patches::LoadLayout(const std::string_view jsn) {
    if (jsn.empty() || jsn.find_first_not_of(" \t\n\r") == std::string_view::npos)
        return {};
    return nlohmann::json::parse(jsn).get<LayoutPatch>();
}

} // namespace sphaira::theme
