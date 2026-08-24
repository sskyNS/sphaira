#include "theme/dump.hpp"
#include "theme/common.hpp"

#include "defines.hpp"
#include "utils/devoptab.hpp"
#include "yati/nx/ncm.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::theme {
namespace {

constexpr const char* DUMP_BASE = "/themes/sphaira/dump";

// 读取挂载点下的一个文件到 vector。
bool ReadMountedFile(const fs::FsPath& root, const char* rel, std::vector<u8>& out) {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s", root.s, rel);

    std::FILE* f = std::fopen(path.s, "rb");
    if (!f) {
        return false;
    }
    ON_SCOPE_EXIT(std::fclose(f));

    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        return false;
    }

    out.resize(static_cast<size_t>(len));
    return std::fread(out.data(), 1, out.size(), f) == out.size();
}

// dump 单个 title 的全部 szs 文件。
bool DumpTitle(NcmContentStorage& cs, NcmContentMetaDatabase& db, u64 title_id, std::string& error) {
    const auto szs_files = ThemeTargetInfo::GetTargetsForTitleId(title_id);
    if (szs_files.empty()) {
        return true; // 该 title 没有需要 dump 的主题。
    }

    NcmContentMetaKey key{};
    s32 meta_total{}, meta_written{};
    const auto rc = ncmContentMetaDatabaseList(
        &db, &meta_total, &meta_written, &key, 1,
        NcmContentMetaType_SystemProgram, title_id, title_id, title_id, NcmContentInstallType_Full);
    if (R_FAILED(rc) || meta_total != 1 || meta_written != 1) {
        error = "Failed to locate content meta for " + ThemeTargetInfo::TitleIdToString(title_id);
        return false;
    }

    ncm::ContentMeta content_meta;
    if (R_FAILED(ncm::GetContentMeta(&db, &key, content_meta))) {
        error = "Failed to get content meta for " + ThemeTargetInfo::TitleIdToString(title_id);
        return false;
    }

    std::vector<NcmContentInfo> infos;
    if (R_FAILED(ncm::GetContentInfos(&db, &key, content_meta.header, infos))) {
        error = "Failed to list contents for " + ThemeTargetInfo::TitleIdToString(title_id);
        return false;
    }

    const NcmContentInfo* program = nullptr;
    for (const auto& info : infos) {
        if (info.content_type == NcmContentType_Program) {
            program = &info;
            break;
        }
    }
    if (!program) {
        error = "Program NCA not found for " + ThemeTargetInfo::TitleIdToString(title_id);
        return false;
    }

    // 挂载 Program NCA。
    fs::FsPath root;
    if (R_FAILED(devoptab::MountNcaNcm(&cs, &program->content_id, root))) {
        error = "Failed to mount NCA " + ThemeTargetInfo::TitleIdToString(title_id) + "（请确认 /switch/prod.keys 内容完整且包含所需 header_key/titlekek）";
        return false;
    }
    ON_SCOPE_EXIT(devoptab::UmountNeworkDevice(root));

    const std::string title_id_str = ThemeTargetInfo::TitleIdToString(title_id);

    for (const auto& szs : szs_files) {
        // szs 形如 "/lyt/ResidentMenu.szs"。
        const std::string rel = "RomFS" + szs;

        std::vector<u8> data;
        if (!ReadMountedFile(root, rel.c_str(), data)) {
            error = "Failed to read " + rel + " from " + root.s;
            return false;
        }

        fs::FsPath out_path = std::string(DUMP_BASE) + "/" + title_id_str + szs;
        fs::FsNativeSd fs;
        fs.CreateDirectoryRecursivelyWithPath(out_path);
        if (R_FAILED(fs.write_entire_file(out_path, data))) {
            error = "Failed to write " + std::string(out_path.s);
            return false;
        }

        log_write("[theme_dump] dumped %s\n", out_path.s);
    }

    return true;
}

} // namespace

bool DumpSystemThemes(std::string& error) {
    // 解包系统主题的 Program NCA 需要 prod.keys 解密 NCA 头与 RomFS。
    if (!fs::FileExists(fs::FsPath{"/switch/prod.keys"})) {
        error = "缺少 prod.keys：请把 prod.keys 放到 /switch/prod.keys 后再按 Y 重新 dump";
        return false;
    }

    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};

    if (R_FAILED(ncmOpenContentStorage(&cs, NcmStorageId_BuiltInSystem))) {
        error = "Failed to open system content storage";
        return false;
    }
    ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

    if (R_FAILED(ncmOpenContentMetaDatabase(&db, NcmStorageId_BuiltInSystem))) {
        error = "Failed to open system content meta database";
        return false;
    }
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

    // 需要 dump 的三个内置 title：qlaunch / user page / psl。
    constexpr u64 targets[] = {
        ThemeTargetInfo::QlaunchID,
        ThemeTargetInfo::UserPageID,
        ThemeTargetInfo::PslID,
    };

    for (const auto title_id : targets) {
        if (!DumpTitle(cs, db, title_id, error)) {
            return false;
        }
    }

    return true;
}

} // namespace sphaira::theme
