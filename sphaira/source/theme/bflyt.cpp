#include "theme/bflyt.hpp"

#include <algorithm>
#include <stack>
#include <ranges>
#include <unordered_map>

namespace sphaira::theme {

using namespace Panes;

TextureSection::TextureSection(Buffer& buf) : BasePane("txl1", buf) {
    Buffer rd(data);
    rd.ByteOrder = Endianness::LittleEndian;
    int texCount = rd.readInt32();
    u32 BaseOff = (u32)rd.Position;
    auto Offsets = rd.ReadS32Array(texCount);
    for (auto off : Offsets) {
        rd.Position = BaseOff + off;
        Textures.push_back(rd.readStr_NullTerm());
    }
}

TextureSection::TextureSection() : BasePane("txl1", 8) {}

void TextureSection::ApplyChanges(Buffer& dataWriter) {
    dataWriter.Write((s32)Textures.size());
    for (size_t i = 0; i < Textures.size(); i++)
        dataWriter.Write((s32)0);
    for (size_t i = 0; i < Textures.size(); i++) {
        u32 off = (u32)dataWriter.Position;
        dataWriter.Write(Textures[i], Buffer::BinaryString::NullTerminated);
        u32 endPos = (u32)dataWriter.Position;
        dataWriter.Position = 4 + i * 4;
        dataWriter.Write(off - 4);
        dataWriter.Position = endPos;
    }
    dataWriter.WriteAlign(4);
}

MaterialsSection::MaterialsSection(u32 version) : BasePane("mat1", 8), Version(version) {}

MaterialsSection::MaterialsSection(Buffer& reader, u32 version) : BasePane("mat1", reader), Version(version) {
    Buffer dataReader(data);
    dataReader.ByteOrder = reader.ByteOrder;
    int matCount = dataReader.readInt32();
    auto Offsets = dataReader.ReadS32Array(matCount);
    for (int i = 0; i < matCount; i++) {
        int matLen = (i == matCount - 1 ? (int)dataReader.Length() : Offsets[i + 1] - 8) - (int)dataReader.Position;
        BflytMaterial mat(dataReader.readBytes(matLen), Version, dataReader.ByteOrder);
        Materials.push_back(std::move(mat));
    }
}

void MaterialsSection::ApplyChanges(Buffer& dataWriter) {
    dataWriter.Write((s32)Materials.size());
    for (size_t i = 0; i < Materials.size(); i++)
        dataWriter.Write((s32)0);
    for (size_t i = 0; i < Materials.size(); i++) {
        u32 off = (u32)dataWriter.Position;
        dataWriter.Write(Materials[i].Write(Version, dataWriter.ByteOrder));
        u32 endPos = (u32)dataWriter.Position;
        dataWriter.Position = 4 + i * 4;
        dataWriter.Write(off + 8);
        dataWriter.Position = endPos;
    }
}

Panes::PanePtr BflytFile::operator[](const std::string& name) {
    return FindPane([name](const Panes::PanePtr& p) { return p->PaneName == name; });
}

Panes::PanePtr BflytFile::FindPane(std::function<bool(const Panes::PanePtr&)> fun) {
    auto&& b = std::find_if(PanesBegin(), PanesEnd(), fun);
    if (b == PanesEnd())
        return nullptr;
    else return *b;
}

int BflytFile::FindRootIndex(const std::string& type) {
    for (size_t i = 0; i < RootPanes.size(); i++)
        if (RootPanes[i]->name == type)
            return (int)i;
    return -1;
}

Panes::PanePtr BflytFile::FindRoot(const std::string& type) {
    int index = FindRootIndex(type);
    return index < 0 ? nullptr : RootPanes[index];
}

BflytFile::BflytFile(const std::vector<u8>& file) {
    Buffer bin(file);
    bin.ByteOrder = Endianness::LittleEndian;
    if (bin.readStr(4) != "FLYT") throw std::runtime_error("Wrong signature");
    bin.readUInt16();
    bin.readUInt16();
    Version = bin.readUInt32();
    bin.readUInt32();
    u16 sectionCount = bin.readUInt16();
    bin.readUInt16();

    Panes::PanePtr lastPane = nullptr;
    std::stack<Panes::PanePtr> currentRoot;
    auto PushPane = [&lastPane, &currentRoot, this](Panes::BasePane* ptr) {
        auto&& x = Panes::PanePtr(ptr);
        if (x->name == "pas1" || x->name == "grs1")
            currentRoot.push(lastPane);
        else if (x->name == "pae1" || x->name == "gre1")
            currentRoot.pop();
        else if (currentRoot.size() == 0)
            RootPanes.push_back(x);
        else {
            x->Parent = currentRoot.top();
            currentRoot.top()->Children.push_back(x);
        }
        lastPane = x;
    };

    for (size_t i = 0; i < sectionCount; i++) {
        std::string name = bin.readStr(4);
        if (name == "txl1")
            PushPane(new TextureSection(bin));
        else if (name == "mat1")
            PushPane(new MaterialsSection(bin, Version));
        else if (name == "pic1")
            PushPane(new Pic1Pane(bin, bin.ByteOrder));
        else if (name == "txt1")
            PushPane(new Txt1Pane(bin, bin.ByteOrder));
        else if (name == "usd1") {
            if (!lastPane)
                throw std::runtime_error("Misplaced user data section");
            lastPane->UserData = std::make_unique<Usd1Pane>(bin);
        } else if (name == "grp1")
            PushPane(new Grp1Pane(bin, Version));
        else if (name == "pan1" || name == "prt1" || name == "wnd1" || name == "bnd1")
            PushPane(new Pan1Pane(bin, bin.ByteOrder, name));
        else
            PushPane(new BasePane(name, bin));
    }
}

BflytFile::~BflytFile() {
    RootPanes.clear();
}

std::shared_ptr<TextureSection> BflytFile::GetTexSection() {
    auto p = FindRoot("txl1");
    if (!p) {
        p = PanePtr(new TextureSection());
        int t = FindRootIndex("fnl1");
        if (t >= 0)
            RootPanes.insert(RootPanes.begin() + t + 1, p);
        else
            RootPanes.insert(RootPanes.begin() + 1, p);
    }
    return std::dynamic_pointer_cast<TextureSection>(p);
}

std::shared_ptr<MaterialsSection> BflytFile::GetMatSection() {
    auto p = FindRoot("mat1");
    if (!p) {
        p = PanePtr(new MaterialsSection(Version));
        int t = FindRootIndex("txl1");
        if (t >= 0)
            RootPanes.insert(RootPanes.begin() + t + 1, p);
        else {
            t = FindRootIndex("fnl1");
            if (t >= 0)
                RootPanes.insert(RootPanes.begin() + t + 1, p);
            else
                RootPanes.insert(RootPanes.begin() + 1, p);
        }
    }
    return std::dynamic_pointer_cast<MaterialsSection>(p);
}

Panes::PanePtr BflytFile::GetRootElement() {
    return FindRoot("pan1");
}

std::shared_ptr<Grp1Pane> BflytFile::GetRootGroup() {
    auto p = FindRoot("grp1");
    if (!p) return nullptr;
    return std::dynamic_pointer_cast<Grp1Pane>(p);
}

std::vector<PanePtr> BflytFile::WritePaneListForBinary() {
    std::vector<PanePtr> res;
    std::stack<PanePtr> ToProc;
    for (auto&& p = RootPanes.rbegin(); p != RootPanes.rend(); p++)
        ToProc.push(*p);

    while (ToProc.size() > 0) {
        auto c = ToProc.top();
        ToProc.pop();
        res.push_back(c);
        if (c->Children.size() != 0) {
            ToProc.emplace(new BasePane(c->name == "grp1" ? "gre1" : "pae1", 8));
            for (auto&& p = c->Children.rbegin(); p != c->Children.rend(); p++)
                ToProc.push(*p);
            res.emplace_back(new BasePane(c->name == "grp1" ? "grs1" : "pas1", 8));
        }
    }
    return res;
}

std::vector<u8> BflytFile::SaveFile() {
    auto&& Panes = WritePaneListForBinary();

    Buffer bin;
    bin.ByteOrder = Endianness::LittleEndian;
    bin.Write("FLYT");
    bin.Write((u16)0xFEFF);
    bin.Write((u16)0x14);
    bin.Write((u32)Version);
    bin.Write((u32)0);
    u16 PaneCount = 0;
    for (const auto& pane : Panes)
        PaneCount += pane->UserData ? 2 : 1;
    bin.Write(PaneCount);
    bin.Write((u16)0);
    for (auto p : Panes)
        p->WritePane(bin);
    bin.WriteAlign(4);
    bin.Position = 0xC;
    bin.Write((u32)bin.Length());
    bin.Position = bin.Length();
    return bin.getBuffer();
}

void BflytFile::RemovePane(PanePtr& pane) {
    auto ptr = pane->Parent.lock();
    auto& c = ptr->Children;
    c.erase(std::remove(c.begin(), c.end(), pane), c.end());
}

void BflytFile::AddPane(size_t offset, PanePtr& Parent, PanePtr& pane) {
    if (offset > Parent->Children.size())
        offset = Parent->Children.size();
    Parent->Children.insert(Parent->Children.begin() + offset, pane);
}

void BflytFile::MovePane(PanePtr& pane, PanePtr& NewParent, size_t offset) {
    RemovePane(pane);
    AddPane(offset, NewParent, pane);
}

// ============================ BflytPatcher ============================

bool BflytPatcher::ClearUVData(const std::string& name) {
    auto target = lyt[name];
    if (!target || target->name != "pic1") return false;

    auto e = std::dynamic_pointer_cast<Pic1Pane>(target);
    for (auto& uv : e->UvCoords) {
        uv.TopLeft = { 0, 0 };
        uv.TopRight = { 1, 0 };
        uv.BottomLeft = { 0, 1 };
        uv.BottomRight = { 1, 1 };
    }
    return true;
}

bool BflytPatcher::ApplyLayoutPatch(const std::vector<PanePatch>& Patches) {
    for (size_t i = 0; i < Patches.size(); i++) {
        auto target = lyt[Patches[i].PaneName];
        if (!target) continue;

        auto p = Patches[i];
        auto e = std::dynamic_pointer_cast<Pan1Pane>(target);

        if (p.ApplyFlags & (u32)PanePatch::Flags::Visible)
            e->SetVisible(p.Visible);

        if (p.ApplyFlags & (u32)PanePatch::Flags::Position) {
            e->Position.X = p.Position.X;
            e->Position.Y = p.Position.Y;
            e->Position.Z = p.Position.Z;
        }
        if (p.ApplyFlags & (u32)PanePatch::Flags::Rotation) {
            e->Rotation.X = p.Rotation.X;
            e->Rotation.Y = p.Rotation.Y;
            e->Rotation.Z = p.Rotation.Z;
        }
        if (p.ApplyFlags & (u32)PanePatch::Flags::Scale) {
            e->Scale.X = p.Scale.X;
            e->Scale.Y = p.Scale.Y;
        }
        if (p.ApplyFlags & (u32)PanePatch::Flags::Size) {
            e->Size.X = p.Size.X;
            e->Size.Y = p.Size.Y;
        }

        if (p.ApplyFlags & (u32)PanePatch::Flags::OriginX)
            e->SetOriginX((OriginX)p.OriginX);
        if (p.ApplyFlags & (u32)PanePatch::Flags::OriginY)
            e->SetOriginY((OriginY)p.OriginY);
        if (p.ApplyFlags & (u32)PanePatch::Flags::ParentOriginX)
            e->SetParentOriginX((OriginX)p.ParentOriginX);
        if (p.ApplyFlags & (u32)PanePatch::Flags::ParentOriginY)
            e->SetParentOriginY((OriginY)p.ParentOriginY);

        if (e->name == "pic1") {
            auto ee = std::dynamic_pointer_cast<Pic1Pane>(e);
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific0) ee->ColorTopLeft = RGBAColor(p.PaneSpecific0());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific1) ee->ColorTopRight = RGBAColor(p.PaneSpecific1());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific2) ee->ColorBottomLeft = RGBAColor(p.PaneSpecific2());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific3) ee->ColorBottomRight = RGBAColor(p.PaneSpecific3());
        }

        if (e->name == "txt1") {
            auto ee = std::dynamic_pointer_cast<Txt1Pane>(e);
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific0) ee->FontTopColor = RGBAColor(p.PaneSpecific0());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific1) ee->ShadowTopColor = RGBAColor(p.PaneSpecific1());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific2) ee->FontBottomColor = RGBAColor(p.PaneSpecific2());
            if (p.ApplyFlags & (u32)PanePatch::Flags::PaneSpecific3) ee->ShadowBottomColor = RGBAColor(p.PaneSpecific3());
        }

        if ((p.ApplyFlags & (u32)PanePatch::Flags::UsdPatches) && target->UserData) {
            auto usd = dynamic_cast<Usd1Pane*>(target->UserData.get());
            for (const auto& patch : p.UsdPatches) {
                auto v = usd->FindName(patch.PropName);
                if (v == nullptr)
                    usd->AddProperty(patch.PropName, patch.PropValues, (Panes::Usd1Pane::ValueType)patch.type);
                else if (v && v->ValueCount == patch.PropValues.size() && (int)v->type && patch.type)
                    v->value = patch.PropValues;
            }
        }
    }
    return true;
}

bool BflytPatcher::ApplyMaterialsPatch(const std::vector<MaterialPatch>& Patches) {
    if (Patches.size() == 0) return true;
    auto mats = lyt.GetMatSection();
    if (!mats) return false;
    for (const auto& p : Patches) {
        const auto filter_condition = [&name = p.MaterialName](const auto& m) { return m.Name == name; };

        for (auto& target : mats->Materials | std::views::filter(filter_condition)) {
            if (p.ForegroundColor != "")
                target.ForegroundColor = (u32)std::stoul(p.ForegroundColor, 0, 16);
            if (p.BackgroundColor != "")
                target.BackgroundColor = (u32)std::stoul(p.BackgroundColor, 0, 16);

            std::unordered_map<std::string, int> texToMapId;
            for (u32 i = 0; i < target.Textures.size(); i++) {
                auto id = target.Textures[i].TextureId;
                texToMapId[lyt.GetTexSection()->Textures.at(id)] = i;
            }

            for (const auto& rp : p.Refs) {
                if (!texToMapId.count(rp.Name)) continue;
                auto& tex = target.Textures[texToMapId.at(rp.Name)];
                if (rp.WrapS) tex.WrapS = *rp.WrapS;
                if (rp.WrapT) tex.WrapT = *rp.WrapT;
            }

            for (const auto& tp : p.Transforms) {
                if (!texToMapId.count(tp.Name)) continue;
                auto& tf = target.TextureTransformations[texToMapId.at(tp.Name)];

#define set(x) if (tp.x) tf.x = *tp.x
                set(X);
                set(Y);
                set(ScaleX);
                set(ScaleY);
                set(Rotation);
#undef set
            }
        }
    }
    return true;
}

bool BflytPatcher::AddGroupNames(const std::vector<ExtraGroup>& Groups) {
    if (Groups.size() == 0) return true;
    if (!lyt.GetRootGroup()) return false;

    std::vector<std::string> groupNames;
    std::vector<std::string> paneNames;
    lyt.FindPane([&groupNames, &paneNames](const PanePtr& cur) {
        if (cur->PaneName == "") return false;
        if (cur->name == "grp1")
            groupNames.push_back(std::dynamic_pointer_cast<Grp1Pane>(cur)->GroupName);
        else
            paneNames.push_back(cur->PaneName);
        return false;
    });

    for (const auto& g : Groups) {
        if (Utils::IndexOf(groupNames, g.GroupName) != SIZE_MAX) continue;
        for (const auto& s : g.Panes)
            if (Utils::IndexOf(paneNames, s) == SIZE_MAX) return false;
        auto toAdd = new Grp1Pane(lyt.Version);
        toAdd->GroupName = g.GroupName;
        toAdd->Panes = g.Panes;
        lyt.GetRootGroup()->Children.emplace_back(toAdd);
    }
    return true;
}

bool BflytPatcher::PatchTextureName(const std::string& original, const std::string& _new) {
    bool patchedSomething = false;
    auto texSection = lyt.GetTexSection();
    if (texSection == nullptr)
        throw std::runtime_error("this layout doesn't have any texture section (?)");
    for (size_t i = 0; i < texSection->Textures.size(); i++) {
        if (texSection->Textures[i] == original) {
            patchedSomething = true;
            texSection->Textures[i] = _new;
        }
    }
    return patchedSomething;
}

u16 BflytPatcher::AddBgMat(const std::string& texName) {
    auto MatSect = lyt.GetMatSection();
    auto texSection = lyt.GetTexSection();
    if (!MatSect || !texSection) return false;
    size_t texIndex = Utils::IndexOf(texSection->Textures, texName);
    if (texIndex == SIZE_MAX) {
        texIndex = texSection->Textures.size();
        texSection->Textures.push_back(texName);
    }

    {
        Buffer bin;
        bin.ByteOrder = Endianness::LittleEndian;
        bin.Write("P_Custm", Buffer::BinaryString::NullTerminated);
        for (size_t i = 0; i < 0x14; i++)
            bin.Write((u8)0);
        bin.Write((s32)0x15);
        bin.Write((s32)0x8040200);
        bin.Write((s32)0);
        bin.Write((u32)0xFFFFFFFF);
        bin.Write((u16)texIndex);
        bin.Write((u16)0x0);
        for (size_t i = 0; i < 0xC; i++)
            bin.Write((u8)0);
        bin.Write((float)1);
        bin.Write((float)1);
        for (size_t i = 0; i < 0x10; i++)
            bin.Write((u8)0);
        MatSect->Materials.push_back(BflytMaterial{ bin.getBuffer(), lyt.Version, bin.ByteOrder });
    }
    return u16(MatSect->Materials.size() - 1);
}

bool BflytPatcher::AddBgPanel(PanePtr target, const std::string& TexName, const std::string& Pic1Name) {
    if (Pic1Name.length() > 0x18)
        throw std::runtime_error("Pic1Name should not be longer than 24 chars");
    auto BgPane = new BasePane("pic1", 0x8);
    int TexIndex = AddBgMat(TexName);
    {
        Buffer bin;
        bin.ByteOrder = Endianness::LittleEndian;
        bin.Write((u8)0x01);
        bin.Write((u8)0x00);
        bin.Write((u8)0xFF);
        bin.Write((u8)0x04);
        bin.Write(Pic1Name);
        int zerCount = (int)Pic1Name.length();
        while (zerCount++ < 0x38)
            bin.Write((u8)0x00);
        bin.Write((float)1);
        bin.Write((float)1);
        bin.Write((float)1280);
        bin.Write((float)720);
        bin.Write((u32)0xFFFFFFFF);
        bin.Write((u32)0xFFFFFFFF);
        bin.Write((u32)0xFFFFFFFF);
        bin.Write((u32)0xFFFFFFFF);
        bin.Write((u16)TexIndex);
        bin.Write((u16)1);
        bin.Write((u32)0);
        bin.Write((u32)0);
        bin.Write((float)1);
        bin.Write((u32)0);
        bin.Write((u32)0);
        bin.Write((float)1);
        bin.Write((float)1);
        bin.Write((float)1);
        BgPane->data = bin.getBuffer();
    }
    auto ptr = target->Parent.lock();
    auto& targetCList = ptr->Children;
    targetCList.emplace(targetCList.begin() + Utils::IndexOf(targetCList, target), BgPane);
    return true;
}

bool BflytPatcher::PatchBgLayout(const PatchTemplate& patch) {
    if (lyt[patch.PatchIdentifier]) return true;
    if (auto p = lyt["3x3lxBG"]) {
        lyt.RemovePane(p);
        lyt.GetTexSection()->Textures[0] = "White1x1^r";
        lyt.GetMatSection()->Materials.erase(lyt.GetMatSection()->Materials.begin() + 1);
    }

    PanePtr target = nullptr;
    for (const auto& tname : patch.targetPanels) {
        auto p = lyt[tname];
        if (!p) continue;
        if (!target) target = p;
        if (patch.DirectPatchPane) {
            auto m = AddBgMat(patch.MaintextureName);
            if (p->name != "pic1") throw std::runtime_error("Expected a picture pane !");
            std::dynamic_pointer_cast<Pic1Pane>(p)->MaterialIndex = m;
        } else if (!patch.NoRemovePanel) {
            auto t = std::dynamic_pointer_cast<Pan1Pane>(p);
            t->Position.X = 5000;
            t->Position.Y = 60000;
        }
    }
    if (!target)
        return false;

    if (!patch.DirectPatchPane)
        return AddBgPanel(target, patch.MaintextureName, patch.PatchIdentifier);
    else return true;
}

bool BflytPatcher::PanePullToFront(const std::string& paneName) {
    auto target = lyt[paneName];
    if (!target) return false;
    auto ptr = target->Parent.lock();
    if (!ptr) return false;
    lyt.MovePane(target, ptr, 0);
    return true;
}

bool BflytPatcher::PanePushBack(const std::string& paneName) {
    auto target = lyt[paneName];
    if (!target) return false;
    auto ptr = target->Parent.lock();
    if (!ptr) return false;
    lyt.MovePane(target, ptr, ptr->Children.size());
    return true;
}

} // namespace sphaira::theme
