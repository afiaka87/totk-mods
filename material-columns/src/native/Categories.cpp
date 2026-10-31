// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Categories.hpp"
#include <lib.hpp>
#include <nn/fs.h>
#include <algorithm>
#include <cstdio>

namespace material_columns::native::categories {
namespace {
constexpr char directory[]="content:/MaterialColumns/Categories";
constexpr unsigned maxFiles=32, maxFileBytes=65536;
bool attempted{};
struct Source { char name[64]{}; bool valid{}; };
struct Diagnostic { const char* name; unsigned reports{}, issues{}; };
void report(void* pointer, unsigned line, config::Issue issue) {
    auto& diagnostic=*static_cast<Diagnostic*>(pointer); ++diagnostic.issues;
    if (diagnostic.reports++<8) Logging.Log("[material-columns] ini file=%s line=%u issue=%u",
        diagnostic.name,line,unsigned(issue));
}
bool parse(const char* name, config::Pass pass, const char* language, Diagnostic& diagnostic) {
    char path[128]; std::snprintf(path,sizeof(path),"%s/%s",directory,name);
    nn::fs::FileHandle file{};
    if (!nn::fs::OpenFile(&file,path,nn::fs::OpenMode_Read).IsSuccess()) return false;
    s64 bytes{};
    bool ok=nn::fs::GetFileSize(&bytes,file).IsSuccess() && bytes>0 && bytes<=maxFileBytes;
    config::Parser parser(registry,builtin,pass,language,report,&diagnostic);
    char chunk[512], line[512]; unsigned used=0; bool overflow=false;
    for (s64 offset=0;ok && offset<bytes;) {
        const auto count=std::min<s64>(sizeof(chunk),bytes-offset);
        ok=nn::fs::ReadFile(file,offset,chunk,count).IsSuccess();
        if (!ok) break;
        for (s64 i=0;i<count;++i) {
            const char c=chunk[i];
            if (c=='\n') {
                if (overflow) parser.badLine(); else parser.line({line,used});
                used=0; overflow=false;
            } else if (!c || used==sizeof(line)) overflow=true;
            else line[used++]=c;
        }
        offset+=count;
    }
    if (used || overflow) { if (overflow) parser.badLine(); else parser.line({line,used}); }
    nn::fs::CloseFile(file);
    const bool version=parser.finish();
    return ok && version;
}
}
void loadOnce(const char* language) {
    if (attempted) return;
    attempted=true;
    nn::fs::DirectoryHandle handle{};
    const auto opened=nn::fs::OpenDirectory(&handle,directory,nn::fs::OpenDirectoryMode_File);
    if (!opened.IsSuccess()) {
        Logging.Log("[material-columns] ini directory unavailable result=%x; native categories retained",opened.GetInnerValueForDebug());
        return;
    }
    Source sources[maxFiles]{}; unsigned count=0; bool valid=true;
    for (;;) {
        nn::fs::DirectoryEntry entry{}; s64 read{};
        if (!nn::fs::ReadDirectory(&read,&entry,handle,1).IsSuccess()) { valid=false; break; }
        if (!read) break;
        const std::string_view name(entry.mName);
        if (name.size()<5 || config::slice(name,name.size()-4)!=".ini") continue;
        if (name.size()>=sizeof(Source::name) || count==maxFiles) { valid=false; break; }
        std::memcpy(sources[count++].name,name.data(),name.size());
    }
    nn::fs::CloseDirectory(handle);
    if (!valid) { Logging.Log("[material-columns] ini directory refused: read failure or file/name limit"); return; }
    for (unsigned i=1;i<count;++i) {
        const auto value=sources[i]; auto j=i;
        while (j && std::strcmp(value.name,sources[j-1].name)<0) { sources[j]=sources[j-1]; --j; }
        sources[j]=value;
    }
    unsigned issues=0;
    for (auto pass:{config::Pass::Validate,config::Pass::Categories,config::Pass::Materials}) {
        for (unsigned i=0;i<count;++i) {
            auto& source=sources[i];
            if (pass!=config::Pass::Validate && !source.valid) continue;
            Diagnostic diagnostic{source.name};
            const bool ok=parse(source.name,pass,language,diagnostic);
            if (pass==config::Pass::Validate) source.valid=ok;
            if (!ok) Logging.Log("[material-columns] ini refused file=%s pass=%u (version or read)",source.name,unsigned(pass));
            issues+=diagnostic.issues;
        }
    }
    Logging.Log("[material-columns] ini files=%u categories=%u mappings=%u storage=%u issues=%u locale=%s",
        count,registry.categories(),registry.mappings(),unsigned(sizeof(registry)),issues,language);
}
}
