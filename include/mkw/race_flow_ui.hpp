#pragma once
#include <memory>

namespace mkw {
class Renderer;
class RaceResultsFlow;
// Retail race-flow UI drawn over the race: the out-of-bounds wipe
// (game_image/wipe.brlyt), the VS result boards (result/position_12players)
// and the end-of-race buttons (button/common_w004_menu). Textures are cooked
// by tools/cook_race_flow.py; the wipe is resident with the race, the board
// and menu textures only from the player's finish until the race is released.
class RaceFlowUi {
public:
 RaceFlowUi();~RaceFlowUi();
 bool load();
 void unload();
 // Loads one board/menu texture per call (the FINISH animation leaves time
 // before the board); true once all are resident, false on a failed load.
 bool loadResultsStep();
 void unloadResults();
 bool resultsLoaded()const;
 bool resultsFailed()const;
 unsigned resultsBytes()const;
 // WipeControl::OnUpdate (USA 0x8064E80C): the Normal animation at the
 // wipe's progress (0..1); nothing is drawn below 0.
 void drawWipe(Renderer& renderer,float progress);
 // One 60 Hz tick of the board's loop animations and the buttons'
 // PushButton animation groups.
 void step(const RaceResultsFlow& flow);
 void drawResults(Renderer& renderer,const RaceResultsFlow& flow);
 // The course preview banner (demo/cup_name + course_name): the mode line
 // beside its icon (FlowIntroIcons index) at the top left, the course name at
 // the bottom right, faded by alpha.
 void drawIntro(Renderer& renderer,const char* mode,unsigned icon,const char* course,float alpha);
 // The award ceremony's two-column standings instead of the race board.
 void setAwardLayout(bool award);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
