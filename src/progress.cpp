#include <mkw/progress.hpp>
#include <mkw/staff_ghosts.hpp>
#include <mkw/menu_ui.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace mkw {
namespace {
constexpr const char* SavePath=MKW_DATA_ROOT "/save.bin";
constexpr const char* LegacyTimes=MKW_DATA_ROOT "/best-times.bin";
constexpr uint32_t Magic=0x4d4b5750,Version=1; // "MKWP"
enum Class:unsigned{CC50,CC100,CC150,Mirror};
constexpr unsigned WiiCups[]={0,1,2,3},RetroCups[]={4,5,6,7},AllCups[]={0,1,2,3,4,5,6,7};
constexpr bool RetroRewind=RetroRewindEdition; // everything is unlocked, as in Retro Rewind
void ghostFile(unsigned slot,unsigned course,char* out,std::size_t size){
    if(slot<Progress::Licenses)std::snprintf(out,size,MKW_DATA_ROOT "/ghosts/l%u/course_%02u.mkg",slot,course);
    else std::snprintf(out,size,MKW_DATA_ROOT "/ghosts/course_%02u.mkg",course); // before licenses
}
}
Progress& progress(){static Progress value;return value;}

bool Progress::load(){
    *this=Progress{};
    FILE* f=std::fopen(SavePath,"rb");if(!f)return false;
    uint32_t head[2]{};uint8_t all=0;
    const bool ok=std::fread(head,sizeof head,1,f)==1&&head[0]==Magic&&head[1]==Version&&
                  std::fread(&all,1,1,f)==1&&std::fread(license.data(),sizeof license,1,f)==1;
    std::fclose(f);
    if(!ok){*this=Progress{};return false;}
    unlockAll=all!=0||RetroRewind;
    for(auto& l:license)l.name[sizeof l.name-1]=0;
    return true;
}
// A temporary file first, so a pulled card or a crash keeps the previous save.
bool Progress::save()const{
    mkdir("ux0:data",0777);mkdir(MKW_DATA_ROOT,0777);
    constexpr const char* Temporary=MKW_DATA_ROOT "/save.tmp";
    FILE* f=std::fopen(Temporary,"wb");if(!f)return false;
    const uint32_t head[2]{Magic,Version};const uint8_t all=unlockAll;
    const bool ok=std::fwrite(head,sizeof head,1,f)==1&&std::fwrite(&all,1,1,f)==1&&std::fwrite(license.data(),sizeof license,1,f)==1;
    if(std::fclose(f)!=0||!ok){std::remove(Temporary);return false;}
    std::remove(SavePath);return std::rename(Temporary,SavePath)==0;
}

void Progress::create(unsigned slot,const char* name){
    if(slot>=Licenses)return;
    const bool first=std::none_of(license.begin(),license.end(),[](const License& l){return l.used;});
    auto& l=license[slot];l=License{};l.used=1;
    std::snprintf(l.name,sizeof l.name,"%s",name&&*name?name:"Player");
    if(!first)return;
    // Best times and ghosts from before licenses become this license's.
    if(FILE* f=std::fopen(LegacyTimes,"rb")){
        std::array<uint32_t,32> times{};
        const bool ok=std::fread(times.data(),sizeof times,1,f)==1;std::fclose(f);
        if(ok){std::copy_n(times.begin(),std::min<std::size_t>(32,RaceCourseCount),l.bestTimes.begin());for(unsigned c=0;c<32;++c)if(times[c])l.timeTrialCourses|=1u<<c;}
        std::rename(LegacyTimes,MKW_DATA_ROOT "/best-times.imported.bin");
    }
    mkdir(MKW_DATA_ROOT "/ghosts",0777);
    char dir[48];std::snprintf(dir,sizeof dir,MKW_DATA_ROOT "/ghosts/l%u",slot);mkdir(dir,0777);
    for(unsigned c=0;c<32;++c){char from[64],to[64];ghostFile(Licenses,c,from,sizeof from);ghostFile(slot,c,to,sizeof to);std::rename(from,to);}
}
void Progress::erase(unsigned slot){
    if(slot>=Licenses)return;
    license[slot]=License{};
    for(unsigned c=0;c<RaceCourseCount;++c){char path[64];ghostFile(slot,c,path,sizeof path);std::remove(path);}
}

unsigned Progress::expertGhosts()const{
    unsigned n=0;
    if(RetroRewind)return 0; // StaffGhosts are the retail courses'
    for(unsigned c=0;c<32;++c)if(me().bestTimes[c]&&me().bestTimes[c]<=StaffGhosts[c].unlock)++n;
    return n;
}
namespace {
template<unsigned N> bool allTrophies(const License& l,unsigned cls,const unsigned (&cups)[N]){for(unsigned c:cups)if(!l.trophy[cls][c])return false;return true;}
template<unsigned N> bool allStars(const License& l,unsigned cls,const unsigned (&cups)[N]){for(unsigned c:cups)if(!l.trophy[cls][c]||!l.stars[cls][c])return false;return true;}
bool won(const License& l,unsigned cls,unsigned cup){return l.trophy[cls][cup]==3;}
unsigned courses(const License& l){unsigned n=0;for(unsigned c=0;c<32;++c)n+=(l.timeTrialCourses>>c)&1u;return n;}
}
bool Progress::character(unsigned driver)const{
    if(unlockAll||driver<12)return true;
    const auto& l=me();
    switch(driver){
    case 12:return allStars(l,CC50,WiiCups);       // Baby Daisy
    case 13:return expertGhosts()>=8;              // Baby Luigi
    case 14:return courses(l)>=16;                 // Birdo
    case 15:return won(l,CC150,3);                 // Daisy: 150cc Special Cup
    case 16:return won(l,CC50,7);                  // Diddy Kong: 50cc Lightning Cup
    case 17:return won(l,CC100,6);                 // Dry Bones: 100cc Leaf Cup
    case 18:return allStars(l,CC150,WiiCups);      // Dry Bowser
    case 19:return expertGhosts()>=4;              // Funky Kong
    case 20:return won(l,CC50,2);                  // King Boo: 50cc Star Cup
    case 21:return allStars(l,Mirror,WiiCups);     // Rosalina
    case 22:return courses(l)>=32;                 // Toadette
    case 23:return allStars(l,CC100,RetroCups);    // Bowser Jr.
    case 24:return won(l,CC100,3);                 // Mii Outfit A: 100cc Special Cup
    case 25:return expertGhosts()>=32;             // Mii Outfit B
    default:return false;
    }
}
bool Progress::vehicle(unsigned weight,unsigned choice)const{
    if(unlockAll||choice<6)return true;
    const auto& l=me();
    // UiVehicleNames: choices 6..11 of each weight class.
    switch(weight*12+choice){
    case 6:return allStars(l,CC50,RetroCups);      // Cheep Charger
    case 7:return expertGhosts()>=1;               // Tiny Titan
    case 8:return won(l,Mirror,7);                 // Blue Falcon: Mirror Lightning Cup
    case 9:return won(l,CC150,2);                  // Quacker: 150cc Star Cup
    case 10:return courses(l)>=8;                  // Magikruiser
    case 11:return won(l,Mirror,6);                // Jet Bubble: Mirror Leaf Cup
    case 18:return won(l,CC50,6);                  // Super Blooper: 50cc Leaf Cup
    case 19:return won(l,CC150,6);                 // Daytripper: 150cc Leaf Cup
    case 20:return expertGhosts()>=24;             // Sprinter
    case 21:return won(l,CC100,7);                 // Zip Zip: 100cc Lightning Cup
    case 22:return allStars(l,CC100,WiiCups);      // Sneakster
    case 23:return won(l,Mirror,2);                // Dolphin Dasher: Mirror Star Cup
    case 30:return won(l,CC50,3);                  // Piranha Prowler: 50cc Special Cup
    case 31:return allStars(l,CC150,RetroCups);    // Jetsetter
    case 32:return won(l,CC150,7);                 // Honeycoupe: 150cc Lightning Cup
    case 33:return won(l,CC100,2);                 // Shooting Star: 100cc Star Cup
    case 34:return expertGhosts()>=12;             // Spear
    case 35:return won(l,Mirror,3);                // Phantom: Mirror Special Cup
    default:return false;
    }
}
bool Progress::cup(unsigned cls,unsigned cup)const{
    if(unlockAll)return true;
    const auto& t=me().trophy[std::min(cls,3u)];
    switch(cup){
    case 2:return t[0]&&t[1];  // Star: Mushroom and Flower
    case 3:return t[2];        // Special: Star
    case 6:return t[4]&&t[5];  // Leaf: Shell and Banana
    case 7:return t[6];        // Lightning: Leaf
    default:return cup<8;
    }
}
bool Progress::anyClassCup(unsigned c)const{for(unsigned cls=0;cls<4;++cls)if(cup(cls,c))return true;return false;}
bool Progress::mirror()const{if(unlockAll)return true;for(unsigned c:AllCups)if(!won(me(),CC150,c))return false;return true;}
bool Progress::mixedTypes(unsigned cls)const{return unlockAll||(cls<2&&allTrophies(me(),cls,AllCups));}

// Bits: 0..13 drivers 12..25, 14..31 machines (weight*6 + choice-6), 32..47
// the locked cups (Star, Special, Leaf, Lightning) of each class, 48 Mirror,
// 49..50 the karts/bikes switch in 50cc and 100cc.
namespace {constexpr unsigned LockedCups[]={2,3,6,7};constexpr const char* ClassNames[]={"50cc","100cc","150cc","Mirror"};}
uint64_t Progress::unlockables()const{
    Progress p=*this;p.unlockAll=false;uint64_t bits=0;unsigned i=0;
    for(unsigned d=12;d<26;++d,++i)if(p.character(d))bits|=1ull<<i;
    for(unsigned w=0;w<3;++w)for(unsigned c=6;c<12;++c,++i)if(p.vehicle(w,c))bits|=1ull<<i;
    for(unsigned cls=0;cls<4;++cls)for(unsigned c:LockedCups){if(p.cup(cls,c))bits|=1ull<<i;++i;}
    if(p.mirror())bits|=1ull<<i;++i;
    for(unsigned cls=0;cls<2;++cls,++i)if(p.mixedTypes(cls))bits|=1ull<<i;
    return bits;
}
const char* Progress::unlockableName(unsigned i){
    static constexpr const char* Cups[]={"Star Cup","Special Cup","Leaf Cup","Lightning Cup"};
    if(i<14)return UiCharacters[12+i].name;
    if(i<32)return UiVehicleNames[(i-14)/6][6+(i-14)%6];
    if(i<48)return Cups[(i-32)%4];
    return i==48?"Mirror":"Karts and Bikes";
}
const char* Progress::unlockableClass(unsigned i){
    if(i>=32&&i<48)return ClassNames[(i-32)/4];
    if(i==49||i==50)return ClassNames[i-49];
    return nullptr;
}

void Progress::grandPrix(unsigned cls,unsigned cup,unsigned place,unsigned points){
    if(cls>3||cup>=RaceCupCount||!place)return;
    auto& l=me();
    const uint8_t trophy=place<=3?uint8_t(4-place):0;
    // shortcut: stars from the points total alone (retail also weighs other race stats); match retail's rank formula if a rank comes out wrong.
    const uint8_t stars=!trophy?0:points>=60?3:points>=56?2:points>=50?1:0;
    l.trophy[cls][cup]=std::max(l.trophy[cls][cup],trophy);
    l.stars[cls][cup]=std::max(l.stars[cls][cup],stars);
}
bool Progress::timeTrial(unsigned course,uint32_t ms){
    if(course>=RaceCourseCount||!ms)return false;
    auto& l=me();if(course<32)l.timeTrialCourses|=1u<<course;
    if(l.bestTimes[course]&&ms>=l.bestTimes[course])return false;
    l.bestTimes[course]=ms;return true;
}
}
