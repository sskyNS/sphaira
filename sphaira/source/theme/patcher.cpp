#include "theme/patcher.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_set>

namespace sphaira::theme {

std::string GeneratePatchListString(const std::vector<PatchTemplate>& templates) {
    std::string FileList = "";
    for (auto p : templates) {
        FileList += "[" + p.FirmName + "] " + p.TemplateName + " : the file is called " + p.SzsName + " from title " + p.TitleId + "\n";
    }
    return FileList;
}

// ============================ Compatibility ============================

namespace Compatibility {

CompatIssue CompatIssue::MissingPane(std::string_view fileName, std::string_view paneName, std::string_view additional, bool critical) {
    return {
        .FileName = std::string(fileName),
        .ItemName = std::string(paneName),
        .AdditionalInfo = std::string(additional),
        .Type = ProblemType::MissingPane,
        .Severity = critical ? ProblemSeverity::Critical : ProblemSeverity::AutoIgnored
    };
}

CompatIssue CompatIssue::Uncertain(std::string_view fileName, std::string_view itemName, std::string_view additional) {
    return {
        .FileName = std::string(fileName),
        .ItemName = std::string(itemName),
        .AdditionalInfo = std::string(additional),
        .Type = ProblemType::Uncertain,
        .Severity = ProblemSeverity::AutoIgnored
    };
}

CompatIssue CompatIssue::MissingGroup(std::string_view fileName, std::string_view groupName) {
    return {
        .FileName = std::string(fileName),
        .ItemName = std::string(groupName),
        .AdditionalInfo = std::string(),
        .Type = ProblemType::MissingGroup,
        .Severity = ProblemSeverity::Critical
    };
}

std::string LayoutNameForAnimation(std::string_view animationName) {
    auto parts = animationName.find_first_of('/');
    if (parts == std::string_view::npos)
        return std::string();

    auto onlyName = animationName.substr(parts + 1);
    parts = onlyName.find_first_of('_');
    if (parts == std::string_view::npos)
        return std::string();

    return "blyt/" + std::string(onlyName.substr(0, parts)) + ".bflyt";
}

void CheckAnimationCompatibility(std::vector<CompatIssue>& res, const LayoutPatch& layout, const SARC::SarcData& szs, std::string_view animName, const Bflan& bflan) {
    auto bflytName = LayoutNameForAnimation(animName);
    if (bflytName.empty() || !szs.files.count(bflytName)) {
        res.push_back(CompatIssue::Uncertain(animName, bflytName, "Unknown bflyt file"));
        return;
    }

    std::unordered_set<std::string> paneNames = {};
    std::unordered_set<std::string> groupNames = {};

    auto bflyt = std::make_unique<BflytFile>(szs.files.at(bflytName));
    auto paneIterator = bflyt->PanesBegin();
    while (paneIterator != bflyt->PanesEnd()) {
        auto pane = *paneIterator;
        if (pane->PaneName != "")
            paneNames.insert(pane->PaneName);

        auto asGrp = std::dynamic_pointer_cast<Panes::Grp1Pane>(pane);
        if (asGrp)
            groupNames.insert(asGrp->GroupName);

        ++paneIterator;
    }

    std::unordered_set<std::string> addedGroups = {};
    for (const auto& patch : layout.Files) {
        if (patch.FileName != bflytName)
            continue;
        for (auto& grp : patch.AddGroups)
            addedGroups.insert(grp.GroupName);
    }

    auto patData = bflan.FindSectionByType<Pat1Section>();
    if (patData)
        for (const auto& group : patData->Groups) {
            if (groupNames.count(group) == 0 && addedGroups.count(group) == 0)
                res.push_back(CompatIssue::MissingGroup(bflytName, group));
        }

    auto paiData = bflan.FindSectionByType<Pai1Section>();
    if (paiData)
        for (const auto& entry : paiData->Entries) {
            if (entry.Target == PaiEntry::AnimationTarget::Pane)
                if (!paneNames.count(entry.Name))
                    res.push_back(CompatIssue::MissingPane(animName, entry.Name, "Animation target", true));
        }
}

} // namespace Compatibility

// ============================ SzsPatcher ============================

SzsPatcher::SzsPatcher(SARC::SarcData&& s) : sarc(s) { Initialize(); }
SzsPatcher::SzsPatcher(SARC::SarcData& s) : sarc(s) { Initialize(); }

void SzsPatcher::Initialize() {
    currentFirmware = hos::Version.ToFirmwareEnum();
    currentTemplate = DetectSarc(sarc);

    if (!currentTemplate) {
        nxthemePartName = "";
    } else {
        ThemeTargetInfo::FindBySzsName(currentTemplate->SzsName, nxthemePartName);
    }
}

SzsPatcher::~SzsPatcher() {
    if (bntx)
        delete bntx;
}

const SARC::SarcData& SzsPatcher::GetSarc() { return sarc; }

SARC::SarcData& SzsPatcher::GetFinalSarc() {
    SaveBntx();
    return sarc;
}

QuickBntx& SzsPatcher::OpenBntx() {
    if (bntx) return *bntx;
    Buffer Reader(sarc.files["timg/__Combined.bntx"]);
    bntx = new QuickBntx(Reader);
    return *bntx;
}

void SzsPatcher::SaveBntx() {
    if (!bntx) return;
    sarc.files["timg/__Combined.bntx"] = bntx->Write();
    delete bntx;
    bntx = nullptr;
}

void SzsPatcher::ApplyRawPatch(const std::optional<LayoutPatch>& p) {
    if (!p) return;
    const auto& patch = *p;
    ApplyRawPatch(&patch);
}

void SzsPatcher::ApplyRawPatch(const LayoutPatch* p) {
    if (!p) return;
    for (auto& f : p->Files) ApplyLayoutPatch(f);
    for (auto& f : p->Anims) ApplyAnimPatch(f);
}

bool SzsPatcher::ApplyAnimPatch(const AnimFilePatch& p) {
    if (!sarc.files.count(p.FileName))
        return false;

    if (!FirmwareTargetBflanVersion) {
        auto bflan = std::make_unique<Bflan>(sarc.files[p.FileName]);
        FirmwareTargetBflanVersion = bflan->Version;
    }

    auto bflan = BflanDeserializer::FromJson(p.AnimJson);
    bflan->Version = *FirmwareTargetBflanVersion;
    bflan->byteOrder = Endianness::LittleEndian;
    sarc.files[p.FileName] = bflan->WriteFile();

    return true;
}

bool SzsPatcher::ApplyLayoutPatch(const LayoutFilePatch& p) {
    if (!sarc.files.count(p.FileName))
        return false;

    BflytFile _target(sarc.files[p.FileName]);
    BflytPatcher target(_target);
    target.ApplyMaterialsPatch(p.Materials);
    auto res = target.ApplyLayoutPatch(p.Patches);
    if (res != true)
        return res;

    if (EnableAnimations) {
        res = target.AddGroupNames(p.AddGroups);
        if (res != true)
            return res;
    }

    for (const auto& n : p.PullFrontPanes)
        target.PanePullToFront(n);
    for (const auto& n : p.PushBackPanes)
        target.PanePushBack(n);

    sarc.files[p.FileName] = _target.SaveFile();
    return true;
}

bool SzsPatcher::PatchLayouts(const LayoutPatch& patch) {
    return PatchLayouts(patch, nxthemePartName);
}

bool SzsPatcher::PatchLayouts() {
    auto fakePatch = LayoutPatch{ .PatchName = "stub" };
    return PatchLayouts(fakePatch, nxthemePartName);
}

int SzsPatcher::FilterIncompatibleAnimations(LayoutPatch& p) {
    std::unordered_set<std::string> remove{};
    std::vector<Compatibility::CompatIssue> issues{};

    for (const auto& anim : p.Anims) {
        issues.clear();
        auto bflan = BflanDeserializer::FromJson(anim.AnimJson);
        Compatibility::CheckAnimationCompatibility(issues, p, sarc, anim.FileName, *bflan);

        for (auto& issue : issues)
            if (issue.Severity == Compatibility::ProblemSeverity::Critical)
                remove.insert(anim.FileName);
    }

    std::erase_if(p.Anims, [&](const auto& e) {
        return remove.count(e.FileName);
    });

    return static_cast<int>(remove.size());
}

bool SzsPatcher::PatchLayouts(const LayoutPatch& original_patch, const std::string& partName) {
    auto patch = original_patch;

    bool useLegacyFixes = false;
    bool useModernFixes = false;
    bool appletPositionFixes = false;
    bool onlineBtnFix = false;

    TotalNonCompatibleFixes = 0;

    if (CompatFixes == LayoutCompatibilityOption::Firmware10 && partName == "home")
        patch.HideOnlineBtn = true;

    if (CompatFixes == LayoutCompatibilityOption::Firmware11 && partName == "home") {
        patch.HideOnlineBtn = false;
        patch.TargetFirmware = static_cast<int>(ConsoleFirmware::Fw11_0);
    }

    if (CompatFixes != LayoutCompatibilityOption::DisableFixes) {
        useLegacyFixes = currentFirmware != ConsoleFirmware::Invariant && patch.UsesOldFixes();
        useModernFixes = !useLegacyFixes && patch.ID != "";
        appletPositionFixes = partName == "home" && NewFirmFixes::ShouldApplyAppletPositionFix(patch, currentFirmware);
        onlineBtnFix = partName == "home" && patch.HideOnlineBtn;
    }

    if (partName == "home" && patch.PatchAppletColorAttrib)
        PatchBntxTextureAttribs({
            { "RdtIcoPvr_00^s",       0x2000000 },
            { "RdtIcoNews_00^s",      0x2000000 }, { "RdtIcoNews_01^s",      0x2000000 },
            { "RdtIcoNews_00_Home^s", 0x2000000 }, { "RdtIcoNews_01_Home^s", 0x2000000 },
            { "RdtIcoSet^s",          0x2000000 },
            { "RdtIcoShop^s",         0x2000000 },
            { "RdtIcoCtrl_00^s",      0x2000000 }, { "RdtIcoCtrl_01^s",      0x2000000 }, { "RdtIcoCtrl_02^s",      0x2000000 },
            { "RdtIcoPwrForm^s",      0x2000000 },
        });

    if (appletPositionFixes)
        ApplyRawPatch(NewFirmFixes::GetAppletsPositionFix(currentFirmware));

    if (onlineBtnFix)
        ApplyRawPatch(NewFirmFixes::GetLegacyAppletButtonsFix(currentFirmware));

    std::optional<LayoutPatch> modern_fix = std::nullopt;
    if (useModernFixes)
        modern_fix = NewFirmFixes::GetFix(patch, currentFirmware);

    if (CompatFixes != LayoutCompatibilityOption::DisableFixes)
        TotalNonCompatibleFixes += FilterIncompatibleAnimations(patch);

    ApplyRawPatch(&patch);

    if (useLegacyFixes)
        ApplyRawPatch(NewFirmFixes::GetFixLegacy(patch.PatchName, currentFirmware, partName));

    if (useModernFixes)
        ApplyRawPatch(modern_fix);

    return true;
}

static bool StrEndsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool StrStartsWith(const std::string& str, const std::string& prefix) {
    return str.find(prefix, 0) == 0;
}

bool SzsPatcher::PatchMainBG(const std::vector<u8>& DDS) {
    if (!currentTemplate)
        return false;

    auto& templ = *currentTemplate;

    BflytFile _MainFile(sarc.files[templ.MainLayoutName]);
    BflytPatcher MainFile(_MainFile);

    auto res = MainFile.PatchBgLayout(templ);
    if (!res) return res;

    QuickBntx& q = OpenBntx();
    if (q.Rlt.size() != 0x80)
        return false;

    auto dds = DDSEncoder::LoadDDS(DDS);
    q.ReplaceTex(templ.MaintextureName, dds);

    auto replaceWith = q.FindTex(templ.SecondaryTexReplace) ? templ.SecondaryTexReplace : "";

    if (replaceWith == "") {
        auto v = q.Textures | std::views::filter([](const auto& d) { return d.Name().starts_with("White"); });
        if (v.empty())
            return false;
        replaceWith = v.front().Name();
    }

    sarc.files[templ.MainLayoutName] = _MainFile.SaveFile();
    for (const auto& t : sarc.files) {
        auto& f = t.first;
        if (!StrEndsWith(f, ".bflyt") || !StrStartsWith(f, "blyt/") || f == templ.MainLayoutName) continue;
        BflytFile _curTarget(sarc.files[f]);
        BflytPatcher curTarget(_curTarget);
        if (curTarget.PatchTextureName(templ.MaintextureName, replaceWith))
            sarc.files[f] = _curTarget.SaveFile();
    }

    return true;
}

bool SzsPatcher::PatchBntxTexture(const std::vector<u8>& DDS, const std::vector<std::string>& texNames, u32 ChannelData) {
    QuickBntx& q = OpenBntx();
    if (q.Rlt.size() != 0x80)
        return false;

    try {
        for (const auto& texName : texNames) {
            auto tex = q.FindTex(texName);
            if (!tex) continue;

            auto dds = DDSEncoder::LoadDDS(DDS);
            q.ReplaceTex(texName, dds);
            if (ChannelData != 0xFFFFFFFF)
                q.FindTex(texName)->ChannelTypes = ChannelData;
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool SzsPatcher::PatchAppletIcon(const std::vector<u8>& DDS, const std::string& texName) {
    if (nxthemePartName == "")
        return false;

    if (!Patches::textureReplacement::NxNameToList.count(nxthemePartName))
        return false;

    const auto& list = Patches::textureReplacement::NxNameToList[nxthemePartName];
    auto replacement = std::find_if(list.begin(), list.end(), [&texName](const TextureReplacement& t) {
        return t.NxThemeName == texName;
    });

    if (replacement == list.end())
        return false;

    if (currentFirmware < replacement->MinFirmware)
        return true;

    auto res = ApplyLayoutPatch(replacement->Patch);
    if (!res) return res;

    PatchBntxTexture(DDS, replacement->BntxNames, replacement->NewColorFlags);

    BflytFile _curTarget{ sarc.files[replacement->FileName] };
    BflytPatcher curTarget(_curTarget);

    curTarget.ClearUVData(replacement->PaneName);
    sarc.files[replacement->FileName] = _curTarget.SaveFile();

    return true;
}

bool SzsPatcher::PatchBntxTextureAttribs(const std::vector<BntxTexAttribPatch>& patches) {
    QuickBntx& q = OpenBntx();
    if (q.Rlt.size() != 0x80)
        return false;

    try {
        for (const auto& patch : patches) {
            auto tex = q.FindTex(patch.TargetTexutre);
            if (tex) tex->ChannelTypes = patch.ChannelData;
        }
    } catch (...) {
        return false;
    }
    return true;
}

const std::optional<PatchTemplate>& SzsPatcher::DetectedSarc() {
    return currentTemplate;
}

std::optional<PatchTemplate> SzsPatcher::DetectSarc(const SARC::SarcData& sarc) {
    if (!sarc.files.count("timg/__Combined.bntx"))
        return std::nullopt;

    for (auto p : Patches::DefaultTemplates) {
        if (!sarc.files.count(p.MainLayoutName))
            continue;

        bool isTarget = true;
        for (std::string s : p.FnameIdentifier) {
            if (!sarc.files.count(s)) {
                isTarget = false;
                break;
            }
        }

        if (!isTarget) continue;

        for (std::string s : p.FnameNotIdentifier) {
            if (sarc.files.count(s)) {
                isTarget = false;
                break;
            }
        }

        if (!isTarget) continue;

        return p;
    }

    return std::nullopt;
}

} // namespace sphaira::theme
