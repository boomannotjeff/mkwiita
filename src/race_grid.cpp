#include <mkw/race_grid.hpp>
#include <mkw/balloon_battle.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/garage.hpp>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/KPadDirector.hh>
#include <game/system/ResourceManager.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartCollide.hh>
#include <game/kart/KartPhysics.hh>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>

namespace mkw {
namespace {
unsigned be16(const uint8_t* b){return unsigned(b[0])<<8|b[1];}
uint32_t be32(const uint8_t* b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
float beFloat(const uint8_t* b){uint32_t v=be32(b);float f;std::memcpy(&f,&v,4);return f;}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 native(const Kinoko::EGG::Vector3f& p){return {p.x,p.y,p.z};}
float horizontalProjection(Vec3 p,Vec3 a,Vec3 b){p.y=a.y=b.y=0;const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
float projection(Vec3 p,Vec3 a,Vec3 b){const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
}

bool EnemyRoute::load(const void* bytes,size_t size,bool itemPath){
    clear();if(!bytes||size<16)return false;
    const auto* b=static_cast<const uint8_t*>(bytes);
    if(std::memcmp(b,"RKMD",4)||be32(b+4)>size||be32(b+4)<16)return false;
    size=be32(b+4);
    const unsigned sections=be16(b+8),header=be16(b+10);
    if(sections>64||header<16+sections*4||header>size)return false;
    size_t enpt=0,enph=0,ptEnd=0,phEnd=0;
    for(unsigned i=0;i<sections;++i){
        const size_t off=header+size_t(be32(b+16+4*i));
        if(off>size||size-off<8)return false;
        size_t end=size;
        for(unsigned j=0;j<sections;++j){const size_t other=header+size_t(be32(b+16+4*j));if(other>off)end=std::min(end,other);}
        if(!std::memcmp(b+off,itemPath?"ITPT":"ENPT",4)){enpt=off;ptEnd=end;}
        if(!std::memcmp(b+off,itemPath?"ITPH":"ENPH",4)){enph=off;phEnd=end;}
    }
    if(!enpt||!enph)return false;
    const unsigned count=be16(b+enpt+4),groups=be16(b+enph+4);
    if(count<2||count>4096||groups==0||groups>255||enpt+8+count*20>ptEnd||enph+8+groups*16>phEnd)return false;
    std::vector<Point> points(count);std::vector<bool> covered(count,false);
    for(unsigned i=0;i<count;++i){const auto* p=b+enpt+8+i*20;auto& v=points[i];
        v.position={beFloat(p),beFloat(p+4),beFloat(p+8)};v.width=beFloat(p+12);v.setting1=be16(p+16);v.setting2=be16(p+18);v.driftSetting=p[18];v.flags=p[19];
        if(!finite(v.position)||!std::isfinite(v.width)||v.width<0)return false;
    }
    for(unsigned g=0;g<groups;++g){const auto* p=b+enph+8+g*16;unsigned start=p[0],length=p[1];
        if(length==0||start+length>count)return false;
        for(unsigned j=start;j<start+length;++j){
            if(covered[j])return false;covered[j]=true;auto& v=points[j];
            // ENPT group-entry setting 1 requires a cutting item. Our CPU
            // driver cannot guarantee that item is held/used at entry, so use
            // ordinary branches. ITPT setting 1 has different semantics.
            v.requiresBoost=!itemPath&&points[start].setting1==1;
            if(j+1<start+length){v.next[v.nextCount++]=j+1;continue;}
            for(unsigned k=0;k<6;++k){const unsigned next=p[8+k];if(next==255)continue;
                if(next>=groups)return false;const unsigned point=b[enph+8+next*16];if(point>=count)return false;
                if(std::find(v.next.begin(),v.next.begin()+v.nextCount,point)==v.next.begin()+v.nextCount)v.next[v.nextCount++]=point;
            }
            if(v.nextCount==0)return false;
        }
    }
    if(std::find(covered.begin(),covered.end(),false)!=covered.end())return false;
    points_=std::move(points);return true;
}
unsigned EnemyRoute::successor(unsigned index,unsigned racer) const{
    if(index>=size()||!points_[index].nextCount)return Invalid;
    const auto& p=points_[index];
    std::array<unsigned,6> allowed{};unsigned count=0;
    for(unsigned n=0;n<p.nextCount;++n)if(!points_[p.next[n]].requiresBoost)allowed[count++]=p.next[n];
    return count?allowed[racer%count]:p.next[racer%p.nextCount];
}
unsigned EnemyRoute::nearest(Vec3 pos,Vec3 forward,unsigned racer) const{
    unsigned result=Invalid;float best=std::numeric_limits<float>::max();
    for(unsigned i=0;i<size();++i){if(points_[i].requiresBoost)continue;unsigned n=successor(i,racer);if(n==Invalid)continue;
        const Vec3 a=points_[i].position,d=points_[n].position-a;
        const Vec3 closest=a+d*std::clamp(projection(pos,a,a+d),0.f,1.f);Vec3 error=pos-closest;
        // Height matters at Coconut Mall's overlapping floors. Prefer a route
        // facing the same direction when equally close, but allow wrong-way recovery.
        float cost=dot(error,error);if(d.x*forward.x+d.z*forward.z<0)cost+=250000.f;
        if(cost<best){best=cost;result=i;}
    }
    return result;
}
Vec3 EnemyRoute::target(unsigned segment,unsigned racer,Vec3 pos,float lookahead,bool horizontalRecovery) const{
    if(segment>=size())return pos;
    unsigned next=successor(segment,racer);Vec3 a=points_[segment].position,z=points_[next].position;
    a=a+(z-a)*std::clamp(horizontalRecovery?horizontalProjection(pos,a,z):projection(pos,a,z),0.f,1.f);
    for(unsigned step=0;step<size();++step){const Vec3 delta=z-a;const float distance=horizontalRecovery?std::hypot(delta.x,delta.z):length(delta);
        if(distance>=lookahead&&distance>0.01f)return a+delta*(lookahead/distance);
        lookahead-=distance;a=z;segment=next;next=successor(segment,racer);z=points_[next].position;
    }
    return z;
}

void RaceGrid::configure(Kinoko::System::RaceConfig& config,unsigned localChoice,unsigned count){
    constexpr unsigned driverCount=GarageChoices.size()/VehiclesPerDriver;
    static_assert(GarageChoices.size()%VehiclesPerDriver==0&&driverCount>=Capacity);
    count_=std::clamp(count,1u,Capacity);localChoice%=GarageChoices.size();
    auto& scenario=config.raceScenario();scenario.playerCount=count_;
    const unsigned course=unsigned(scenario.course);
    battleArena_=course>=32&&course<42;
    const unsigned localDriver=localChoice/VehiclesPerDriver;
    // The grid: the set slots when they are a full permutation, else racer order.
    bool slotted=true;{std::array<bool,Capacity> used{};
        for(unsigned i=0;i<count_;++i){const unsigned slot=startSlots_[i];if(slot>=count_||used[slot]){slotted=false;break;}used[slot]=true;}}
    for(unsigned i=0;i<Capacity;++i){auto& p=scenario.players[i];
        p.startPosition=slotted&&i<count_?startSlots_[i]:0xff;
        if(i>=count_){p.type=Kinoko::System::RaceConfig::Player::Type::None;continue;}
        // CPUs take each driver's standard kart, or standard bike (garage slot 3)
        // in the bikes-only 100cc Grand Prix.
        unsigned cpuVehicle=config.raceScenario().engineClass==1?3:0;
        if(cpuVehicles_>=0){
            // VS/Battle rules: a machine of the allowed kind for each CPU
            // (garage slot order: karts 0,1,2,6,7,8; bikes 3,4,5,9,10,11).
            constexpr unsigned Karts[6]={0,1,2,6,7,8},Bikes[6]={3,4,5,9,10,11};
            uint32_t x=cpuSeed_^(i*2654435761u);x^=x>>16;x*=2246822519u;x^=x>>13;
            const unsigned pick=x%12;
            cpuVehicle=cpuVehicles_==1?Karts[pick%6]:cpuVehicles_==2?Bikes[pick%6]:pick;
        }
        choices_[i]=i?((localDriver+i)%driverCount)*VehiclesPerDriver+cpuVehicle:localChoice;
        if(i==1&&ghostChoice_>=0)choices_[i]=unsigned(ghostChoice_)%GarageChoices.size();
        const auto& choice=GarageChoices[choices_[i]];
        p.character=static_cast<Kinoko::Character>(choice.character);p.vehicle=static_cast<Kinoko::Vehicle>(choice.vehicle);
        p.type=i?Kinoko::System::RaceConfig::Player::Type::CPU:Kinoko::System::RaceConfig::Player::Type::Local;
        p.driftIsAuto=i!=0;
    }
}
bool RaceGrid::reset(){
    using namespace Kinoko;
    drivers_={};finished_={};laps_={};route_.clear();bulletRoute_.clear();hop_.clear();navDist_.clear();
    auto* config=System::RaceConfig::Instance();if(!config)return false;
    count_=config->raceScenario().playerCount;
    auto* kartObjects=Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i){
        auto* kart=kartObjects->object(i);
        // Ranks before the start follow the grid (CPU lanes read them).
        const unsigned slot=config->raceScenario().players[i].startPosition;
        ranks_[i]=slot<count_?slot+1:i+1;
        // Stat speed times the engine-class rate (retail KartMove +0x14).
        drivers_[i].nativeSpeed=kart->move()->statBaseSpeed();
        drivers_[i].last=native(kart->pos());
    }
    size_t size=0;const void* kmp=System::ResourceManager::Instance()->getFile("course.kmp",&size,System::ArchiveId::Course);
    bool ok=route_.load(kmp,size);bulletRoute_.load(kmp,size,true);
    if(battleArena_){
        // Arenas have no laps: their ENPT graph is a roaming network. CPUs
        // wander it and chase nearby enemies (battleDrive); retail Enemy::AI
        // (race laps) stays off.
        speedParamReady_=false;lapDistance_=0;bulletRoute_.clear();
        if(!ok)route_.clear();
        buildBattleNav();
        CpuAi::Setup setup;setup.route=nullptr;setup.count=count_;
        ai_.reset(setup);
        return true;
    }
    size_t paramSize=0;const void* params=System::ResourceManager::Instance()->getFile("kartAISpdParam.bin",&paramSize,System::ArchiveId::Core);
    // The chosen engine class and CPU level (VS rules; Normal otherwise).
    const unsigned engineClass=std::min<unsigned>(config->raceScenario().engineClass,2);
    speedParamReady_=readCpuSpeedParam(params,paramSize,unsigned(config->raceScenario().course),engineClass,cpuLevel_,speedParam_);
    // Distance adapter: a permitted native ENPT circuit scales checkpoint lap
    // completion. Retail uses mean CKPT circuit length; branched-path parity remains pending.
    lapDistance_=0;std::vector<bool> visited(route_.size(),false);unsigned point=0;
    while(point<route_.size()&&!visited[point]){visited[point]=true;unsigned next=route_.successor(point,0);lapDistance_+=length(route_.point(next).position-route_.point(point).position);point=next;}
    // AISpeedRace compares against the retail mean-checkpoint distance scale.
    // ENPT paths include long authored branch chords, so using their raw loop
    // length suppresses the catch-up response on several courses.
    const float routeLength=lapDistance_;
    lapDistance_=std::clamp(lapDistance_,4500.f,9000.f);
    afterStep();
    // Retail Enemy::AI (cpu_ai.cpp): the chosen engine class and CPU level (cpuMode).
    CpuAi::Setup setup;setup.route=&route_;setup.count=count_;setup.engineClass=engineClass;setup.cpuMode=cpuLevel_;
    setup.speed=speedParam_;setup.speedReady=speedParamReady_;
    size_t actionSize=0;const void* actions=System::ResourceManager::Instance()->getFile("kartAIActionParam.bin",&actionSize,System::ArchiveId::Core);
    setup.actionsReady=readCpuActionParam(actions,actionSize,setup.cpuMode,setup.actions);
    if(!setup.actionsReady)for(auto& a:setup.actions)a={50,10,70,70,80,50,0,0};
    setup.courseLength=std::max(routeLength,1000.f);setup.humanMask=ghostChoice_>=0&&count_>1?3:1;
    setup.seed=0x9e3779b9u^(unsigned(config->raceScenario().course)*2654435761u);
    ai_.reset(setup);
    return ok;
}
float RaceGrid::steeringTo(Vec3 forward,Vec3 offset){
    // Kinoko's positive stick produces negative yaw (KartMove::calcTurn).
    const float error=std::atan2(forward.z*offset.x-forward.x*offset.z,forward.x*offset.x+forward.z*offset.z);
    return std::clamp(-error*2.3f,-1.f,1.f);
}
void RaceGrid::beforeStep(uint16_t bulletPlayers){
    using namespace Kinoko;
    auto* manager=System::RaceManager::Instance();auto* objects=Kart::KartObjectManager::Instance();
    if(!manager||!objects)return;
    const bool racing=manager->isStageReached(System::RaceManager::Stage::Race);
    float leaderCompletion=manager->player(0).raceCompletion();
    for(unsigned j=1;j<count_;++j)leaderCompletion=std::max(leaderCompletion,manager->player(j).raceCompletion());
    // Retail Enemy::AI drives every CPU outside a Bullet (and player zero for QA).
    uint32_t aiMask=0;
    // A Time Trial ghost replays its recorded inputs (main.cpp): no AI at all.
    for(unsigned i=0;i<count_;++i)if(!(bulletPlayers&(1u<<i))&&(i||driveLocal)&&!ghost(i))aiMask|=1u<<i;
    if(ai_.ready()){
        // CPUs run the retail stuck handler inside CpuAi; the QA autopilot keeps
        // the adapted recovery below.
        ai_.setRecovering(0,drivers_[0].reverse>0);
        ai_.step(aiMask,ranks_,laps_,finished_,System::RaceManager::Instance()->lapCount());
    }
    for(unsigned i=0;i<count_;++i){
        const bool bullet=(bulletPlayers&(1u<<i))!=0;
        auto& d=drivers_[i];
        if(i==0&&!bullet&&!driveLocal){if(d.bullet)objects->object(i)->move()->setKartSpeedLimit();d.bullet=false;continue;}
        if(ghost(i))continue;
        auto* pad=System::KPadDirector::Instance()->hostController(i);
        if(battleArena_&&!bullet){battleDrive(i,racing);continue;}
        const auto& route=bullet?bulletRoute_:route_;
        if(!route.size()){pad->setInputsRawStick(0,7,7,System::Trick::None);continue;}
        unsigned& segment=bullet?d.bulletSegment:d.segment;
        auto* kart=objects->object(i);const Vec3 pos=native(kart->pos()),forward=native(kart->bodyFront());
        bool respawning=kart->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);
        // A human may drive far beyond the last segment between Bullet uses.
        // Bullet follows retail ITPT/ITPH; ordinary CPUs follow ENPT/ENPH.
        // Reacquire with full 3D distance so stacked mall floors stay distinct.
        if(segment==EnemyRoute::Invalid||d.respawning!=respawning||length(pos-d.last)>2000.f||bullet!=d.bullet)segment=route.nearest(pos,forward,i);
        if(bullet!=d.bullet){
            d.stuck=d.reverse=d.recoveries=0;
            if(bullet){d.bulletTurn=.03f;kart->move()->clearDrift();}
            else kart->move()->setKartSpeedLimit();
        }
        if(bullet)kart->move()->setBulletSpeedLimit();
        d.bullet=bullet;
        d.last=pos;d.respawning=respawning;
        // Adapted navigation recovery, not recovered Nintendo AI: keep normal
        // 3D progress (ramps/half-pipes), but recover a segment held for six
        // seconds, or a descending chord whose pursuit target lies behind us.
        // No position, velocity, checkpoint or finish state is changed here.
        if(racing&&!respawning&&!bullet)++d.routeTicks;else d.routeTicks=0;
        float speed=std::fabs(kart->move()->speed());
        if(!bullet&&ai_.ready()){
            // Retail drive: inputs from the AI controller; it also rotates the
            // body toward the route target and sets the CPU base speed.
            const auto& command=ai_.command(i);
            uint16_t buttons=command.buttons;float aiStick=command.stick;
            float speed=std::fabs(kart->move()->speed());
            if(racing&&!respawning&&i==0){
                // QA autopilot only: adapted reverse-out recovery.
                if(speed<3.f){if(++d.stuck>150){d.reverse=70;d.stuck=0;++d.recoveries;}}else d.stuck=0;
                if(d.reverse){--d.reverse;buttons=2;aiStick=0;}
                if(d.recoveries>=3){kart->collide()->activateOob(false,nullptr,true,false);d.recoveries=0;d.reverse=0;}
                if(speed>20.f&&!d.reverse)d.recoveries=0;
            }else if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown&&manager->getCountdownTimer()<24)buttons=1;
            // Trick input from the retail AITrickHandler (CPU) / AIEngine command.
            const auto trick=racing&&!d.reverse?System::Trick(std::min<uint8_t>(command.trick,4)):System::Trick::None;
            const int raw=std::clamp(7+int(std::lround(aiStick*7.f)),0,14);
            pad->setInputsRawStick(buttons,unsigned(raw),7,trick);
            continue;
        }
        if(i&&racing&&!bullet&&speedParamReady_){
            const float gap=std::max(0.f,(leaderCompletion-manager->player(i).raceCompletion())*lapDistance_);
            // Stable target ranks adapt the source rank-manager interface. Its
            // randomized top/middle/bottom group state machine is not yet ported.
            const float policySpeed=d.speedPolicy.step(speedParam_,ranks_[i],ranks_[i],gap,laps_[i]);
            d.desiredSpeed=cpuBaseSpeed(d.nativeSpeed,policySpeed);
            kart->move()->setCpuBaseSpeed(d.desiredSpeed);
        }
        const float lookahead=std::clamp(350.f+speed*7.f,400.f,1400.f);
        const Vec3 routeA=route.point(segment).position,routeB=route.point(route.successor(segment,i)).position;
        const bool descentBehind=routeB.y<routeA.y&&
            (horizontalProjection(pos,routeA,routeB)-projection(pos,routeA,routeB))*length(routeB-routeA)>lookahead;
        const bool horizontalRecovery=!bullet&&(d.routeTicks>360||descentBehind);
        for(unsigned guard=0;guard<8;++guard){unsigned next=route.successor(segment,i);const Vec3 a=route.point(segment).position,b=route.point(next).position;
            if(bullet){
                // Retail a5b08 advances after crossing the horizontal plane
                // through the current ITPT point, along its incoming segment.
                Vec3 delta=b-a;delta.y=0;
                if(dot(pos-b,delta)<=0)break;
            }else if((horizontalRecovery?horizontalProjection(pos,a,b):projection(pos,a,b))<1.f&&length(pos-b)>180.f)break;
            segment=next;d.routeTicks=0;
        }
        Vec3 target=bullet?route.point(route.successor(segment,i)).position:route.target(segment,i,pos,lookahead,horizontalRecovery);
        float steer=steeringTo(forward,target-pos);uint16_t buttons=0;
        if(!bullet){
            // Adapted pursuit measures its heading error on the road plane.
            // Global XZ yaw is incorrect on steep/banked course surfaces.
            const Vec3 up=normalized(native(kart->move()->smoothedUp()));
            const Vec3 heading=normalized(tangent(forward,up),forward);
            const Vec3 desired=normalized(tangent(target-pos,up),heading);
            steer=std::clamp(-std::atan2(dot(cross(heading,desired),up),dot(heading,desired))*2.3f,-1.f,1.f);
        }
        if(racing&&!respawning&&bullet){
            // Retail target selection and adaptive rotation rate (a5400).
            // This ground-plane integration retains Kinoko floor/jump physics;
            // the separate retail free-flight/recovery controller is pending.
            buttons=1;d.reverse=d.recoveries=0;
            if(kart->status().offBit(Kart::eStatus::InCannon,Kart::eStatus::InAction)){
                const Vec3 up=normalized(native(kart->move()->smoothedUp()));
                const Vec3 heading=normalized(tangent(native(kart->move()->dir()),up),forward);
                const Vec3 desired=normalized(tangent(target-pos,up),heading);
                const float angle=std::atan2(dot(cross(heading,desired),up),dot(heading,desired));
                d.bulletTurn=bulletTurnRate(d.bulletTurn,angle);
                const float turn=angle*d.bulletTurn;
                EGG::Quatf correction(std::cos(turn*.5f),up.x*std::sin(turn*.5f),up.y*std::sin(turn*.5f),up.z*std::sin(turn*.5f));
                const EGG::Vector3f direction=correction.rotateVector(kart->move()->dir());
                kart->setRot(correction*kart->dynamics()->mainRot());
                kart->move()->setDir(direction);kart->move()->setVel1Dir(direction);
                // Steering is supplied above: no manual drift/hop state or
                // stale human stick is inherited by the transformed driver.
                steer=0;
            }

        }else if(racing&&!respawning){
            // Lift/brake before tight bends. All speed changes pass through
            // original KartMove acceleration/brake/road collision code.
            const float bend=std::fabs(steer);
            // Existing pursuit driver still needs corner braking. The recovered
            // base-speed controller supplies its straight-line pace and catch-up.
            const float pace=speedParamReady_?d.desiredSpeed:76.f;
            const float desired=std::clamp(pace-40.f*bend,32.f,pace);
            buttons=speed>desired+8.f?2:speed<desired?1:0;
            if(speed<3.f||d.routeTicks>360){if(++d.stuck>150){d.reverse=70;d.stuck=0;++d.recoveries;}}
            else d.stuck=0;
            if(d.reverse){
                --d.reverse;buttons=2;steer=-steer;
                // Rejoin the route at the backed-out position instead of
                // repeatedly aiming at the same inaccessible ledge.
                if(!d.reverse)segment=route.nearest(pos,forward,i);
            }
            if(d.recoveries>=3){kart->collide()->activateOob(false,nullptr,true,false);d.recoveries=0;d.reverse=0;}
            if(speed>20.f&&!d.reverse&&d.routeTicks<=360)d.recoveries=0;
        }else if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown&&manager->getCountdownTimer()<24){buttons=1;}
        const int stick=std::clamp(int(std::lround(steer*7.f))+7,0,14);
        // The recovered AITrickHandler permits tricks at AirStart, excluding
        // jump pads and item/object actions. Probability/direction choice here
        // is native policy; source vehicle trick physics remains unchanged.
        const bool trick=racing&&!bullet&&kart->status().onBit(Kart::eStatus::AirStart)&&kart->status().offBit(Kart::eStatus::JumpPad,Kart::eStatus::InAction);
        pad->setInputsRawStick(buttons,unsigned(stick),7,trick?System::Trick::Up:System::Trick::None);
    }
}
void RaceGrid::buildBattleNav(){
    hop_.clear();navDist_.clear();
    const unsigned n=route_.size();
    if(n<2||n>512)return;
    // Arena ENPT is a roaming network (the graph retail battle CPUs drive):
    // every authored link is a drivable lane, taken either way here.
    std::vector<std::vector<std::pair<unsigned,float>>> links(n);
    for(unsigned i=0;i<n;++i){const auto& p=route_.point(i);
        for(unsigned k=0;k<p.nextCount;++k){const unsigned j=p.next[k];if(j==i)continue;
            const float w=length(route_.point(j).position-p.position);
            links[i].push_back({j,w});links[j].push_back({i,w});}}
    hop_.assign(std::size_t(n)*n,uint16_t(EnemyRoute::Invalid));
    navDist_.assign(std::size_t(n)*n,std::numeric_limits<float>::max());
    using Entry=std::pair<float,unsigned>;
    for(unsigned g=0;g<n;++g){
        uint16_t* hop=&hop_[std::size_t(g)*n];float* dist=&navDist_[std::size_t(g)*n];
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
        dist[g]=0;hop[g]=uint16_t(g);open.push({0.f,g});
        while(!open.empty()){const auto [d,u]=open.top();open.pop();if(d>dist[u])continue;
            for(const auto& [v,w]:links[u])if(d+w<dist[v]){dist[v]=d+w;hop[v]=uint16_t(u);open.push({dist[v],v});}}
    }
}
RaceGrid::NavSpot RaceGrid::navSpot(Vec3 p,unsigned avoid) const{
    // Height counts double: arenas stack floors and ramps.
    NavSpot best;float bestCost=std::numeric_limits<float>::max();
    for(unsigned i=0;i<route_.size();++i){const auto& point=route_.point(i);
        if(i==avoid)continue;
        for(unsigned k=0;k<point.nextCount;++k){const unsigned j=point.next[k];if(j==avoid||j==i)continue;
            const Vec3 a=point.position,b=route_.point(j).position;
            const Vec3 at=a+(b-a)*std::clamp(projection(p,a,b),0.f,1.f),e=p-at;
            const float cost=e.x*e.x+e.z*e.z+4.f*e.y*e.y;
            if(cost<bestCost){bestCost=cost;best={i,j,at};}
        }
    }
    return best;
}
void RaceGrid::battleDrive(unsigned i,bool racing){
    using namespace Kinoko;
    auto* objects=Kart::KartObjectManager::Instance();auto* manager=System::RaceManager::Instance();
    auto* pad=System::KPadDirector::Instance()->hostController(i);
    auto* kart=objects->object(i);auto& d=drivers_[i];
    if(d.bullet){kart->move()->setKartSpeedLimit();d.bullet=false;}
    const Vec3 pos=native(kart->pos()),forward=native(kart->bodyFront());
    const bool respawning=kart->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);
    d.last=pos;d.respawning=respawning;
    if(d.detour)--d.detour;
    if(d.ignoreTicks&&!--d.ignoreTicks)d.ignoreTarget=-1;
    if(!d.detour)d.badNode=EnemyRoute::Invalid;
    auto away=[&](unsigned j){return objects->object(j)->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);};
    auto flat=[&](Vec3 p){const float dx=p.x-pos.x,dz=p.z-pos.z;return dx*dx+dz*dz;};
    // Height counts double: arenas stack floors, and a box overhead is far away.
    auto span=[&](Vec3 p){const float dy=(p.y-pos.y)*2.f;return flat(p)+dy*dy;};
    const auto& g=battleGoals_;
    const bool armed=(g.holding>>i)&1u;
    // Retail battle CPUs hunt item boxes empty-handed and enemies when armed;
    // Coin Runners CPUs collect coins between fights. Level widens the hunt.
    static constexpr float ChaseByLevel[]={3000.f,5000.f,9000.f},LeadByLevel[]={10.f,25.f,45.f},SteerByLevel[]={.8f,1.f,1.25f};
    const float Chase=ChaseByLevel[cpuLevel_],Keep=Chase*1.4f;
    // An enemy that just lost a balloon can't lose another: Balloon CPUs look
    // past it, and past one this kart just failed to reach.
    auto huntable=[&](unsigned j){return BalloonBattle::team(j)!=BalloonBattle::team(i)&&!away(j)&&int(j)!=d.ignoreTarget&&(g.coinRunners||!((g.safe>>j)&1u));};
    if(d.target>=0&&(unsigned(d.target)>=count_||!huntable(unsigned(d.target))||flat(native(objects->object(unsigned(d.target))->pos()))>Keep*Keep))d.target=-1;
    if(d.target<0){
        float best=Chase*Chase;
        for(unsigned j=0;j<count_;++j){
            if(!huntable(j))continue;
            // Coin Runners: a rich enemy is worth a longer drive.
            const float dist=flat(native(objects->object(j)->pos()))/(g.coinRunners?1.f+g.scores[j]*.1f:1.f);
            if(dist<best){best=dist;d.target=int(j);}
        }
    }
    auto ignored=[&](Vec3 p){const Vec3 e=p-d.ignoreAt;return d.ignoreTicks&&dot(e,e)<500.f*500.f;};
    auto nearest=[&](const Vec3* points,unsigned n,float limit){int best=-1;float bestDist=limit*limit;
        for(unsigned k=0;k<n;++k){if(ignored(points[k]))continue;const float dist=span(points[k]);if(dist<bestDist){bestDist=dist;best=int(k);}}return best;};
    const int box=nearest(g.boxes,g.boxCount,1e6f),coin=nearest(g.coins,g.coinCount,1e6f);
    const int nearCoin=nearest(g.coins,g.coinCount,4000.f);
    auto lead=[&](unsigned j){
        // Aim where the enemy will be when this kart gets there.
        const auto* m=objects->object(j)->move();const Vec3 at=native(objects->object(j)->pos());
        const float ticks=std::min(std::sqrt(flat(at))/std::max(std::fabs(kart->move()->speed()),30.f),LeadByLevel[cpuLevel_]);
        return at+native(m->vel1Dir())*(m->speed()*ticks);
    };
    bool hasGoal=true,pickup=true;Vec3 goal=pos+forward*1000.f;
    auto hunt=[&]{pickup=false;return lead(unsigned(d.target));};
    if(g.coinRunners){
        if(nearCoin>=0&&!(armed&&d.target>=0&&flat(native(objects->object(unsigned(d.target))->pos()))<1500.f*1500.f))goal=g.coins[nearCoin];
        else if(armed&&d.target>=0)goal=hunt();
        else if(!armed&&box>=0&&(coin<0||span(g.boxes[box])<span(g.coins[coin])))goal=g.boxes[box];
        else if(coin>=0)goal=g.coins[coin];
        else if(d.target>=0)goal=hunt();
        else hasGoal=false;
    }else{
        if(armed&&d.target>=0)goal=hunt();
        else if(!armed&&box>=0)goal=g.boxes[box];
        else if(d.target>=0)goal=hunt();
        else hasGoal=false;
    }
    // Where to steer: the goal itself when it is close on the same level (or
    // on this kart's own lane), else the next node of the shortest path over
    // the route graph, so a wall between the two is driven around.
    Vec3 steerAt=goal;bool direct=true;unsigned steerNode=EnemyRoute::Invalid;
    auto dropPath=[&]{d.navNode=d.navLast=EnemyRoute::Invalid;d.navFinal=false;};
    if(hasGoal){
        d.segment=EnemyRoute::Invalid; // re-found on the way back to roaming
        const bool close=flat(goal)<1500.f*1500.f&&std::fabs(goal.y-pos.y)<300.f;
        if(hop_.empty()||(!d.detour&&close)||respawning)dropPath();
        else{
            const NavSpot to=navSpot(goal);
            if(to.a<route_.size()&&((d.navLast!=to.a&&d.navLast!=to.b)||(!d.navFinal&&d.navNode>=route_.size()))){
                dropPath();
                const NavSpot from=navSpot(pos,d.badNode);
                if(from.a>=route_.size()||(from.a==to.a&&from.b==to.b)||(from.a==to.b&&from.b==to.a)){d.navFinal=true;d.navLast=to.a;}
                else{
                    // The lane ends (this kart's, then the goal's) with the shortest way through.
                    float best=std::numeric_limits<float>::max();
                    for(const unsigned e:{from.a,from.b})for(const unsigned f:{to.a,to.b}){
                        const float cost=length(route_.point(e).position-from.at)+navDist(e,f)+length(goal-route_.point(f).position);
                        if(cost<best){best=cost;d.navNode=e;d.navLast=f;}
                    }
                }
            }
            // Within reach of a node: on to the next one, or past the last
            // one straight at the goal along its lane.
            constexpr float Reach=800.f;
            for(unsigned guard=0;guard<4&&!d.navFinal&&d.navNode<route_.size()&&flat(route_.point(d.navNode).position)<Reach*Reach;++guard){
                if(d.navNode==d.navLast)d.navFinal=true;else d.navNode=navHop(d.navNode,d.navLast);
            }
            if(!d.navFinal&&d.navNode<route_.size()){steerAt=route_.point(d.navNode).position;steerNode=d.navNode;direct=false;}
            else if(!d.navFinal)dropPath();
        }
    }else if(route_.size()){
        dropPath();
        // Roam the arena's route graph, varying the branch at every junction.
        if(d.segment>=route_.size()||d.wander>=route_.size()||respawning){
            d.segment=route_.nearest(pos,forward,i);
            d.wander=d.segment<route_.size()?route_.successor(d.segment,i+d.routeTicks):EnemyRoute::Invalid;
        }
        if(d.wander<route_.size()){
            const auto& next=route_.point(d.wander);
            const float reach=std::max(next.width,600.f);
            if(flat(next.position)<reach*reach){
                ++d.routeTicks;d.segment=d.wander;
                d.wander=route_.successor(d.segment,i*7+d.routeTicks*3);
            }
            if(d.wander<route_.size())steerAt=route_.point(d.wander).position;
        }
        direct=false;
    }
    float steer=std::clamp(steeringTo(forward,{steerAt.x-pos.x,0.f,steerAt.z-pos.z})*SteerByLevel[cpuLevel_],-1.f,1.f);
    uint16_t buttons=0;
    if(racing&&!respawning){
        buttons=1;
        // Progress over one-second windows: barely moved, or most of it
        // pressed against a wall without getting far, is stuck (pinned by a
        // wall, a kart or an object), whatever the speedometer says.
        if(!d.window)d.anchor=pos;
        if(kart->status().onBit(Kart::eStatus::WallCollision,Kart::eStatus::Wall3Collision))++d.wallTicks;
        if(++d.window>=60){
            const float moved=std::sqrt(flat(d.anchor));
            const bool pinned=moved<250.f,walled=d.wallTicks>=40&&moved<2000.f;
            d.stuckWindows=pinned||walled?d.stuckWindows+1:0;
            if(walled||d.stuckWindows>=2){
                // Back out, then take the graph's way around for three
                // seconds, off the node this approach was heading for, and
                // give up on the goal for five.
                d.reverse=pinned||d.stuckWindows>=2?50:20;d.stuckWindows=0;
                d.detour=180;
                d.badNode=steerNode;dropPath();
                d.ignoreTicks=300;
                d.ignoreAt={1e9f,1e9f,1e9f};
                if(hasGoal&&!pickup){d.ignoreTarget=d.target;d.target=-1;}
                else if(hasGoal)d.ignoreAt=goal;
                else d.segment=EnemyRoute::Invalid;
            }
            d.window=0;d.wallTicks=0;
        }
        // A goal just behind sits inside the turning circle: back into it
        // instead of orbiting it at full lock.
        const bool backUp=hasGoal&&pickup&&direct&&flat(goal)<900.f*900.f&&(goal.x-pos.x)*forward.x+(goal.z-pos.z)*forward.z<0.f;
        if(d.reverse||backUp){if(d.reverse)--d.reverse;buttons=2;steer=-steer;}
    }else{
        d.window=d.wallTicks=d.stuckWindows=d.reverse=0;
        if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown&&manager->getCountdownTimer()<24)buttons=1;
    }
    const int stick=std::clamp(int(std::lround(steer*7.f))+7,0,14);
    pad->setInputsRawStick(buttons,unsigned(stick),7,System::Trick::None);
}
uint16_t RaceGrid::bulletNoEndPlayers() const{
    uint16_t mask=0;
    for(unsigned i=0;i<count_;++i){const auto& d=drivers_[i];if(!d.bullet||d.bulletSegment>=bulletRoute_.size())continue;
        const unsigned next=bulletRoute_.successor(d.bulletSegment,i);
        // Retail KartKiller::readPointSettings (.text a6288), decimal ones
        // digit of ITPT setting2: 1 means the Bullet must not end here.
        if(next<bulletRoute_.size()&&bulletRoute_.point(next).setting2<32768&&bulletRoute_.point(next).setting2%10==1)mask|=uint16_t(1u<<i);
    }
    return mask;
}
void RaceGrid::afterStep(){
    auto* manager=Kinoko::System::RaceManager::Instance();if(!manager)return;
    forwardBumps();
    std::array<unsigned,Capacity> order{};for(unsigned i=0;i<count_;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+count_,[&](unsigned a,unsigned b){
        const auto& x=manager->player(a);const auto& y=manager->player(b);
        if(x.finished()!=y.finished())return x.finished();
        if(x.finished())return x.finishOrder()<y.finishOrder();
        if(x.raceCompletion()!=y.raceCompletion())return x.raceCompletion()>y.raceCompletion();
        return a<b;
    });
    for(unsigned rank=0;rank<count_;++rank)ranks_[order[rank]]=rank+1;
    // Raceinfo maxLap (RaceinfoPlayer+0x26), not currentLap (+0x24): CtrlRaceLap
    // (0x807EF810), JugemSwitchLap (0x80725BD8) and the AI all read it. endLap
    // (0x805349B8) raises it only past the old max, so backing over the line
    // (decrementLap) never lowers it. Running max of currentLap = maxLap.
    for(unsigned i=0;i<count_;++i){const auto& p=manager->player(i);finished_[i]=p.finished();laps_[i]=std::max(laps_[i],unsigned(std::clamp<int>(p.currentLap(),1,manager->lapCount())));}
}
void RaceGrid::forwardBumps(){
    using namespace Kinoko;
    // Retail racer-to-racer collision now runs inside the vendored kart pass
    // (KartCollide::checkKartCollision); fn_1_609F4 then calls AI::handleBump
    // on the bumped racer with the applied lateral force.
    auto* objects=Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i){
        const auto* collide=objects->object(i)->collide();
        if(collide->kartBumped())ai_.bump(i,native(collide->kartBumpForce()));
    }
}
}
