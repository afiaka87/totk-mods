// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_LOG_HOST_TEST
#include "../src/native/DiagnosticLog.cpp"
#include <cassert>
#include <map>
#include <string>
std::map<std::string,std::string> files;
std::map<unsigned,std::string> handles;
unsigned nextHandle{}, writes{}, mounts{};
bool failWrite{};
namespace nn::fs {
Result MountSdCard(const char* name) { assert(std::string(name)=="mclog");++mounts;return {true}; }
Result CreateDirectory(const char*) { return {false,0x402}; }
Result OpenFile(FileHandle* out,const char* name,int) {
    if(!files.count(name)) return {false};
    out->index=++nextHandle;handles[out->index]=name;return {true};
}
void CloseFile(FileHandle handle) { assert(handles.erase(handle.index)==1); }
Result DeleteFile(const char* name) { return {files.erase(name)!=0}; }
Result RenameFile(const char* from,const char* to) { assert(files.count(from)&&!files.count(to));files[to]=files[from];files.erase(from);return {true}; }
Result CreateFile(const char* name,s64 size) { assert(!files.count(name));files[name].resize(size);return {true}; }
Result SetFileSize(FileHandle handle,s64 size) { files.at(handles.at(handle.index)).resize(size);return {true}; }
Result WriteFile(FileHandle handle,s64 offset,const void* bytes,u64 count,const WriteOption&) {
    assert(count==0 || static_cast<const char*>(bytes)[count-1]=='\n');
    ++writes;if(failWrite)return {false,0x1234};
    auto& file=files.at(handles.at(handle.index));assert(offset+count<=file.size());
    file.replace(offset,count,static_cast<const char*>(bytes),count);return {true};
}
}
using namespace material_columns::native;
void reset() { queued=written=0;dropped=0;disabled=false;ready=false;ticks=0;queueLock.clear();writerLock.clear(); }
int main() {
    files[path]="prior profile";files[previous]="older profile";
    queueDiagnostic("startup memory");assert(!mounts&&!writes);
    pumpDiagnostics(true);
    assert(files[previous]=="prior profile");
    assert(files[path].find("startup memory\n")!=std::string::npos);
    assert(files[path].find("queue=8192B")!=std::string::npos);
    const auto oldWrites=writes;pumpDiagnostics(true);assert(writes==oldWrites);
    queueLock.test_and_set();queueDiagnostic("contended");queueLock.clear();
    std::string chunk(510,'a');for(unsigned i=0;i<40;++i)queueDiagnostic(chunk);
    assert(queued<=queueCapacity&&dropped>0);pumpDiagnostics(true);
    assert(queued==0 && files[path].find("dropped-lines=")!=std::string::npos);
    reset();files.clear();
    for(unsigned i=0;i<300 && !disabled;++i) { for(unsigned j=0;j<4;++j)queueDiagnostic(chunk);pumpDiagnostics(); }
    assert(disabled&&files[path].size()<=fileLimit&&handles.empty());
    reset();files.clear();failWrite=true;queueDiagnostic("fail");pumpDiagnostics(true);
    assert(disabled&&handles.empty());const auto attempts=writes;
    queueDiagnostic("later");pumpDiagnostics(true);assert(writes==attempts);
}
