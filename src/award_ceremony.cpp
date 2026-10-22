// Grand Prix award ceremony: see include/mkw/award_ceremony.hpp.
#include <mkw/award_ceremony.hpp>
#include <mkw/course_lighting.hpp>
#include <mkw/garage.hpp>
#include <mkw/display_layout.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
// Demo/Award.szs model names by Kinoko::Character. Outfit C Miis share B's body.
constexpr const char* AwardCodes[48]={
    "mr","bpc","wl","kp","bds","ka","bmr","lg","ko","dk","ys","wr","blg","kk","nk","ds","pc","ca","dd","kt","jr","bk","fk","rs",
    "sa_mii_m","sa_mii_f","sb_mii_m","sb_mii_f","sb_mii_m","sb_mii_f",
    "ma_mii_m","ma_mii_f","mb_mii_m","mb_mii_f","mb_mii_m","mb_mii_f",
    "la_mii_m","la_mii_f","lb_mii_m","lb_mii_f","lb_mii_m","lb_mii_f",
    "ma_mii_m","sa_mii_m","la_mii_m","pc3","ds3","rs3"};
// Demo/Trophy by cup, Mushroom to Lightning.
constexpr const char* Trophies[8]={"kinoko","flower","star","special","koura","banana","konoha","thunder"};
// award_v_stand1: first place's block tops out at y 1364 over x -125..125, the
// side blocks at 1295 over |x| 175..375, all of them z 27750..27950. The camera
// moves look down -z, so second stands at screen left.
constexpr Vec3 Spots[3]={{0,1364,27850},{-275,1295,27850},{275,1295,27850}};
// The team stand (award_v_stand2): flat at y 1304 over x -500..500, z 27510..28000;
// up to six winners in a row across it.
constexpr float TeamTop=1304,TeamZ=27755,TeamSpacing=166;
constexpr unsigned FadeTicks=30;
// The podium's last frame holds this long before the standings.
constexpr unsigned PodiumHold=180;
// demowin02 is a single frame: the cut between the drive and the podium.
constexpr unsigned CutHold=60;
// models/ceremony/text.bin order (tools/cook_ceremony.py TEXT).
enum TextIndex:unsigned {TextCongratulations=0,TextBetterLuck=1,TextCup=2,TextRank=10,TextGrade=11,TextPlace=19,TextClass=22,TextTeam=26,TextMode=28,TextCount=31};
constexpr const char* TextKeys[TextCount]={"congratulations","better_luck","cup_0","cup_1","cup_2","cup_3","cup_4","cup_5","cup_6","cup_7",
    "rank","grade_0","grade_1","grade_2","grade_3","grade_4","grade_5","grade_6","grade_7",
    "place_0","place_1","place_2","class_0","class_1","class_2","class_3","team_0","team_1","mode_0","mode_1","mode_2"};
C3D_Mtx frame(Vec3 x,Vec3 y,Vec3 z,Vec3 at){
    C3D_Mtx m;Mtx_Identity(&m);
    m.r[0].x=x.x;m.r[0].y=y.x;m.r[0].z=z.x;m.r[0].w=at.x;
    m.r[1].x=x.y;m.r[1].y=y.y;m.r[1].z=z.y;m.r[1].w=at.y;
    m.r[2].x=x.z;m.r[2].y=y.z;m.r[2].z=z.z;m.r[2].w=at.z;
    return m;
}

const char* awardCode(unsigned character,unsigned vehicle){
    // Peach, Daisy and Rosalina wear their biker outfits on bikes (vehicle 18+).
    if(vehicle>=18){if(character==16)return "pc3";if(character==15)return "ds3";if(character==23)return "rs3";}
    return AwardCodes[std::min(character,47u)];
}
bool loadList(const char* list,std::vector<std::unique_ptr<Model>>& parts){
    FILE* f=std::fopen(list,"r");if(!f)return false;
    char name[64];bool ok=true;
    while(std::fscanf(f,"%63s",name)==1){
        if(std::strchr(name,'/')||std::strstr(name,"..")){ok=false;break;}
        char path[128];std::snprintf(path,sizeof path,"models/%s.gmdl",name);
        auto model=std::make_unique<Model>();
        if(!model->load(path)){ok=false;break;}
        model->enableStaticCulling();parts.push_back(std::move(model));
    }
    std::fclose(f);
    return ok&&!parts.empty();
}
std::unique_ptr<Model> loadModel(const char* name){
    char path[128];std::snprintf(path,sizeof path,"models/%s.gmdl",name);
    auto model=std::make_unique<Model>();
    return model->load(path)?std::move(model):nullptr;
}
// tools/ceremony/export_scene.ts: "SCAM", version 1, frame count, 13 floats a frame.
bool loadCamera(const char* name,std::vector<float>& frames,unsigned& count){
    char path[128];std::snprintf(path,sizeof path,"models/ceremony/%s.scam",name);
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    char magic[4];std::uint32_t header[2];
    bool ok=std::fread(magic,1,4,f)==4&&!std::memcmp(magic,"SCAM",4)&&std::fread(header,4,2,f)==2&&header[0]==1&&header[1]>0&&header[1]<100000;
    if(ok){frames.resize(std::size_t(header[1])*13);ok=std::fread(frames.data(),sizeof(float),frames.size(),f)==frames.size();count=header[1];}
    std::fclose(f);
    return ok;
}
}

AwardCeremony::~AwardCeremony()=default;

unsigned AwardCeremony::grade(unsigned points){
    // Approximated from the points alone (four wins are 60): retail's grade
    // also weighs how the races were driven.
    constexpr unsigned Floors[7]={60,56,52,46,40,32,24};
    for(unsigned g=0;g<7;++g)if(points>=Floors[g])return g;
    return 7;
}

bool AwardCeremony::load(const Setup& setup){
    setup_=setup;setup_.count=std::min(setup.count,setup.team?6u:3u);
    shot_=shotTick_=tick_=ending_=driveTicks_=podiumTicks_=standingsTicks_=0;skipping_=false;driving_=false;standings_=false;cameras_.clear();confetti_.clear();
    if(FILE* f=std::fopen("models/ceremony.json","r"))std::fclose(f);else return false;
    // The team stand sits where the podium does, on the winning run.
    const unsigned stage=setup_.won||setup_.team?55:56;
    char list[64];
    std::snprintf(list,sizeof list,"models/course_%02u.list",stage);
    if(!loadList(list,stage_)){stage_.clear();return false;}
    std::snprintf(list,sizeof list,"models/course_%02u_sky.list",stage);
    if(!loadList(list,sky_))sky_.clear();
    stand_=loadModel(setup_.team?"award_v_stand2":"award_v_stand1");shadow_=loadModel("award_shadow");
    if(!setup_.vs){char trophy[32];std::snprintf(trophy,sizeof trophy,"trophy_%s",Trophies[std::min(setup_.cup,7u)]);trophy_=loadModel(trophy);}
    for(unsigned i=0;i<setup_.count;++i){
        auto& r=racers_[i];
        char path[128];const char* code=awardCode(setup_.character[i],setup_.vehicle[i]);
        std::snprintf(path,sizeof path,"models/award_%s.gmdl",code);
        r.model=std::make_unique<Model>();
        if(!r.model->load(path)){r.model.reset();continue;}
        std::snprintf(path,sizeof path,"models/award_%s.dskm",code);
        r.animation=std::make_unique<DriverAnimation>();
        if(!r.animation->load(path,*r.model)){r.animation.reset();}
        else{
            const char* clip=i==0||setup_.team?"demowin":"demooth";
            r.clip=r.animation->find(clip);r.face=r.animation->face(clip);
        }
        r.position=setup_.team?Vec3{(float(i)-float(setup_.count-1)*.5f)*TeamSpacing,TeamTop,TeamZ}:Spots[i];
    }
    // The podium: 123dai's demowin03 (it ends facing the podium); the losing
    // scene is one fixed shot from across the course. A win drives the winning
    // run before it (loadKarts); the standings follow both.
    std::vector<const char*> shots;
    if(setup_.won||setup_.team)shots={"demowin03"};else shots={"demolos01_16vs9"};
    for(std::size_t i=0;i<shots.size();++i){
        Camera c;
        if(!loadCamera(shots[i],c.frames,c.count))return false;
        const bool last=i+1==shots.size();
        c.hold=last?(c.count>1?PodiumHold:PodiumHold*2):(c.count>1?0:CutHold);
        cameras_.push_back(std::move(c));
    }
    // Without the karts, their lanes or the course cameras, the drive is left out.
    driving_=setup_.won&&!setup_.team&&loadKarts();
    // Overlay: the closing line, the cup and the rank grade.
    std::array<uint16_t,TextCount*2> sizes{};
    if(FILE* f=std::fopen("models/ceremony/text.bin","rb")){
        // Files cooked before a string was added are shorter: those strings
        // stay hidden, the rest still show.
        const std::size_t read=std::fread(sizes.data(),2,sizes.size(),f);
        std::fill(sizes.begin()+std::ptrdiff_t(read&~std::size_t(1)),sizes.end(),uint16_t(0));
        std::fclose(f);
    }
    auto text=[&](Text& t,unsigned index){
        char path[96];std::snprintf(path,sizeof path,"textures/ceremony_text_%s.t3x",TextKeys[index]);
        t.width=sizes[index*2];t.height=sizes[index*2+1];
        if(!t.width||!t.height||!t.texture.load(path))t.width=t.height=0;
    };
    text(headline_,setup_.won?TextCongratulations:TextBetterLuck);
    // The standings line: "You got Nth place!" in the top three, else the losing line.
    text(placeText_,setup_.won&&setup_.place>=1&&setup_.place<=3?TextPlace+setup_.place-1:TextBetterLuck);
    text(classText_,TextClass+std::min(setup_.engineClass,3u));
    if(!setup_.vs){
        text(cupName_,TextCup+std::min(setup_.cup,7u));
        text(rankLabel_,TextRank);
        text(gradeText_,TextGrade+grade(setup_.points));
    }
    if(setup_.team){text(teamText_,TextTeam+std::min(setup_.winner,1u));text(modeText_,TextMode+std::min(setup_.mode,2u));}
    // Award cup icons: the Special Cup's is the crown (oukan), the Leaf Cup's konoha.
    static const char* cupFiles[8]={"kinoko","flower","star","oukan","koura","banana","konoha","thunder"};
    char path[96];std::snprintf(path,sizeof path,"textures/ui/award/award__tt_cup_icon_%s_00.t3x",cupFiles[std::min(setup_.cup,7u)]);
    if(!setup_.vs)cupIcon_.load(path);
    if(setup_.won||setup_.team)confettiTexture_.load("textures/ui/award/winning_run__tt_block_16x16.t3x");
    return true;
}

bool AwardCeremony::loadKarts(){
    // models/ceremony/winning_run.bin (tools/cook_ceremony.py cook_winning_run):
    // the three lanes, the CAME chain and the POTI routes it rides.
    FILE* f=std::fopen("models/ceremony/winning_run.bin","rb");if(!f)return false;
    std::uint32_t header[2]{};std::vector<float> points;
    bool ok=std::fread(header,4,2,f)==2&&header[0]==3&&header[1]>=2&&header[1]<1000;
    if(ok){points.resize(std::size_t(header[0])*header[1]*3);ok=std::fread(points.data(),4,points.size(),f)==points.size();}
    driveCameras_.clear();routes_.clear();
    std::uint32_t cameras[2]{};
    ok=ok&&std::fread(cameras,4,2,f)==2&&cameras[0]>0&&cameras[0]<64&&cameras[1]<cameras[0];
    for(unsigned c=0;ok&&c<cameras[0];++c){
        uint8_t bytes[4];uint16_t speeds[4];float values[12];
        ok=std::fread(bytes,1,4,f)==4&&std::fread(speeds,2,4,f)==4&&std::fread(values,4,12,f)==12;
        if(!ok)break;
        DriveCamera d;d.type=bytes[0];d.next=bytes[1];d.route=bytes[2];d.pathSpeed=speeds[0];d.fovSpeed=speeds[1];d.viewSpeed=speeds[2];
        d.position={values[0],values[1],values[2]};d.viewStart={values[3],values[4],values[5]};d.viewEnd={values[6],values[7],values[8]};
        d.fovStart=values[9];d.fovEnd=values[10];d.time=values[11];
        driveCameras_.push_back(d);
    }
    std::uint32_t routes=0;
    ok=ok&&std::fread(&routes,4,1,f)==1&&routes<64;
    for(unsigned r=0;ok&&r<routes;++r){
        std::uint32_t count=0;ok=std::fread(&count,4,1,f)==1&&count<1000;
        Route route;
        for(unsigned n=0;ok&&n<count;++n){float v[4];ok=std::fread(v,4,4,f)==4;route.points.push_back({v[0],v[1],v[2]});route.speeds.push_back(v[3]);}
        routes_.push_back(std::move(route));
    }
    std::fclose(f);
    if(!ok)return false;
    driveFirst_=cameras[1];
    for(unsigned i=0;i<setup_.count;++i){
        auto& k=karts_[i];
        for(const auto& c:GarageChoices)if(c.character==setup_.character[i]&&c.vehicle==setup_.vehicle[i]){k.choice=&c;break;}
        if(!k.choice)return false;
        auto model=[&](const char* suffix){auto m=std::make_unique<Model>();char p[160];std::snprintf(p,sizeof p,"models/%s%s.gmdl",k.choice->prefix,suffix);if(!m->load(p))m.reset();return m;};
        k.body=model("body");k.driver=model("driver");
        static const char* wheelNames[4]={"tire_fl","tire_fr","tire_rl","tire_rr"};
        const unsigned wheels=k.choice->bike?2u:4u;
        for(unsigned w=0;w<wheels;++w)if(!(k.wheels[w]=model(wheelNames[w])))return false;
        if(!k.body||!k.driver)return false;
        char p[160];std::snprintf(p,sizeof p,"models/%sdriver.dskm",k.choice->prefix);
        k.animation=std::make_unique<DriverAnimation>();
        if(k.animation->load(p,*k.driver)){k.animation->prepareLitSkinning(*k.driver);k.clip=k.animation->find("drive");}
        else k.animation.reset();
        // Wheels placed from the body's extent (the race takes them from
        // Kinoko's tyre physics): a tyre radius in from each end, a kart's at
        // the sides, a bike's on its centre line; the body rides a radius up.
        const auto& b=*k.body;const auto& t=*k.wheels[0];
        k.radius=std::max((t.maximum.y-t.minimum.y)*.5f,8.f);
        const float halfWidth=(t.maximum.x-t.minimum.x)*.5f;
        const float front=b.maximum.z-k.radius*1.1f,rear=b.minimum.z+k.radius*1.1f,y=b.minimum.y+k.radius*.8f;
        if(k.choice->bike){k.wheelAt[0]={0,y,front};k.wheelAt[1]={0,y,rear};}
        else{
            const float x=b.maximum.x-halfWidth;
            k.wheelAt={Vec3{x,y,front},Vec3{-x,y,front},Vec3{x,y,rear},Vec3{-x,y,rear}};
        }
        k.lift=k.radius-y;
        k.lane.clear();k.along.clear();
        for(unsigned n=0;n<header[1];++n){const float* q=&points[(std::size_t(i)*header[1]+n)*3];k.lane.push_back({q[0],q[1],q[2]});}
        float total=0;k.along.push_back(0);
        for(std::size_t n=1;n<k.lane.size();++n){total+=length(k.lane[n]-k.lane[n-1]);k.along.push_back(total);}
        // Cruising speed (units a frame): where the retail cameras' timing
        // finds the karts in their shots.
        k.speed=74.f;
    }
    beginDriveCamera(int(driveFirst_));
    return driveCamera_>=0;
}

void AwardCeremony::beginDriveCamera(int index){
    driveCamera_=index>=0&&unsigned(index)<driveCameras_.size()?index:-1;
    if(driveCamera_<0)return;
    const auto& c=driveCameras_[unsigned(driveCamera_)];
    driveElapsed_=0;routeDistance_=0;driveFov_=c.fovStart;driveTarget_=c.viewStart;
    const bool routed=c.type!=4&&c.route<routes_.size()&&!routes_[c.route].points.empty();
    driveEye_=routed?routes_[c.route].points.front():c.position;
}

float AwardCeremony::kartDistance(unsigned kart) const{
    // Pulling away over a second, then cruising; parked at the lane's end.
    const auto& k=karts_[kart];if(k.along.empty())return 0;
    const float t=float(driveTicks_),ramp=60.f;
    return std::min(t<ramp?.5f*k.speed*t*t/ramp:k.speed*(t-ramp*.5f),k.along.back());
}

Vec3 AwardCeremony::kartPosition(unsigned kart,float distance) const{
    const auto& k=karts_[kart];if(k.lane.size()<2)return {};
    distance=std::clamp(distance,0.f,k.along.back());
    std::size_t n=1;while(n+1<k.lane.size()&&k.along[n]<distance)++n;
    const float span=std::max(k.along[n]-k.along[n-1],.001f),u=std::clamp((distance-k.along[n-1])/span,0.f,1.f);
    return k.lane[n-1]+(k.lane[n]-k.lane[n-1])*u;
}

void AwardCeremony::stepDriveCamera(){
    if(driveCamera_<0)return;
    const auto& c=driveCameras_[unsigned(driveCamera_)];
    // FOV and the view point step linearly at speed/100 a frame.
    auto approach=[](float& value,float goal,float step){value=value<goal?std::min(goal,value+step):std::max(goal,value-step);};
    approach(driveFov_,c.fovEnd,float(c.fovSpeed)/100.f);
    if(c.type==2){
        // Watches the player's kart (the winner's when the player is not there).
        const unsigned watched=setup_.player>=0&&unsigned(setup_.player)<setup_.count?unsigned(setup_.player):0;
        driveTarget_=kartPosition(watched,kartDistance(watched))+Vec3{0,60,0};
    }else{
        const Vec3 delta=c.viewEnd-driveTarget_;const float remaining=length(delta),step=float(c.viewSpeed)/100.f;
        driveTarget_=remaining<=step?c.viewEnd:driveTarget_+delta*(step/remaining);
    }
    // Route cameras ride their POTI route at its points' speeds, interpolated
    // along each segment, and stop at its end.
    if(c.type!=4&&c.route<routes_.size()){
        const auto& r=routes_[c.route];
        if(r.points.size()>=2){
            float covered=0;std::size_t n=0;
            for(;n+1<r.points.size();++n){const float len=length(r.points[n+1]-r.points[n]);if(covered+len>=routeDistance_)break;covered+=len;}
            if(n+1<r.points.size()){
                const float len=std::max(length(r.points[n+1]-r.points[n]),.001f),u=std::clamp((routeDistance_-covered)/len,0.f,1.f);
                driveEye_=r.points[n]+(r.points[n+1]-r.points[n])*u;
                routeDistance_+=r.speeds[n]+(r.speeds[n+1]-r.speeds[n])*u;
            }else driveEye_=r.points.back();
        }
    }
    if(++driveElapsed_>=c.time){
        // A next index past the chain ends the drive.
        if(c.next<driveCameras_.size())beginDriveCamera(c.next);
        else driveCamera_=-1;
    }
}

bool AwardCeremony::step(unsigned ticks){
    for(unsigned t=0;t<ticks;++t){
        ++tick_;
        if(skipping_){if(++ending_>=FadeTicks)return false;continue;}
        if(driving_){
            ++driveTicks_;stepDriveCamera();
            if(driveCamera_<0)driving_=false;
            continue;
        }
        if(shot_>=cameras_.size())return false;
        const auto& c=cameras_[shot_];
        if(!standings_)++podiumTicks_;
        if((setup_.won||setup_.team)&&confettiTexture_.ready){
            // Confetti: a burst as the podium opens, then a steady fall.
            unsigned seed=tick_*2654435761u;
            auto random=[&](){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
            static constexpr uint32_t Colors[6]={0xff4040ffu,0xffd020ffu,0x40c0ffffu,0x60e060ffu,0xff80d0ffu,0xffffffffu};
            const unsigned spawn=podiumTicks_==1?60:(tick_%3==0?1:0);
            for(unsigned n=0;n<spawn&&confetti_.size()<160;++n)
                confetti_.push_back({random()*display::Width,-10.f-random()*120.f,(random()-.5f)*.6f,.7f+random()*.9f,random()*6.28f,3.f+random()*3.f,Colors[unsigned(random()*6)%6]});
            for(auto& p:confetti_){p.x+=p.vx+std::sin(p.phase)*.35f;p.y+=p.vy;p.phase+=.08f;}
            confetti_.erase(std::remove_if(confetti_.begin(),confetti_.end(),[](const Confetti& p){return p.y>display::Height+10;}),confetti_.end());
        }
        if(standings_){++standingsTicks_;continue;}
        if(++shotTick_>=c.count+c.hold){
            if(shot_+1<cameras_.size()){++shot_;shotTick_=0;}
            else{standings_=true;standingsTicks_=0;}
        }
    }
    return true;
}

void AwardCeremony::skip(){skipping_=true;}

float AwardCeremony::fade() const{
    const float in=tick_<FadeTicks?1.f-float(tick_)/FadeTicks:0.f;
    const float out=skipping_?float(ending_)/FadeTicks:0.f;
    // A short dip to black at the cut from the drive to the podium.
    // Not in the standings: skipped from the drive, podiumTicks_ stays 0 and this held it black.
    float cut=!driving_&&!standings_&&driveTicks_>0&&podiumTicks_<15?1.f-float(podiumTicks_)/15.f:0.f;
    // A dip to black into the standings, too.
    if(standings_&&standingsTicks_<15)cut=std::max(cut,1.f-float(standingsTicks_)/15.f);
    return std::clamp(std::max({in,out,cut}),0.f,1.f);
}

void AwardCeremony::drawKarts(Renderer& renderer){
    for(unsigned i=0;i<setup_.count;++i){
        auto& k=karts_[i];if(!k.body||k.lane.size()<2)continue;
        const float d=kartDistance(i);
        // Facing along the lane, smoothed over a short stretch either side.
        const Vec3 p=kartPosition(i,d),forward=normalized(kartPosition(i,d+150.f)-kartPosition(i,d-150.f),{0,0,1});
        const Vec3 side=normalized(cross({0,1,0},forward),{1,0,0}),up=cross(forward,side);
        const C3D_Mtx body=frame(side,up,forward,p+up*k.lift);
        renderer.draw(*k.body,body);
        if(k.animation&&k.clip>=0){
            const float duration=std::max(k.animation->duration(unsigned(k.clip)),1.f);
            k.animation->apply(*k.driver,k.clip,std::fmod(float(tick_),duration),tick_);
        }
        C3D_Mtx seated=body;Mtx_Translate(&seated,0,k.choice->seatY,k.choice->seatZ,true);
        // GPU-skinned drivers need their bone palette bound (as the race's drawSkinned).
        if(k.animation&&k.animation->gpuSkinned())renderer.setSkinPalette(k.animation->palette(),k.animation->matrixCount(),21);
        renderer.draw(*k.driver,seated);
        renderer.setSkinPalette(nullptr,0,12);
        const float roll=d/k.radius;
        for(unsigned w=0;w<(k.choice->bike?2u:4u);++w){
            C3D_Mtx wheel=body;Mtx_Translate(&wheel,k.wheelAt[w].x,k.wheelAt[w].y,k.wheelAt[w].z,true);Mtx_RotateX(&wheel,roll,true);
            renderer.draw(*k.wheels[w],wheel);
        }
    }
}

void AwardCeremony::drawOverlay(Renderer& renderer,unsigned settled,bool standings){
    // The top line: the cup icon, engine class and cup name at the left, RANK:
    // and the grade at the right; then the headline under it (AwardType,
    // AwardRank and AwardCongratulations), sized to fit the 424-unit canvas.
    if(settled<20)return;
    const float alpha=std::min(1.f,float(settled-20)/20.f);
    auto text=[&](const Text& t,float x,float y,float height,uint32_t top,uint32_t bottom,float anchor){
        if(!t.width||!t.texture.ready)return 0.f;
        const float w=t.width*height/t.height;
        renderer.drawScreenFontSprite(t.texture,x-w*anchor,y-height*.5f,w,height,t.width/float(t.texture.gpu.width),t.height/float(t.texture.gpu.height),
                                      alpha,top,bottom,0x00000000u,0xffffffffu);
        return w;
    };
    constexpr uint32_t Gold0=0xfff8a0ffu,Gold1=0xffc020ffu;
    constexpr float Top=22.f,Icon=28.f,Line=12.f;
    // Teams have no cup, class or grade: the headline only.
    if(setup_.team){
        if(standings){
            // AwardItemWin: the mode at the top left, "The red/blue team
            // wins!" over the two columns in the winners' colour.
            text(modeText_,12.f,Top,Line,Gold0,Gold1,0.f);
            if(setup_.winner==0)text(teamText_,display::CenterX,58.f,22.f,0xffd8d8ffu,0xff6060ffu,.5f);
            else text(teamText_,display::CenterX,58.f,22.f,0xd8e8ffffu,0x5888ffffu,.5f);
            return;
        }
        if(setup_.won)text(headline_,display::CenterX,62.f,24.f,Gold0,Gold1,.5f);
        else text(headline_,display::CenterX,62.f,22.f,0xc8ffffffu,0x40d8ffffu,.5f);
        return;
    }
    if(cupIcon_.ready)
        renderer.drawScreenSpriteRegion(cupIcon_,12.f,Top-Icon*.5f,Icon,Icon,0,0,128.f/float(cupIcon_.gpu.width),128.f/float(cupIcon_.gpu.height),alpha);
    float x=12.f+Icon+5.f;
    x+=text(classText_,x,Top,Line,Gold0,Gold1,0.f);
    text(cupName_,x+4.f,Top,Line,Gold0,Gold1,0.f);
    const float gradeWidth=text(gradeText_,display::Width-12.f,Top,Line*1.3f,Gold0,Gold1,1.f);
    text(rankLabel_,display::Width-16.f-gradeWidth,Top,Line,Gold0,Gold1,1.f);
    if(standings){
        // The place line in pink; "Better luck next time!" in cyan.
        if(setup_.won&&setup_.place<=3)text(placeText_,display::CenterX,58.f,20.f,0xfff4faffu,0xffb0d8ffu,.5f);
        else text(placeText_,display::CenterX,58.f,20.f,0xc8ffffffu,0x40d8ffffu,.5f);
    }else if(setup_.won)text(headline_,display::CenterX,62.f,24.f,Gold0,Gold1,.5f);
    else text(headline_,display::CenterX,62.f,22.f,0xc8ffffffu,0x40d8ffffu,.5f);
}

void AwardCeremony::draw(Renderer& renderer,void (*standingsBoard)(void*),void* context){
    if(cameras_.empty())return;
    const unsigned shot=std::min<unsigned>(shot_,unsigned(cameras_.size()-1));
    const auto& c=cameras_[shot];
    renderer.setRetailLighting(courseLighting("Race/Course/castle_course.szs"));
    if(driving_){
        // The winning run's own course cameras.
        renderer.setFov(std::clamp(driveFov_,5.f,120.f));
        renderer.begin(driveEye_,driveTarget_,{0,1,0});
    }else{
        const float* f=&c.frames[std::size_t(std::min(standings_?c.count-1:shotTick_,c.count-1))*13];
        // NW4R rotate camera: the view rows from X, Y, Z rotation in degrees.
        constexpr float Rad=3.14159265f/180.f;
        const float sx=std::sin(f[3]*Rad),cx=std::cos(f[3]*Rad),sy=std::sin(f[4]*Rad),cy=std::cos(f[4]*Rad),sz=std::sin(f[5]*Rad),cz=std::cos(f[5]*Rad);
        const Vec3 eye{f[0],f[1],f[2]};
        const Vec3 up{sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy};
        const Vec3 back{cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy};
        const bool aim=f[6]!=0||f[7]!=0||f[8]!=0;
        const Vec3 target=aim?Vec3{f[6],f[7],f[8]}:eye-back*1000.f;
        lastEye_=eye;lastTarget_=target;
        renderer.setFov(f[10]>1.f?f[10]:40.f);
        if(standings_&&setup_.won&&!setup_.team){
            // The standings look up from the podium into the sky, where the
            // trophy turns (the losing scene keeps its view).
            const Vec3 ahead=normalized(Vec3{target.x-eye.x,0,target.z-eye.z},{0,0,-1});
            renderer.begin(eye,eye+ahead*1000.f+Vec3{0,700.f,0},{0,1,0});
        }else renderer.begin(eye,target,aim?Vec3{0,1,0}:up);
    }
    C3D_Mtx identity;Mtx_Identity(&identity);
    renderer.setCourseLighting(true);
    for(auto& model:sky_)renderer.draw(*model,identity,true);
    for(auto& model:stage_)renderer.draw(*model,identity);
    if(stand_)renderer.draw(*stand_,identity);
    // Racers, karts and the trophy keep the course grade: v3 models otherwise
    // fall to the legacy 0.35 floor and render nearly black.
    std::array<Renderer::ShadowVolume,6> shadows{};unsigned shadowCount=0;
    if(driving_)drawKarts(renderer);
    else{
        for(unsigned i=0;i<setup_.count&&!(standings_&&setup_.won&&!setup_.team);++i){
            auto& r=racers_[i];if(!r.model)continue;
            if(r.animation&&r.clip>=0){
                const float duration=std::max(r.animation->duration(unsigned(r.clip)),1.f);
                float frame=float(podiumTicks_);
                frame=r.animation->loop(unsigned(r.clip))?std::fmod(frame,duration):std::min(frame,duration-1.f);
                r.animation->apply(*r.model,r.clip,frame,tick_,r.face,frame);
            }
            C3D_Mtx m;Mtx_Identity(&m);Mtx_Translate(&m,r.position.x,r.position.y,r.position.z,true);
            renderer.draw(*r.model,m);
            if(shadow_)shadows[shadowCount++]={shadow_.get(),m};
        }
        if(trophy_&&standings_&&setup_.won&&!setup_.team){
            // The cup's trophy in the sky behind the standings, turning slowly.
            const Vec3 ahead=normalized(Vec3{lastTarget_.x-lastEye_.x,0,lastTarget_.z-lastEye_.z},{0,0,-1});
            const Vec3 at=lastEye_+ahead*900.f+Vec3{0,560.f,0};
            C3D_Mtx m;Mtx_Identity(&m);Mtx_Translate(&m,at.x,at.y,at.z,true);
            Mtx_RotateY(&m,float(tick_)*.012f,true);Mtx_Scale(&m,2.6f,2.6f,2.6f);
            renderer.draw(*trophy_,m);
        }
    }
    renderer.setCourseLighting(false);
    if(shadowCount)renderer.drawShadowVolumes(shadows.data(),shadowCount,.5f);
    if(!driving_){
        if(confettiTexture_.ready&&!standings_)for(const auto& p:confetti_){
            const float w=p.size*std::fabs(std::cos(p.phase)),h=p.size;
            renderer.drawScreenSpriteRegion(confettiTexture_,p.x-w*.5f,p.y-h*.5f,std::max(w,.5f),h,0,0,16.f/float(confettiTexture_.gpu.width),16.f/float(confettiTexture_.gpu.height),1.f,p.color);
        }
        // The words over the last shot once it has settled, then the standings.
        if(standings_){
            drawOverlay(renderer,standingsTicks_+20,true);
            if(standingsBoard)standingsBoard(context);
        }else if(shot_+1>=cameras_.size())drawOverlay(renderer,shotTick_,false);
    }
    if(const float a=fade();a>0)renderer.drawScreenFade(a,false);
    renderer.end();
}
}
