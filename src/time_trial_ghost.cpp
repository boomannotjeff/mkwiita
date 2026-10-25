#include <mkw/time_trial_ghost.hpp>
#include <mkw/progress.hpp>
#include <cstdio>
#include <sys/stat.h>

namespace mkw {
namespace {
// The longest kept run: 30 minutes of ticks.
constexpr uint32_t MaxFrames=60u*60u*30u;
// Each license keeps its own ghosts.
void ghostPath(unsigned course,char* out,std::size_t size,TimeTrialGhost::Source source=TimeTrialGhost::Source::Own){
    if(source==TimeTrialGhost::Source::Own)std::snprintf(out,size,MKW_DATA_ROOT "/ghosts/l%u/course_%02u.mkg",progress().current,course);
    else std::snprintf(out,size,MKW_DATA_ROOT "/ghosts/staff/course_%02u_%s.mkg",course,source==TimeTrialGhost::Source::Staff?"staff":"expert");
}
bool readHeader(std::FILE* f,unsigned which,TimeTrialGhost::Header& h){
    return std::fread(&h,sizeof h,1,f)==1&&h.magic==TimeTrialGhost::Magic&&h.version==TimeTrialGhost::Version&&h.course==which&&h.frames>0&&h.frames<=MaxFrames&&h.milliseconds;
}
}

bool TimeTrialGhost::peek(unsigned which,Header& out,Source source){
    char path[64];ghostPath(which,path,sizeof path,source);
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    const bool ok=readHeader(f,which,out);
    std::fclose(f);return ok;
}

bool TimeTrialGhost::load(unsigned which,Source source){
    clear();
    char path[64];ghostPath(which,path,sizeof path,source);
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    Header h{};bool ok=readHeader(f,which,h);
    if(ok){frames.resize(h.frames);ok=std::fread(frames.data(),sizeof(GhostFrame),frames.size(),f)==frames.size();}
    if(ok){poses.resize(h.frames);ok=std::fread(poses.data(),sizeof(GhostPose),poses.size(),f)==poses.size();}
    std::fclose(f);
    if(!ok){clear();return false;}
    course=which;choice=h.choice;driftAuto=h.driftAuto!=0;milliseconds=h.milliseconds;
    return true;
}

bool TimeTrialGhost::save() const{
    if(!valid()||frames.size()>MaxFrames)return false;
    mkdir("ux0:data",0777);mkdir(MKW_DATA_ROOT,0777);mkdir(MKW_DATA_ROOT "/ghosts",0777);
    {char dir[40];std::snprintf(dir,sizeof dir,MKW_DATA_ROOT "/ghosts/l%u",progress().current);mkdir(dir,0777);}
    char path[64],temporary[72];ghostPath(course,path,sizeof path);std::snprintf(temporary,sizeof temporary,"%s.tmp",path);
    // A temporary file first, so a pulled card keeps the previous ghost.
    FILE* f=std::fopen(temporary,"wb");if(!f)return false;
    const Header h{Magic,Version,course,choice,driftAuto?1u:0u,milliseconds,uint32_t(frames.size())};
    const bool ok=std::fwrite(&h,sizeof h,1,f)==1&&std::fwrite(frames.data(),sizeof(GhostFrame),frames.size(),f)==frames.size()
        &&std::fwrite(poses.data(),sizeof(GhostPose),poses.size(),f)==poses.size();
    if(std::fclose(f)!=0||!ok){std::remove(temporary);return false;}
    std::remove(path);return std::rename(temporary,path)==0;
}
}
