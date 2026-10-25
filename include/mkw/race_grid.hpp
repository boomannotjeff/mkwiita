#pragma once
#include <mkw/math.hpp>
#include <mkw/cpu_speed.hpp>
#include <mkw/cpu_ai.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kinoko::System { class RaceConfig; }
namespace mkw {
// KMP ENPT/ENPH data with bounded parsing. The route is Nintendo-authored;
// pursuit steering and recovery are adapted; the speed kernel is retail-derived.
class EnemyRoute {
public:
    static constexpr unsigned Invalid=0xffff;
    // ENPT: setting1 u16, setting2 u8 (drift: 1 end, 2 forbid, 3 force), setting3 u8 flags.
    struct Point { Vec3 position; float width=0; uint16_t setting1=0,setting2=0; uint8_t driftSetting=0,flags=0; std::array<unsigned,6> next{}; unsigned nextCount=0; bool requiresBoost=false; };
    bool load(const void* bytes,size_t size,bool itemPath=false);
    void clear(){points_.clear();}
    unsigned size() const{return unsigned(points_.size());}
    const Point& point(unsigned index) const{return points_[index];}
    unsigned successor(unsigned index,unsigned racer) const;
    unsigned nearest(Vec3 position,Vec3 forward,unsigned racer) const;
    Vec3 target(unsigned segment,unsigned racer,Vec3 position,float lookahead,bool horizontalRecovery=false) const;
private:
    std::vector<Point> points_;
};

class RaceGrid {
public:
    static constexpr unsigned Capacity=12;
    // The menu RaceConfig a race runs under (retail RaceConfig +0xB70 game mode, +0xB8C race
    // number, each player's points +0xD8, GlobalContext +0x74 vehicle rule). Set before the
    // race scene is created; configure() and reset() read it.
    struct Session {
        enum class Mode : uint8_t { GrandPrix=0, Versus=1, TimeTrial=2, Battle=3 };
        Mode mode=Mode::Versus;
        unsigned vehicles=2;      // 0 karts only, 1 bikes only, 2 both
        unsigned raceNumber=0;    // 0: the first race of the Grand Prix / VS set
        std::array<uint16_t,Capacity> points{}; // every racer's points before this race
        bool newLineup=true;      // a new set from the menus: draw the CPU racers again
        uint32_t seed=1;          // the menus' Util::Random seed (retail: the OS time)
    };
    void setSession(const Session& session){session_=session;lineupPending_|=session.newLineup;}
    // Call within the RaceConfig init callback. Count=1 keeps a solo race.
    void configure(Kinoko::System::RaceConfig& config,unsigned localGarageChoice,unsigned count=Capacity);
    // Call after every scene creation/restart. Owns copies, never scene pointers.
    bool reset();
    // Call immediately before scene.calc. Player zero remains human unless
    // its bit in bulletPlayers is set. Bullet guidance applies to every racer;
    // CPU inputs never press the item key.
    void beforeStep(uint16_t bulletPlayers=0);
    void afterStep();
    unsigned count() const{return count_;}
    bool routeReady() const{return route_.size()!=0;}
    uint16_t bulletNoEndPlayers() const;
    unsigned rank(unsigned player=0) const{return player<count_?ranks_[player]:0;}
    bool finished(unsigned player=0) const{return player<count_&&finished_[player];}
    unsigned lap(unsigned player=0) const{return player<count_?laps_[player]:0;}
    unsigned garageChoice(unsigned player) const{return player<count_?choices_[player]:0;}
    unsigned routePoint(unsigned player) const{return player<count_?drivers_[player].segment:EnemyRoute::Invalid;}
    static float steeringTo(Vec3 forward,Vec3 offset);
    // QA autopilot: player zero follows the CPU route like the other racers.
    bool driveLocal=false;
    bool battleArena() const{return battleArena_;}
    // Grid slots (0 = pole) by racer for the next configure(), as retail starts
    // a Grand Prix's races in the previous race's finishing order; 0xff keeps
    // the racer index. Not a permutation of 0..count-1: ignored.
    void setStartOrder(const std::array<uint8_t,Capacity>& slots){startSlots_=slots;}
    void clearStartOrder(){startSlots_.fill(0xff);}
    // VS/Battle rules for the next configure()/reset(): CPU level 0 Easy,
    // 1 Normal, 2 Hard; CPU vehicles -1 the standard kart (Grand Prix), 0 All,
    // 1 karts only, 2 bikes only; seed picks each CPU's machine.
    void setCpuRules(unsigned level,int vehicles,uint32_t seed){cpuLevel_=std::min(level,2u);cpuVehicles_=vehicles;cpuSeed_=seed;}
    // Time Trial ghost: racer 1 takes this garage choice (its inputs come from
    // the recorded run, not the AI); -1 for none.
    void setGhostChoice(int choice){ghostChoice_=choice;}
    bool ghost(unsigned racer) const{return ghostChoice_>=0&&racer==1;}
    // Battle arenas, set before every beforeStep(): what battle CPUs go for.
    // Pointers are borrowed for that call only. holding/safe are racer masks
    // (an item in hand or rolling; just lost a balloon and can't lose another).
    struct BattleGoals {const Vec3* boxes=nullptr;unsigned boxCount=0;const Vec3* coins=nullptr;unsigned coinCount=0;uint16_t holding=0,safe=0;bool coinRunners=false;std::array<uint16_t,Capacity> scores{};};
    void setBattleGoals(const BattleGoals& goals){battleGoals_=goals;}
    CpuAi& ai(){return ai_;}
    const CpuAi& ai()const{return ai_;}
private:
    void forwardBumps();
    void battleDrive(unsigned racer,bool racing);
    // Battle arenas: shortest paths over the ENPT graph, driven both ways.
    // hop_[goal*n+from] is the neighbour of from on the way to goal and
    // navDist_ the path length; a spot is the closest point on any link.
    void buildBattleNav();
    struct NavSpot {unsigned a=EnemyRoute::Invalid,b=EnemyRoute::Invalid;Vec3 at{};};
    NavSpot navSpot(Vec3 p,unsigned avoid=EnemyRoute::Invalid) const;
    unsigned navHop(unsigned from,unsigned goal) const{return hop_[std::size_t(goal)*route_.size()+from];}
    float navDist(unsigned from,unsigned goal) const{return navDist_[std::size_t(goal)*route_.size()+from];}
    struct Driver {CpuSpeedState speedPolicy{};float nativeSpeed=70,desiredSpeed=70;unsigned segment=EnemyRoute::Invalid,bulletSegment=EnemyRoute::Invalid,wander=EnemyRoute::Invalid;int target=-1;unsigned stuck=0,reverse=0,recoveries=0,routeTicks=0;Vec3 last{};float bulletTurn=.03f;bool respawning=false,bullet=false;
        // Battle progress checks (where the window started, its ticks against
        // a wall) and what a failed approach rules out: during a detour the
        // graph path only, avoiding badNode; for ignoreTicks the goal at
        // ignoreAt or the enemy ignoreTarget.
        Vec3 anchor{};unsigned window=0,wallTicks=0,stuckWindows=0,detour=0,ignoreTicks=0,badNode=EnemyRoute::Invalid;Vec3 ignoreAt{};int ignoreTarget=-1;
        // The committed path: the next node, the node at the goal's lane, and
        // past it (final) straight at the goal. Re-planned only when the goal
        // leaves that lane or an approach fails: a fresh pick every tick
        // flips between lanes and leaves the kart dithering.
        unsigned navNode=EnemyRoute::Invalid,navLast=EnemyRoute::Invalid;bool navFinal=false;};
    void drawLineup(unsigned localDriver);
    EnemyRoute route_,bulletRoute_;
    CpuAi ai_;
    std::array<Driver,Capacity> drivers_{};
    std::array<unsigned,Capacity> choices_{},ranks_{},laps_{};
    std::array<bool,Capacity> finished_{};
    CpuSpeedParam speedParam_{};bool speedParamReady_=false;float lapDistance_=0;
    unsigned count_=1;bool battleArena_=false;
    unsigned cpuLevel_=1;int cpuVehicles_=-1;uint32_t cpuSeed_=0;int ghostChoice_=-1;
    BattleGoals battleGoals_{};std::vector<uint16_t> hop_;std::vector<float> navDist_;
    std::array<uint8_t,Capacity> startSlots_{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
    // The CPU line-up (garage choices), kept for every race of a Grand Prix / VS set.
    Session session_{};
    std::array<unsigned,Capacity> lineup_{};
    unsigned lineupDriver_=~0u;bool lineupPending_=true;
};
}
