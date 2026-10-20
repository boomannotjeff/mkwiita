#pragma once
#include <algorithm>
#include <memory>
#include <mkw/math.hpp>
#include <mkw/race_hud_layout.hpp>
#include <mkw/visibility.hpp>
namespace mkw {
class Renderer;
class Model;
class RaceItems;
struct CountdownCue {int digit=0;bool go=false;float scale=1,alpha=1;unsigned frame=0;};
// The original 240-tick countdown. RacePage_afterCalc (USA 0x80835A28) sets
// count n's message (3, 2, 1, GO) and starts its animation at timer
// n*60 + 51, so the zoom-in lands on frame 9 with the beep and lamp at
// (n+1)*60; GO lands as physics unlocks at 240.
inline constexpr unsigned CountdownCueStart=51;
inline CountdownCue countdownCue(unsigned stage,unsigned timer){
    CountdownCue cue;
    constexpr unsigned GoStart=CountdownCueStart+3*60;
    if(stage==1&&timer>=CountdownCueStart&&timer<GoStart){
        cue.digit=3-int((timer-CountdownCueStart)/60);
        cue.frame=(timer-CountdownCueStart)%60;
    }else if((stage==1||stage==2)&&timer>=GoStart&&timer<GoStart+HudCountFrames.size()){
        cue.go=true;cue.frame=timer-GoStart;
    }
    if(cue.digit||cue.go){cue.scale=HudCountFrames[cue.frame][0].scale;cue.alpha=HudCountFrames[cue.frame][0].alpha;}
    return cue;
}
class RaceHud {
public:
    RaceHud();~RaceHud();
    bool load();void reset();
    // Source presentation recurrences advance at 60 Hz, independent of rendering.
    // started: the race frame counter runs (countdown onward); HUD counters slide in.
    void stepWorld(const RaceItems&,Vec3 cameraBackward,bool started=true);
    void drawWorld(Renderer&,const RaceItems&,Vec3 viewer,unsigned tick);
    // finishFrame: frames since the player finished (FINISH animation), or -1.
    // exitSlide: the race-end CtrlRaceBase hide slide 0..1, or -1.
    void drawOverlay(Renderer&,const RaceItems&,unsigned stage,unsigned timer,unsigned rank,unsigned lap,bool finished,unsigned tick,int finishFrame=-1,float exitSlide=-1.f);
    bool speedometer=false; // off as on the Wii; SELECT toggles it during a race
    unsigned lapCount=3;    // the race's laps (Retro Rewind: the course's own, up to 9)
    bool teams=false;       // Battle and Team VS: name tags in the team colours
    // Battle (Balloon Battle, Coin Runners): the lap counter and position give
    // way to the team score, the player's points or coins and the +1/-1 popup.
    struct BattleView {bool active=false;unsigned red=0,blue=0,points=0,team=0;int popup=0;unsigned popupAge=0;};
    BattleView battle;
    // The battle digits and signs (textures/ui/race, cooked by tools/cook_battle.py).
    bool loadBattle();
    // The set's team totals at the top centre (battle_total_point total_point_3),
    // over the results; winner 0 red, 1 blue pulses (2: none yet).
    void drawBattleTotals(Renderer&,unsigned red,unsigned blue,unsigned winner,unsigned tick);
    // Coin Runners: the player's coins as a pile of the arena's coin model over
    // the coin count, columns of ten from the left. Call in the 3D pass.
    static constexpr unsigned CoinPileColumn=10,CoinPileColumns=3;
    void drawCoinPile(Renderer&,const Model& coin,const Bounds& bounds,unsigned coins);
private:
    void drawBattle(Renderer&,float exitLift);
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
