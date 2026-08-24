#include "theme/nxtheme.hpp"
#include "theme/json.hpp"
#include "theme/sarc.hpp"
#include "theme/yaz0.hpp"
#include "theme/bntx.hpp"
#include "defines.hpp"
#include "minizip_helper.hpp"

#include <minizip/unzip.h>
#include <exception>
#include <cstring>

namespace sphaira::theme {
namespace {

std::string_view GetStringView(const std::vector<u8>& vec) {
    return std::string_view(reinterpret_cast<const char*>(vec.data()), vec.size());
}

} // namespace

bool zip::IsZip(std::span<const u8> data) {
    return data.size() >= 4 && data[0] == 'P' && data[1] == 'K';
}

ContainerResult zip::Extract(std::span<const u8> data) {
    mz::MzSpan mz_span{data};
    zlib_filefunc64_def file_func;
    mz::FileFuncSpan(&mz_span, &file_func);

    auto zfile = unzOpen2_64("", &file_func);
    if (!zfile)
        return std::string("Failed to open zip archive");

    ON_SCOPE_EXIT(unzClose(zfile));

    FileContainer res = {};

    if (unzGoToFirstFile(zfile) != UNZ_OK)
        return std::string("Failed to read zip archive");

    do {
        char name[0x400]{};
        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(zfile, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK)
            continue;

        // 目录条目（名字以 / 结尾）跳过。
        const size_t name_len = std::strlen(name);
        if (name_len && name[name_len - 1] == '/')
            continue;

        if (unzOpenCurrentFile(zfile) != UNZ_OK)
            continue;

        ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

        auto vec = std::vector<u8>(info.uncompressed_size);
        if ((int)info.uncompressed_size != unzReadCurrentFile(zfile, vec.data(), (u32)vec.size()))
            return std::string("Failed to extract archive item: ") + name;

        res[name] = std::move(vec);
    } while (unzGoToNextFile(zfile) == UNZ_OK);

    return res;
}

ContainerResult szs::Extract(const std::vector<u8>& data) {
    if (data.size() < 4) return std::string("The provided file is too short");

    std::vector<u8> decompressed;

    if (yaz0::IsYaz0(data)) {
        try {
            decompressed = yaz0::Decompress(data);
            return szs::Extract(decompressed);
        } catch (const std::exception& e) {
            return std::string("Failed to decompress Yaz0 data: ") + e.what();
        }
    }

    try {
        auto unpacked = SARC::Unpack(data);
        return unpacked.files;
    } catch (const std::exception& e) {
        return std::string("Failed to unpack SARC data: ") + e.what();
    }
}

NxTheme NxTheme::FromError(std::string message) {
    auto res = NxTheme(FileContainer{});
    res.error = std::move(message);
    return res;
}

NxTheme NxTheme::TryLoad(const std::vector<u8>& data) {
    if (data.size() < 4)
        return NxTheme::FromError("The provided file is too small");

    auto extracted = zip::IsZip(data) ? zip::Extract(data) : szs::Extract(data);

    if (std::holds_alternative<std::string>(extracted))
        return NxTheme::FromError(std::get<std::string>(extracted));

    return NxTheme(std::move(std::get<FileContainer>(extracted)));
}

void NxTheme::initialize() {
    try {
        if (!files.count("info.json")) {
            error = "This theme does not contain the info.json manifest file.";
            return;
        }

        const auto& manifestData = files["info.json"];
        std::string manifestStr(reinterpret_cast<const char*>(manifestData.data()), manifestData.size());

        manifest = ThemeFileManifest::FromJson(manifestStr);

        if (manifest->Version <= 0) {
            error = "Parsing the metadata of this theme failed.";
            return;
        }
    } catch (const std::exception& e) {
        error = std::string("Error while loading theme data: ") + e.what();
        return;
    }
}

bool NxTheme::HasImagePart(std::string_view partName) const {
    if (files.count(std::string(partName) + ".dds")) return true;
    if (files.count(std::string(partName) + ".png")) return true;
    return false;
}

FileResult NxTheme::ConvertToDDS(const FileData& image, bool transparent, int width, int height) {
    try {
        auto dds = DDSConv::ConvertImage(image, transparent, width, height);
        if (dds.IsSuccess())
            return dds.Data;
        return dds.ErrorMessage;
    } catch (const std::exception& e) {
        return std::string("Error while converting image: ") + e.what();
    }
}

FileResult NxTheme::GetMainImage() const {
    if (files.count("image.dds")) return files.at("image.dds");
    if (!files.count("image.jpg")) return std::string("No main image found in the theme");
    return ConvertToDDS(files.at("image.jpg"), false, 1280, 720);
}

FileResult NxTheme::GetImagePart(std::string_view partName, int width, int height) const {
    auto name = std::string(partName) + ".dds";
    if (files.count(name)) return files.at(name);

    name = std::string(partName) + ".png";
    if (!files.count(name)) return std::string("Part name not found");

    const auto& data = files.at(name);
    return ConvertToDDS(data, true, width, height);
}

std::string_view NxTheme::GetMainLayout() const {
    if (!HasMainLayout()) return "";
    return GetStringView(files.at("layout.json"));
}

std::string_view NxTheme::GetCommonLayout() const {
    if (!HasCommonLayout()) return "";
    return GetStringView(files.at("common.json"));
}

ThemeFileManifest ThemeFileManifest::FromJson(std::string_view json) {
    auto j = nlohmann::json::parse(json);

    ThemeFileManifest res = { 0 };
    if (j.count("Version") && j.count("Target")) {
        res.Version = j["Version"].get<int>();
        res.Target = j["Target"].get<std::string>();
    } else {
        res.Version = -1;
        return res;
    }

    if (j.count("Author"))
        res.Author = j["Author"].get<std::string>();
    if (j.count("ThemeName"))
        res.ThemeName = j["ThemeName"].get<std::string>();
    if (j.count("LayoutInfo"))
        res.LayoutInfo = j["LayoutInfo"].get<std::string>();

    return res;
}

} // namespace sphaira::theme
