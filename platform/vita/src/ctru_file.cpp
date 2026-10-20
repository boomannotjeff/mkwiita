// QA: counts file opens and the time spent opening (fopen is wrapped at link,
// -Wl,--wrap=fopen), as src/linear_lock.cpp did on the 3DS.
#include <3ds.h>
#include <mkw/asset_pack.hpp>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace mkw {std::atomic<unsigned> fileOpens{0};std::atomic<unsigned long long> fileOpenTicks{0};}
extern "C" {
FILE* __real_fopen(const char* path,const char* mode);
FILE* __wrap_fopen(const char* path,const char* mode){
 const u64 start=svcGetSystemTick();
 // models/ and textures/ ship inside race*.pak (tools/pack_race_assets.py), not as loose files.
 FILE* file=mode[0]=='r'&&(!std::strncmp(path,"models/",7)||!std::strncmp(path,"textures/",9))?mkw::pack::openFile(path):nullptr;
 if(!file)file=__real_fopen(path,mode);
 mkw::fileOpens.fetch_add(1,std::memory_order_relaxed);mkw::fileOpenTicks.fetch_add(svcGetSystemTick()-start,std::memory_order_relaxed);
 return file;
}
}
