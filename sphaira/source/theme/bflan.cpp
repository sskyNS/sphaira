#include "theme/bflan.hpp"
#include "theme/json.hpp"

#include <stdexcept>

namespace sphaira::theme {

KeyFrame::KeyFrame() {}

KeyFrame::KeyFrame(Buffer& bin, u16 DataType) {
    Frame = bin.readFloat();
    if (DataType == 2) {
        Value = bin.readFloat();
        Blend = bin.readFloat();
    } else if (DataType == 1) {
        Value = (float)bin.readInt16();
        Blend = (float)bin.readInt16();
    } else throw std::runtime_error("Unexpected data type for keyframe");
}

PaiTagEntry::PaiTagEntry() {}

PaiTagEntry::PaiTagEntry(Buffer& bin, std::string TagName) {
    u32 tagStart = (u32)bin.Position;
    Index = bin.readUInt8();
    AnimationTarget = bin.readUInt8();
    DataType = bin.readUInt16();
    auto KeyFrameCount = bin.readUInt16();
    bin.readUInt16();
    bin.Position = tagStart + bin.readUInt32();
    for (size_t i = 0; i < KeyFrameCount; i++)
        KeyFrames.emplace_back(bin, DataType);
    if (TagName == "FLEU") {
        FLEUUnknownInt = bin.readUInt32();
        FLEUEntryName = bin.readStr_NullTerm();
    }
}

void PaiTagEntry::Write(Buffer& bin, std::string TagName) {
    u32 tagStart = (u32)bin.Position;
    bin.Write(Index);
    bin.Write(AnimationTarget);
    bin.Write(DataType);
    bin.Write((u16)KeyFrames.size());
    bin.Write((u16)0);
    bin.Write((u32)bin.Position - tagStart + 4);
    for (size_t i = 0; i < KeyFrames.size(); i++) {
        bin.Write(KeyFrames[i].Frame);
        if (DataType == 2) {
            bin.Write(KeyFrames[i].Value);
            bin.Write(KeyFrames[i].Blend);
        } else if (DataType == 1) {
            bin.Write((u16)KeyFrames[i].Value);
            bin.Write((u16)KeyFrames[i].Blend);
        } else throw std::runtime_error("Unexpected data type for KeyFrame");
    }
    if (TagName == "FLEU") {
        bin.Write(FLEUUnknownInt);
        bin.Write(FLEUEntryName, Buffer::BinaryString::NullTerminated);
        while (bin.Position % 4 != 0)
            bin.Write((u8)0);
    }
}

PaiTag::PaiTag() {}

PaiTag::PaiTag(Buffer& bin, u8 TargetType) {
    if (TargetType == 2)
        Unknown = bin.readUInt32();
    auto sectionStart = (u32)bin.Position;
    TagType = bin.readStr(4);
    auto entryCount = bin.readUInt32();
    std::vector<u32> EntryOffsets;
    for (size_t i = 0; i < entryCount; i++)
        EntryOffsets.push_back(bin.readUInt32());
    for (size_t i = 0; i < entryCount; i++) {
        bin.Position = EntryOffsets[i] + sectionStart;
        Entries.emplace_back(bin, TagType);
    }
}

void PaiTag::Write(Buffer& bin, u8 TargetType) {
    if (TargetType == 2)
        bin.Write(Unknown);
    auto sectionStart = (u32)bin.Position;
    bin.Write(TagType, Buffer::BinaryString::NoPrefixOrTermination);
    bin.Write((u32)Entries.size());
    auto EntryTable = bin.Position;
    for (size_t i = 0; i < Entries.size(); i++)
        bin.Write((u32)0);
    for (size_t i = 0; i < Entries.size(); i++) {
        auto oldpos = bin.Position;
        bin.Position = EntryTable + i * 4;
        bin.Write((u32)oldpos - sectionStart);
        bin.Position = oldpos;
        Entries[i].Write(bin, TagType);
    }
}

PaiEntry::PaiEntry() {}

PaiEntry::PaiEntry(Buffer& bin) {
    u32 SectionStart = (u32)bin.Position;
    Name = bin.readStr_Fixed(28);
    auto tagCount = bin.readUInt8();
    Target = (AnimationTarget)bin.readUInt8();
    bin.readUInt16();
    std::vector<u32> TagOffsets;
    for (size_t i = 0; i < tagCount; i++)
        TagOffsets.push_back(bin.readUInt32());
    if (tagCount == 0) return;
    UnkwnownData = bin.readBytes((int)(TagOffsets[0] + SectionStart - bin.Position));
    for (size_t i = 0; i < tagCount; i++) {
        bin.Position = TagOffsets[i] + SectionStart;
        Tags.emplace_back(bin, (u8)Target);
    }
}

void PaiEntry::Write(Buffer& bin) {
    auto SectionStart = (u32)bin.Position;
    bin.WriteFixedLengthString(Name, 28);
    bin.Write((u8)Tags.size());
    bin.Write((u8)Target);
    bin.Write((u16)0);
    auto tagTable = bin.Position;
    for (size_t i = 0; i < Tags.size(); i++)
        bin.Write((u32)0);
    bin.Write(UnkwnownData);
    for (size_t i = 0; i < Tags.size(); i++) {
        auto oldPos = (u32)bin.Position;
        bin.Position = tagTable + i * 4;
        bin.Write((u32)(oldPos - SectionStart));
        bin.Position = oldPos;
        Tags[i].Write(bin, (u8)Target);
    }
}

BflanSection::BflanSection(const std::string& _name) : TypeName(_name) {}
BflanSection::BflanSection(const std::string& _name, const std::vector<u8>& data) : TypeName(_name), Data(data) {}

void BflanSection::BuildData(Endianness byteOrder) {}
void BflanSection::Write(Buffer& bin) {
    if (TypeName.size() != 4) throw std::runtime_error("unexpected type len");
    BuildData(bin.ByteOrder);
    bin.Write(TypeName, Buffer::BinaryString::NoPrefixOrTermination);
    bin.Write((u32)Data.size() + 8);
    bin.Write(Data);
}

Pat1Section::Pat1Section() : BflanSection("pat1") {}
Pat1Section::Pat1Section(const std::vector<u8>& data, Endianness bo) : BflanSection("pat1", data) {
    ParseData(bo);
}

void Pat1Section::ParseData(Endianness bo) {
    Buffer bin{ Data };
    bin.ByteOrder = bo;
    AnimationOrder = bin.readUInt16();
    auto groupCount = bin.readUInt16();
    if (groupCount != 1) throw std::runtime_error("File with unexpected group count");
    auto animName = bin.readUInt32() - 8;
    auto groupNames = bin.readUInt32() - 8;
    Unk_StartOfFile = bin.readUInt16();
    Unk_EndOfFile = bin.readUInt16();
    ChildBinding = bin.readUInt8();
    Unk_EndOfHeader = bin.readBytes((int)animName - (int)bin.Position);
    bin.Position = animName;
    Name = bin.readStr_NullTerm();
    for (size_t i = 0; i < groupCount; i++) {
        bin.Position = groupNames + i * groupNameLen;
        Groups.push_back(bin.readStr_Fixed(groupNameLen));
    }
}

void Pat1Section::BuildData(Endianness byteOrder) {
    Buffer bin;
    bin.ByteOrder = byteOrder;
    bin.Write((u16)AnimationOrder);
    bin.Write((u16)Groups.size());
    auto UpdateOffsetsPos = bin.Position;
    bin.Write((u32)0);
    bin.Write((u32)0);
    bin.Write(Unk_StartOfFile);
    bin.Write(Unk_EndOfFile);
    bin.Write(ChildBinding);
    bin.Write(Unk_EndOfHeader);
    auto oldPos = bin.Position;
    bin.Position = UpdateOffsetsPos;
    bin.Write((u32)oldPos + 8);
    bin.Position = oldPos;
    bin.Write(Name, Buffer::BinaryString::NullTerminated);
    while (bin.Position % 4 != 0)
        bin.Write((u8)0);
    oldPos = bin.Position;
    bin.Position = UpdateOffsetsPos + 4;
    bin.Write((u32)oldPos + 8);
    bin.Position = oldPos;
    for (size_t i = 0; i < Groups.size(); i++)
        bin.WriteFixedLengthString(Groups[i], groupNameLen);
    Data = bin.getBuffer();
}

Pai1Section::Pai1Section() : BflanSection("pai1") {}
Pai1Section::Pai1Section(const std::vector<u8>& data, Endianness bo) : BflanSection("pai1", data) {
    ParseData(bo);
}

void Pai1Section::ParseData(Endianness bo) {
    Buffer bin{ Data };
    bin.ByteOrder = bo;
    FrameSize = bin.readUInt16();
    Flags = bin.readUInt8();
    bin.readUInt8();
    auto texCount = bin.readUInt16();
    auto entryCount = bin.readUInt16();
    auto entryTable = bin.readUInt32() - 8;
    if (texCount != 0) {
        auto texTableStart = bin.Position;
        std::vector<u32> offsets;
        for (size_t i = 0; i < texCount; i++)
            offsets.push_back(bin.readUInt32());
        for (size_t i = 0; i < texCount; i++) {
            bin.Position = texTableStart + offsets[i];
            Textures.push_back(bin.readStr_NullTerm());
        }
    }
    for (size_t i = 0; i < entryCount; i++) {
        bin.Position = entryTable + i * 4;
        bin.Position = bin.readUInt32() - 8;
        Entries.emplace_back(bin);
    }
}

void Pai1Section::BuildData(Endianness bo) {
    Buffer bin;
    bin.ByteOrder = bo;
    bin.Write(FrameSize);
    bin.Write(Flags);
    bin.Write((u8)0);
    bin.Write((u16)Textures.size());
    bin.Write((u16)Entries.size());
    auto updateOffsets = bin.Position;
    bin.Write((u32)0);
    if (Textures.size() != 0) {
        auto texTableStart = bin.Position;
        for (size_t i = 0; i < Textures.size(); i++)
            bin.Write((u32)0);
        for (size_t i = 0; i < Textures.size(); i++) {
            auto texPos = bin.Position;
            bin.Write(Textures[i], Buffer::BinaryString::NullTerminated);
            auto endPos = bin.Position;
            bin.Position = texTableStart + i * 4;
            bin.Write((u32)(texPos - texTableStart));
            bin.Position = endPos;
        }
        while (bin.Position % 4 != 0)
            bin.Write((u8)0);
    }
    auto EntryTableStart = bin.Position;
    bin.Position = updateOffsets;
    bin.Write((u32)EntryTableStart + 8);
    bin.Position = EntryTableStart;
    for (size_t i = 0; i < Entries.size(); i++)
        bin.Write((u32)0);

    for (size_t i = 0; i < Entries.size(); i++) {
        auto oldpos = bin.Position;
        bin.Position = EntryTableStart + 4 * i;
        bin.Write((u32)oldpos + 8);
        bin.Position = oldpos;
        Entries[i].Write(bin);
    }

    Data = bin.getBuffer();
}

Bflan::~Bflan() {
    for (BflanSection* ptr : Sections)
        delete ptr;
    Sections.clear();
}

Bflan::Bflan() {}

Bflan::Bflan(const std::vector<u8>& data) {
    Buffer bin{ data };
    ParseFile(bin);
}

std::vector<u8> Bflan::WriteFile() {
    Buffer bin;
    bin.ByteOrder = byteOrder;
    bin.Write("FLAN", Buffer::BinaryString::NoPrefixOrTermination);
    bin.Write((u16)0xFEFF);
    bin.Write((u16)0x14);
    bin.Write(Version);
    bin.Write((u32)0);
    bin.Write((u16)Sections.size());
    bin.Write((u16)0);

    for (size_t i = 0; i < Sections.size(); i++)
        Sections[i]->Write(bin);

    bin.Position = 0xC;
    bin.Write((u32)bin.Length());
    return bin.getBuffer();
}

void Bflan::ParseFile(Buffer& bin) {
    if (bin.readStr(4) != "FLAN")
        throw std::runtime_error("Wrong bflan magic");
    u8 BOM = bin.readUInt8();
    if (BOM == 0xFF) byteOrder = Endianness::LittleEndian;
    else if (BOM == 0xFE) byteOrder = Endianness::BigEndian;
    else throw std::runtime_error("Unexpected BFLAN BOM");
    bin.ByteOrder = byteOrder;
    bin.readUInt8();
    if (bin.readUInt16() != 0x14) throw std::runtime_error("Unexpected bflan header size");
    Version = bin.readUInt32();
    bin.readUInt32();
    auto sectionCount = bin.readUInt16();
    bin.readUInt16();

    for (size_t i = 0; i < sectionCount; i++) {
        std::string sectionName = bin.readStr(4);
        s32 sectionSize = bin.readInt32();
        auto sectionData = bin.readBytes(sectionSize - 8);

        if (sectionName == "pat1")
            Sections.push_back((BflanSection*)new Pat1Section(sectionData, bin.ByteOrder));
        else if (sectionName == "pai1")
            Sections.push_back((BflanSection*)new Pai1Section(sectionData, bin.ByteOrder));
        else
            throw std::runtime_error("unexpected section");
    }
}

std::unique_ptr<Bflan> BflanDeserializer::FromJson(std::string jsn) {
    auto res = std::make_unique<Bflan>();
    auto j = nlohmann::json::parse(jsn);

    res->byteOrder = j["LittleEndian"].get<bool>() ? Endianness::LittleEndian : Endianness::BigEndian;
    res->Version = j["Version"].get<u32>();

    {
        auto pat1 = j["pat1"];
        Pat1Section* p = new Pat1Section();
        p->AnimationOrder = pat1["AnimationOrder"].get<u16>();
        p->Name = pat1["Name"].get<std::string>();
        p->ChildBinding = pat1["ChildBinding"].get<u8>();
        p->Unk_StartOfFile = pat1["Unk_StartOfFile"].get<u16>();
        p->Unk_EndOfFile = pat1["Unk_EndOfFile"].get<u16>();
        p->Groups = pat1["Groups"].get<std::vector<std::string>>();
        p->Unk_EndOfHeader = Base64::Decode(pat1["Unk_EndOfHeader"]);
        res->Sections.push_back((BflanSection*)p);
    }

    {
        auto pai1 = j["pai1"];
        Pai1Section* p = new Pai1Section();
        p->FrameSize = pai1["FrameSize"].get<u16>();
        p->Flags = pai1["Flags"].get<u8>();
        p->Textures = pai1["Textures"].get<std::vector<std::string>>();

        for (auto& Entry : pai1["Entries"]) {
            PaiEntry e;
            e.Name = Entry["Name"];
            e.Target = (PaiEntry::AnimationTarget)Entry["Target"].get<u8>();
            e.UnkwnownData = Base64::Decode(Entry["UnkwnownData"]);
            for (auto& Tag : Entry["Tags"]) {
                PaiTag t;
                t.Unknown = Tag["Unknown"].get<u32>();
                t.TagType = Tag["TagType"];
                for (auto& TagEntry : Tag["Entries"]) {
                    PaiTagEntry pe = {};
                    if (!TagEntry["Index"].is_null()) pe.Index = TagEntry["Index"].get<u8>();
                    if (!TagEntry["AnimationTarget"].is_null()) pe.AnimationTarget = TagEntry["AnimationTarget"].get<u8>();
                    if (!TagEntry["DataType"].is_null()) pe.DataType = TagEntry["DataType"].get<u16>();
                    if (!TagEntry["FLEUUnknownInt"].is_null()) pe.FLEUUnknownInt = TagEntry["FLEUUnknownInt"].get<u32>();
                    if (!TagEntry["FLEUEntryName"].is_null()) pe.FLEUEntryName = TagEntry["FLEUEntryName"].get<std::string>();
                    for (auto& key : TagEntry["KeyFrames"]) {
                        KeyFrame k;
                        k.Frame = key["Frame"].get<float>();
                        k.Value = key["Value"].get<float>();
                        k.Blend = key["Blend"].get<float>();
                        pe.KeyFrames.push_back(k);
                    }
                    t.Entries.push_back(pe);
                }
                e.Tags.push_back(t);
            }
            p->Entries.push_back(e);
        }

        res->Sections.push_back((BflanSection*)p);
    }

    return res;
}

} // namespace sphaira::theme
