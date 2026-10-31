// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Identity.hpp"
#include "Localization.hpp"
#include <array>
#include <string_view>

namespace material_columns::config {
constexpr unsigned maxCategories = 44, maxMappings = 256, captionBytes = 192;
enum class Issue { Syntax, Version, Name, Locale, Category, Capacity, Override };
enum class Pass { Validate, Categories, Materials };
using Builtin = bool (*)(std::uint64_t, std::uint32_t&);
using Report = void (*)(void*, unsigned, Issue);

// string_view::substr can link exception machinery even in a -fno-exceptions module.
inline std::string_view slice(std::string_view s, std::size_t start, std::size_t count=std::string_view::npos) {
    if (start>=s.size()) return {};
    const auto remaining=s.size()-start;
    return {s.data()+start,count<remaining ? count : remaining};
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front()==' ' || s.front()=='\t' || s.front()=='\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back()==' ' || s.back()=='\t' || s.back()=='\r')) s.remove_suffix(1);
    return s;
}
inline bool identifier(std::string_view s, bool actor = false) {
    if (s.empty() || s.size()>63) return false;
    if (!actor && (s.front()=='.' || s.back()=='.' || s.find("..")!=s.npos)) return false;
    for (char c:s) if (!((c>='a' && c<='z') || (c>='0' && c<='9') ||
        (actor ? (c=='_' || (c>='A' && c<='Z')) : (c=='-' || c=='.')))) return false;
    return true;
}
inline std::uint64_t key(std::string_view s, bool group = false) {
    char text[70]{};
    const unsigned prefix = group ? 6 : 0;
    if (group) std::memcpy(text,"group:",6);
    if (s.size()>63) return 0;
    std::memcpy(text+prefix,s.data(),s.size());
    return identity(text);
}
inline int languageScore(std::string_view candidate, const char* selected) {
    bool known=false;
    for (const auto* code:localization::locales) if (candidate==code) known=true;
    if (!known) return -1;
    if (candidate==selected) return 3;
    const std::string_view language(selected);
    if ((language=="fr-CA" && candidate=="fr") || (language=="es-419" && candidate=="es") ||
        (language=="pt-BR" && candidate=="pt")) return 2;
    return candidate=="en-US" ? 1 : 0;
}

class Registry {
    struct Category { std::uint64_t id{}; char caption[captionBytes]{}; };
    struct Mapping { std::uint64_t actor{}, group{}; };
    std::array<Category,maxCategories> categories_{};
    std::array<Mapping,maxMappings> mappings_{};
    unsigned categoriesCount_{}, mappingsCount_{};
    unsigned mappingIndex(std::uint64_t actor) const {
        unsigned first=0,end=mappingsCount_;
        while (first<end) {
            const auto middle=first+(end-first)/2;
            if (mappings_[middle].actor<actor) first=middle+1; else end=middle;
        }
        return first;
    }
public:
    unsigned categories() const { return categoriesCount_; }
    unsigned mappings() const { return mappingsCount_; }
    bool category(std::uint64_t id, const char* caption) {
        if (!caption || std::strlen(caption)>=captionBytes) return false;
        unsigned i=0;
        while (i<categoriesCount_ && categories_[i].id!=id) ++i;
        if (i==maxCategories) return false;
        if (i==categoriesCount_) ++categoriesCount_;
        categories_[i].id=id;
        std::strcpy(categories_[i].caption,caption);
        return true;
    }
    bool rank(std::uint64_t id, std::uint32_t& out, Builtin builtin) const {
        if (builtin(id,out)) return true;
        for (unsigned i=0;i<categoriesCount_;++i) if (categories_[i].id==id) {
            out=0xffff0000u+i; return true;
        }
        return false;
    }
    const char* label(std::uint64_t id, const char* fallback) const {
        for (unsigned i=0;i<categoriesCount_;++i) if (categories_[i].id==id) return categories_[i].caption;
        return fallback;
    }
    std::uint64_t mapped(std::uint64_t actor) const {
        const auto i=mappingIndex(actor);
        return i<mappingsCount_ && mappings_[i].actor==actor ? mappings_[i].group : 0;
    }
    bool map(std::uint64_t actor, std::uint64_t group) {
        const auto i=mappingIndex(actor);
        if (i==mappingsCount_ || mappings_[i].actor!=actor) {
            if (mappingsCount_==maxMappings) return false;
            for (auto j=mappingsCount_;j>i;--j) mappings_[j]=mappings_[j-1];
            ++mappingsCount_;
        }
        mappings_[i]={actor,group}; return true;
    }
};

class Parser {
    Registry& registry_;
    Builtin builtin_;
    Report report_;
    void* context_;
    Pass pass_;
    const char* language_;
    enum class Section { None, General, Category, Materials } section_{};
    std::uint64_t group_{};
    unsigned line_{}, sectionLine_{};
    char caption_[captionBytes]{};
    int score_{};
    bool english_{}, version_{}, invalidVersion_{};
    void issue(Issue reason) { if (report_) report_(context_,line_,reason); }
    void finishCategory() {
        if (pass_!=Pass::Categories || section_!=Section::Category || !group_) return;
        const auto saved=line_; line_=sectionLine_;
        if (!english_ || !score_) issue(Issue::Name);
        else if (!registry_.category(group_,caption_)) issue(Issue::Capacity);
        line_=saved;
    }
public:
    Parser(Registry& registry, Builtin builtin, Pass pass, const char* language,
           Report report=nullptr, void* context=nullptr)
        : registry_(registry), builtin_(builtin), report_(report), context_(context), pass_(pass), language_(language) {}
    void line(std::string_view text) {
        ++line_;
        if (line_==1 && slice(text,0,3)=="\xef\xbb\xbf") text.remove_prefix(3);
        text=trim(text);
        if (text.empty() || text.front()==';' || text.front()=='#') return;
        if (text.front()=='[') {
            finishCategory(); section_=Section::None; group_=0; english_=false; score_=0;
            sectionLine_=line_;
            if (text.back()!=']') { issue(Issue::Syntax); return; }
            text=trim(slice(text,1,text.size()-2));
            if (text=="General") section_=Section::General;
            else if (text=="Materials") section_=Section::Materials;
            else if (slice(text,0,9)=="Category.") {
                text.remove_prefix(9); std::uint32_t rank{};
                if (!identifier(text) || text.find('.')==text.npos || builtin_(key(text,true),rank)) {
                    issue(Issue::Category); return;
                }
                group_=key(text,true); section_=Section::Category;
            } else issue(Issue::Syntax);
            return;
        }
        const auto equal=text.find('=');
        if (equal==text.npos) { issue(Issue::Syntax); return; }
        const auto name=trim(slice(text,0,equal)), value=trim(slice(text,equal+1));
        if (section_==Section::General) {
            if (name!="Version" || value!="1") { invalidVersion_=true; issue(Issue::Version); }
            else version_=true;
        } else if (section_==Section::Category && pass_==Pass::Categories) {
            if (slice(name,0,5)!="Name.") { issue(Issue::Syntax); return; }
            const auto locale=slice(name,5);
            const auto score=languageScore(locale,language_);
            if (score<0) { issue(Issue::Locale); return; }
            char encoded[captionBytes]{}; char16_t decoded[64]{}; unsigned length{};
            if (value.empty() || value.size()>=sizeof(encoded)) { issue(Issue::Name); return; }
            std::memcpy(encoded,value.data(),value.size());
            if (!localization::decode(encoded,value.size()+1,decoded,length)) { issue(Issue::Name); return; }
            if (locale=="en-US") english_=true;
            if (score && score>=score_) { std::strcpy(caption_,encoded); score_=score; }
        } else if (section_==Section::Materials && pass_==Pass::Materials) {
            if (!identifier(name,true) || !identifier(value)) { issue(Issue::Name); return; }
            const auto actor=key(name), group=key(value,true); std::uint32_t rank{};
            if (!registry_.rank(group,rank,builtin_)) { issue(Issue::Category); return; }
            const auto prior=registry_.mapped(actor);
            if (!registry_.map(actor,group)) issue(Issue::Capacity);
            else if (prior && prior!=group) issue(Issue::Override);
        } else if (section_==Section::None) issue(Issue::Syntax);
    }
    void badLine() { ++line_; finishCategory(); section_=Section::None; issue(Issue::Syntax); }
    bool finish() { finishCategory(); return version_ && !invalidVersion_; }
};
}
