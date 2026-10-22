#include "mkw/vehicle_animation.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <game/kart/KartBody.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartState.hh>
#include <game/kart/KartObject.hh>
#include <game/kart/KartParamFileManager.hh>
#include <game/kart/KartSuspensionPhysics.hh>
#include <game/render/KartModel.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>

namespace mkw {
namespace {
constexpr float Pi=3.14159265358979323846f,Degrees=Pi/180.f;
constexpr const char* Names[]={"handle","f_sus","swingarm","r_sus","arm_fl","arm_fr","arm_rl","arm_rr","sus_fr"};
int accessoryIndex(const char* name){for(int i=0;i<9;++i)if(!std::strcmp(name,Names[i]))return i;return -1;}
C3D_Mtx identity(){C3D_Mtx m{};m.r[0].x=m.r[1].y=m.r[2].z=m.r[3].w=1;return m;}
float& at(C3D_Mtx& m,unsigned row,unsigned col){return m.r[row].c[3-col];}
float at(const C3D_Mtx& m,unsigned row,unsigned col){return m.r[row].c[3-col];}
C3D_Mtx multiply(const C3D_Mtx& a,const C3D_Mtx& b){
    C3D_Mtx out{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        for(unsigned k=0;k<4;++k)at(out,r,c)+=at(a,r,k)*at(b,k,c);
    return out;
}
C3D_Mtx fromSource(const Kinoko::EGG::Matrix34f& src){
    auto out=identity();for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)at(out,r,c)=src[r,c];return out;
}
Vec3 transform(const C3D_Mtx& m,Vec3 p){return {m.r[0].x*p.x+m.r[0].y*p.y+m.r[0].z*p.z+m.r[0].w,
    m.r[1].x*p.x+m.r[1].y*p.y+m.r[1].z*p.z+m.r[1].w,
    m.r[2].x*p.x+m.r[2].y*p.y+m.r[2].z*p.z+m.r[2].w};}
Vec3 inverseRigid(const C3D_Mtx& m,Vec3 p){
    p=p-Vec3{m.r[0].w,m.r[1].w,m.r[2].w};
    return {p.x*m.r[0].x+p.y*m.r[1].x+p.z*m.r[2].x,
            p.x*m.r[0].y+p.y*m.r[1].y+p.z*m.r[2].y,
            p.x*m.r[0].z+p.y*m.r[1].z+p.z*m.r[2].z};
}
C3D_Mtx local(Vec3 position={},Vec3 rotation={},Vec3 scale={1,1,1}){
    const float sx=std::sin(rotation.x),cx=std::cos(rotation.x),sy=std::sin(rotation.y),cy=std::cos(rotation.y),sz=std::sin(rotation.z),cz=std::cos(rotation.z);
    auto m=identity();
    m.r[0].x=cy*cz*scale.x;m.r[0].y=(sx*sy*cz-cx*sz)*scale.y;m.r[0].z=(cx*sy*cz+sx*sz)*scale.z;
    m.r[1].x=cy*sz*scale.x;m.r[1].y=(sx*sy*sz+cx*cz)*scale.y;m.r[1].z=(cx*sy*sz-sx*cz)*scale.z;
    m.r[2].x=-sy*scale.x;m.r[2].y=cy*sx*scale.y;m.r[2].z=cx*cy*scale.z;
    m.r[0].w=position.x;m.r[1].w=position.y;m.r[2].w=position.z;return m;
}
void scale(C3D_Mtx& m,Vec3 scale){for(unsigned r=0;r<3;++r){m.r[r].x*=scale.x;m.r[r].y*=scale.y;m.r[r].z*=scale.z;}}
bool finiteMatrix(const C3D_Mtx& m){for(const auto& row:m.r)for(float value:row.c)if(!std::isfinite(value))return false;return true;}
Vec3 vec(const Kinoko::EGG::Vector3f& v){return {v.x,v.y,v.z};}
}

void VehicleAnimation::reset(){
    luma_=false;lumaMotion_.reset();lumaFrame_=0;
    body_=handle_=handleBase_=identity();for(auto& m:wheels_)m=identity();for(auto& m:wheelFrames_)m=identity();
    for(auto& m:accessories_)m=identity();accessorySet_=0;roll_={};travel_={};radii_={};display_={};wheelLocal_={};count_=0;vehicle_=~0u;bike_=false;driveFrame_=15;
    base_=handleSource_=identity();for(auto& m:wheelSources_)m=identity();tirePositions_={};bspRadii_={};kartScale_={1,1,1};steering_=0;wheelMotion_={};dirty_=false;
}
Vec3 VehicleAnimation::displayVec(unsigned offset) const{return {display_[offset/4],display_[offset/4+1],display_[offset/4+2]};}
void VehicleAnimation::readParameters(unsigned vehicle){
    display_={};vehicle_=vehicle;
    auto* manager=Kinoko::Kart::KartParamFileManager::Instance();
    const auto id=static_cast<Kinoko::Vehicle>(vehicle);
    auto stream=bike_?manager->getBikeDispParamsStream(id):manager->getKartDispParamsStream(id);
    // These are the user's original files; reading the unused visual fields
    // doesn't change KartParam or any simulation state.
    const unsigned floats=bike_?0x58/4:0xf8/4;
    for(unsigned i=0;i<floats;++i)display_[i]=stream.read_f32();
}
float VehicleAnimation::advanceRoll(float phase,float signedDistance,float radius){
    if(!std::isfinite(signedDistance)||!std::isfinite(radius)||radius<=.0001f)return phase;
    return std::remainder(phase+signedDistance/radius,2*Pi);
}
void VehicleAnimation::step(Kinoko::Kart::KartObject& kart){
    bike_=kart.param()->isBike();count_=std::min(4u,unsigned(kart.param()->tireCount()));
    const unsigned vehicle=unsigned(Kinoko::System::RaceConfig::Instance()->raceScenario().players[kart.param()->playerIdx()].vehicle);
    if(vehicle_!=vehicle){roll_={};wheelMotion_={};readParameters(vehicle);}
    base_=fromSource(kart.pose());kartScale_=vec(kart.scale());
    steering_=std::clamp(kart.model()->steeringInput(),-1.f,1.f);
    const auto& bsp=kart.param()->bsp();
    using S=Kinoko::Kart::eStatus;
    const auto& status=kart.status();
    const auto& scenario=Kinoko::System::RaceConfig::Instance()->raceScenario();
    const unsigned character=unsigned(scenario.players[kart.param()->playerIdx()].character);
    luma_=character==unsigned(Kinoko::Character::Rosalina)||character==unsigned(Kinoko::Character::Rosalina_Biker_Outfit);
    if(luma_){
        // Inputs as MKW-3DS v0.6.0 feeds TicoModel::Update (src/vehicle_animation.cpp).
        LumaMotion::Input input;
        input.position=vec(kart.pos());input.previousPosition=vec(kart.prevPos());input.velocity=vec(kart.velocity());
        input.right={base_.r[0].x,base_.r[1].x,base_.r[2].x};
        input.poseUp={base_.r[0].y,base_.r[1].y,base_.r[2].y};
        input.poseForward={base_.r[0].z,base_.r[1].z,base_.r[2].z};
        input.bodyUp=vec(kart.bodyUp());input.bodyForward=vec(kart.bodyFront());
        input.movementUp=vec(kart.move()->smoothedUp());
        // Retail KartMove +0x74 is the velocity direction, not smoothedForward (+0x80).
        input.internalVelocityDirection=vec(kart.move()->vel1Dir());
        input.scaleY=kartScale_.y;input.hopHeight=kart.move()->hopPosY();
        input.resetPosition=unsigned(Kinoko::System::RaceManager::Instance()->stage())<2||status.onBit(S::InRespawn,S::AfterRespawn);
        input.overZipper=status.onBit(S::OverZipper);input.inCannon=status.onBit(S::InCannon);
        lumaMotion_.step(input);
        // USA 807c54c4: only the frozen rescue on N64 Sherbet Land stops its wait clip.
        if(!(unsigned(scenario.course)==27&&status.onBit(S::InRespawn,S::AfterRespawn)))++lumaFrame_;
    }
    const float speed=wheelMotion_.step({
        unsigned(Kinoko::System::RaceManager::Instance()->stage()),
        kart.move()->speed(),kart.state()->startBoostCharge(),display_[2],display_[1],
        status.onBit(S::Accelerate),status.onBit(S::Brake),status.onBit(S::TouchingGround),
        status.onBit(S::WallCollision,S::Wall3Collision),status.onBit(S::InAction),
        status.onBit(S::ChargingSSMT),status.onBit(S::Burnout),status.onBit(S::ChargeStartBoost)});
    for(unsigned i=0;i<count_;++i){
        auto* physics=kart.tirePhysics(i);
        const unsigned bspIndex=bike_?i:(count_==3?(i+1)/2:i/2);
        tirePositions_[i]=vec(physics->pos());
        travel_[i]=physics->suspTravel();radii_[i]=physics->effectiveRadius();
        // Retail Wheel::UpdateMatrix (USA 0x8059444c) uses the unscaled
        // effective radius at WheelPhysics+0x6c. Scaling only affects the
        // rendered mesh below, not the roll denominator a second time.
        roll_[i]=advanceRoll(roll_[i],speed,radii_[i]);
        wheelSources_[i]=fromSource(kart.body()->wheelMatrix(i));
        bspRadii_[i]=bsp.wheels[bspIndex].wheelRadius;
    }
    if(bike_)handleSource_=count_?wheelSources_[0]:fromSource(kart.body()->wheelMatrix(0));
    dirty_=true;
}
void VehicleAnimation::evaluate() const{
    if(!dirty_)return;
    dirty_=false;
    const auto& base=base_;const auto kartScale=kartScale_;
    body_=base;scale(body_,kartScale);
    const float steering=steering_;
    driveFrame_=std::clamp(15.f-15.f*steering,0.f,29.f);
    handleBase_=base;handle_=base;
    if(bike_){
        handleBase_=multiply(body_,local(displayVec(0x0c),displayVec(0x18)*Degrees));
        handle_=handleSource_;scale(handle_,kartScale);
        const Vec3 baseX{handleBase_.r[0].x,handleBase_.r[1].x,handleBase_.r[2].x};
        const Vec3 baseZ{handleBase_.r[0].z,handleBase_.r[1].z,handleBase_.r[2].z};
        const Vec3 steeredZ{handle_.r[0].z,handle_.r[1].z,handle_.r[2].z};
        driveFrame_=std::clamp(15.f+std::atan2(dot(baseX,steeredZ),dot(baseZ,steeredZ))/Degrees,0.f,29.f);
    }
    for(unsigned i=0;i<count_;++i){
        const unsigned bspIndex=bike_?i:(count_==3?(i+1)/2:i/2);
        const bool front=bspIndex==0;
        const Vec3 position=tirePositions_[i];
        wheelLocal_[i]=inverseRigid(base,position);
        wheelLocal_[i]={wheelLocal_[i].x/std::max(.001f,kartScale.x),wheelLocal_[i].y/std::max(.001f,kartScale.y),wheelLocal_[i].z/std::max(.001f,kartScale.z)};
        wheelFrames_[i]=wheelSources_[i];
        if(!bike_&&front)wheelFrames_[i]=multiply(wheelFrames_[i],local({},{0,-15.f*Degrees*steering,0}));
        wheelFrames_[i].r[0].w=position.x;wheelFrames_[i].r[1].w=position.y;wheelFrames_[i].r[2].w=position.z;
        wheels_[i]=multiply(wheelFrames_[i],local({},{roll_[i],0,0}));
        Vec3 meshScale=(!bike_&&!front)?displayVec(0x3c):Vec3{1,1,1};
        // Preserve source model oversize and the original rear XYZ scale. Only
        // the physical radius's live change (e.g. offroad sinking) is additional.
        const float compression=bspRadii_[i]>0?radii_[i]/bspRadii_[i]:1;
        scale(wheels_[i],{kartScale.x*meshScale.x*compression,kartScale.y*meshScale.y*compression,kartScale.z*meshScale.z*compression});
    }
    updateAccessories();
}
void VehicleAnimation::updateAccessories() const{
    // Slots this pass leaves unset read as body_ (see accessory()).
    accessorySet_=0;
    auto set=[&](unsigned slot,const C3D_Mtx& m){accessories_[slot]=m;accessorySet_|=uint64_t(1)<<slot;};
    if(bike_&&count_>=2){
        set(0,handle_);
        set(4,wheelFrames_[0]); // lower fork follows the suspended front axle
        const Vec3 pivot=displayVec(0x24),delta=wheelLocal_[1]-pivot;
        const float swingAngle=std::atan2(delta.y,-delta.z);
        set(8,multiply(body_,local(pivot,{swingAngle,0,0})));
        const Vec3 shockTop=displayVec(0x3c),shockDelta=wheelLocal_[1]-shockTop;
        // Source shock meshes point down -Y, swingarms back along -Z.
        // The source rest-length multiplier converts link length to mesh scale.
        const float shockAngle=std::atan2(-shockDelta.z,-shockDelta.y);
        const float multiplier=display_[0x54/4];
        const float stretch=multiplier>0?length(shockDelta)*multiplier:1;
        set(12,multiply(body_,local(shockTop,{shockAngle,0,0},{1,stretch,1})));
        return;
    }
    if(bike_)return;
    for(unsigned i=0;i<count_;++i){
        const bool front=i<2,reflected=(i&1)!=0;
        Vec3 pivot=displayVec(front?0x48:0x60);
        Vec3 rotation=front?displayVec(0x54):Vec3{display_[0x6c/4],0,0};
        if(reflected){pivot.x=-pivot.x;rotation.z=-rotation.z;}
        const Vec3 delta=wheelLocal_[i]-pivot;
        const float travelAngle=std::atan2(delta.y,std::abs(delta.x));
        rotation=rotation*Degrees;rotation.z+=(reflected?-travelAngle:travelAngle);
        const auto arm=multiply(body_,local(pivot,rotation));
        set((4+i)*4,arm);
        // A sole right-arm mesh is mirrored for the opposite side; all four
        // pivots remain the authored front/rear values, never bounding-box guesses.
        auto sharedArm=arm;if(!reflected)scale(sharedArm,{-1,1,1});
        set(5*4+i,sharedArm);
        Vec3 shockTop=displayVec(front?0xa8:0xbc);
        // Display file stores the right-hand shock anchor and inverse bottom X.
        const float bottomOffset=display_[front?0xb4/4:0xc8/4];
        const float inverseLength=display_[front?0xb8/4:0xcc/4];
        if(!reflected)shockTop.x=-shockTop.x;
        Vec3 bottom=wheelLocal_[i];bottom.x+=(reflected?bottomOffset:-bottomOffset);
        const Vec3 shock=bottom-shockTop;
        const float zAngle=std::atan2(shock.x,-shock.y);
        const float xAngle=std::atan2(-shock.z,std::sqrt(shock.x*shock.x+shock.y*shock.y));
        auto suspension=multiply(body_,local(shockTop,{xAngle,0,zAngle},{1,inverseLength>0?length(shock)*inverseLength:1,1}));
        if(!reflected)scale(suspension,{-1,1,1});
        set(8*4+i,suspension);
    }
}
bool VehicleAnimation::accessorySupported(const char* name) const{
    const int index=accessoryIndex(name);return count_&&index>=0&&(bike_?(index<4):(index>=4));
}
unsigned VehicleAnimation::accessoryCount(const char* name) const{
    if(!accessorySupported(name))return 0;
    if(!bike_&&!std::strcmp(name,"arm_fr")&&(vehicle_==5||vehicle_==6))return count_;
    if(!bike_&&!std::strcmp(name,"sus_fr")&&vehicle_==5)return count_;
    return 1;
}
C3D_Mtx VehicleAnimation::accessory(const char* name,bool sourceAnimated,unsigned instance) const{
    evaluate();
    const int index=accessoryIndex(name);if(index<0||!accessorySupported(name))return body_;
    if(index==0&&sourceAnimated)return handleBase_;
    const unsigned slot=unsigned(index)*4+std::min(instance,3u);
    return accessorySet_>>slot&1?accessories_[slot]:body_;
}
bool VehicleAnimation::finite() const{
    evaluate();
    if(!finiteMatrix(body_)||!finiteMatrix(handle_)||!finiteMatrix(handleBase_))return false;
    for(unsigned i=0;i<count_;++i)if(!finiteMatrix(wheels_[i])||!std::isfinite(roll_[i]))return false;
    for(unsigned i=0;i<accessories_.size();++i)if(!finiteMatrix(accessorySet_>>i&1?accessories_[i]:body_))return false;
    return true;
}
}
