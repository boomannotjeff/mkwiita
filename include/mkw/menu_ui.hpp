#pragma once
#include <mkw/menu_focus.hpp>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
namespace mkw {
enum class UiPage {Title,MainMenu,Mode,Character,Vehicle,Drift,Cup,Course,Confirm,Race,Pause,QuitConfirm,Results,Error,Strap,License,EngineClass,VSMode,BattleMode,BattleCup,BattleStage,Ghost,Intro,Rules,Options,Controls,GameSettings,LicenseSettings,Records,EraseLicense};
enum class UiAction {None,StartRace,RestartRace,ResumeRace,Exit};
enum class UiMode {GrandPrix,TimeTrials,VS,Battle};
enum class UiEngineClass {CC50,CC100,CC150,Mirror};
struct UiMainMenuControl {unsigned group;float x,y;};
// Seven source child controls in four frames. Centers combine the original
// widescreen BRCTR frame transform and child translation (source canvas units).
inline constexpr std::array<UiMainMenuControl,7> UiMainMenuControls{{
 {0,-135.f,68.8f},{1,86.4f,68.8f},{1,135.f,68.8f},{1,183.6f,68.8f},
 {2,-172.8f,-93.2f},{2,-97.2f,-93.2f},{3,135.f,-93.2f}
}};
// pageLeft/pageRight (L/R): the Retro Rewind edition's other cup pages.
struct UiInput {bool up=false,down=false,left=false,right=false,accept=false,back=false,start=false,switchType=false,pageLeft=false,pageRight=false;};
struct UiSelection {
 unsigned driver=0,vehicle=0,course=0;bool automatic=false;
 UiMode mode=UiMode::GrandPrix;UiEngineClass engineClass=UiEngineClass::CC150;
 unsigned license=0,battleStage=0;bool teams=false,coinRunners=false;
 // VS and Battle rules (Pages::VSSettings / BattleSettings): CPU level (Easy,
 // Normal, Hard), CPU vehicles (All, Karts, Bikes), course or stage order
 // (Choose, Random, In Order), items (Balanced, Aggressive, Strategic, None),
 // the VS race count and the battle wins (values of UiRaceCounts / UiBattleWins).
 unsigned cpuLevel=1,cpuVehicles=0,courseOrder=0,itemMode=0,raceCount=4,battleWins=2;
 // Time Trials: 0 race the course's ghost, 1 watch its replay, 2 alone.
 unsigned ghost=2;
 // ...from the Ghost page's card: 0 the license's own, 1 the staff ghost, 2 the expert staff ghost.
 unsigned ghostSource=0;
 // The retail unlocked engine-class page switches both 50cc/100cc entries
 // together between their restricted type and the combined kart/bike list.
 bool mixedEngineTypes=false;
 // Retro Rewind: 150cc races at 200cc (L/R on the class and rules pages, as Pulsar's setting).
 bool cc200=false;
 // Retro Rewind: the chosen character's skin (0 default; GarageSkinCounts), L/R on the character page.
 unsigned skin=0;
 bool operator==(const UiSelection&)const=default;
};
// The rules' count values (tools/cook_menu_ui.py bakes a label for each).
inline constexpr std::array<unsigned,10> UiRaceCounts{{1,2,3,4,5,6,7,8,16,32}};
inline constexpr std::array<unsigned,5> UiBattleWins{{1,2,3,4,5}};
// One rules row: its kind and option count (Count rows step through the counts).
enum class UiRuleRow {Class,Cpu,Vehicles,Courses,Items,Count,Ok};
struct UiRuleRowInfo {UiRuleRow row;unsigned options;};
// VSSettingRadioBase/UpDownBase order; BattleSettingRadioBase drops Class and
// offers three item settings.
inline constexpr std::array<UiRuleRowInfo,7> UiVSRules{{{UiRuleRow::Class,4},{UiRuleRow::Cpu,3},{UiRuleRow::Vehicles,3},{UiRuleRow::Courses,3},{UiRuleRow::Items,4},{UiRuleRow::Count,1},{UiRuleRow::Ok,1}}};
inline constexpr std::array<UiRuleRowInfo,6> UiBattleRules{{{UiRuleRow::Cpu,3},{UiRuleRow::Vehicles,3},{UiRuleRow::Courses,3},{UiRuleRow::Items,3},{UiRuleRow::Count,1},{UiRuleRow::Ok,1}}};
struct UiCharacterInfo {const char* name;unsigned retailId,weight;};
// The first 24 entries are the cooked retail race roster. Mii identity models
// require the separate profile-rendering path before they can enter a race.
inline constexpr std::array<UiCharacterInfo,26> UiCharacters{{
 {"Mario",0,1},{"Luigi",7,1},{"Peach",16,1},{"Yoshi",10,1},
 {"Baby Mario",6,0},{"Baby Peach",1,0},{"Toad",8,0},{"Koopa Troopa",14,0},
 {"Wario",11,2},{"Waluigi",2,2},{"Donkey Kong",9,2},{"Bowser",3,2},
 {"Baby Daisy",4,0},{"Baby Luigi",12,0},{"Birdo",17,1},{"Daisy",15,1},
 {"Diddy Kong",18,1},{"Dry Bones",5,0},{"Dry Bowser",21,2},{"Funky Kong",22,2},
 {"King Boo",19,2},{"Rosalina",23,2},{"Toadette",13,0},{"Bowser Jr.",20,1},
 {"Mii Outfit A",24,1},{"Mii Outfit B",26,1}
}};
// Full retail roster in screen row order; race storage order is independent.
inline constexpr std::array<unsigned,26> UiCharacterDrivers{{
 4,13,5,12, 6,22,7,17, 0,1,2,15, 3,14,16,23, 8,9,10,11, 20,21,19,18, 24,25
}};
// Choices match GarageChoices (VehiclesPerDriver per driver) and the vehicle sprite keys.
inline constexpr const char* UiVehicleNames[3][12]={
 {"Standard Kart S","Booster Seat","Mini Beast","Standard Bike S","Bullet Bike","Bit Bike","Cheep Charger","Tiny Titan","Blue Falcon","Quacker","Magikruiser","Jet Bubble"},
 {"Standard Kart M","Classic Dragster","Wild Wing","Standard Bike M","Mach Bike","Sugarscoot","Super Blooper","Daytripper","Sprinter","Zip Zip","Sneakster","Dolphin Dasher"},
 {"Standard Kart L","Offroader","Flame Flyer","Standard Bike L","Flame Runner","Wario Bike","Piranha Prowler","Jetsetter","Honeycoupe","Shooting Star","Spear","Phantom"}
};
inline constexpr const char* UiBattleStageNames[]={"Block Plaza","Delfino Pier","Funky Stadium","Chain Chomp Wheel","Thwomp Desert","SNES Battle Course 4","GBA Battle Course 3","N64 Skyscraper","GCN Cookie Land","DS Twilight House"};
// Pause buttons in screen order (PauseMenu*.brctr): only Time Trials offers
// Restart, Change Course and Change Character; the other modes Continue / Quit.
enum class UiPauseButton {Continue,Restart,ChangeCourse,ChangeCharacter,Quit};
struct UiPauseMenu {const UiPauseButton* buttons;unsigned count;};
// Platform-independent navigation. Scene mutation is exclusively handled by main.
class MenuState {
public:
 UiPage page=UiPage::Strap;UiSelection selection{};
 unsigned cursor=0,age=0,focusAge=0;bool unavailable=false;
 // The Cup page shows eight cups: cupPage picks which (more than one page only in the Retro Rewind edition).
 unsigned cupPage=0;
 // The choice is not unlocked for this license (the notice says "Locked").
 bool locked=false;
 // An empty license slot was chosen: main asks for its name, then creates it.
 bool nameRequest=false;
 // A message window over the page (the Drift page's Help): A or B closes it.
 bool messageOpen=false;
 // Controls: the action row waiting for its new button (-1 none); main binds it.
 int binding=-1;
 // Options (from the license page): settings main keeps in quality.txt and save.bin.
 // Controls live in mkw::controls(); settingsChanged asks main to save them.
 // Pages edit these copies only: main warms neighbouring pages by running inputs on
 // copies of the state, so a step must never write globals (progress, controls).
 struct Settings {bool framerate60=true,wiiMap=true,unlockAll=false;} settings{};
 bool settingsChanged=false;
 // Clock noise from main for Random courses: page ages alone repeat after every race.
 unsigned entropy=0;
 // Erase License was confirmed: main erases selection.license and saves.
 bool eraseRequest=false;
 // Records: the cup whose Time Trial bests are shown.
 unsigned recordsCup=0;
 // Controls / GameSettings rows: a label and its value; OK last. Settings step with
 // left/right or A, a Controls row waits for a button (binding).
 const char* settingLabel(unsigned row)const;const char* settingValue(unsigned row)const;
 void stepSetting(unsigned row,int direction);
 // An "Unlocked!" notice over the page (a Progress unlockable, -1 none): A or B closes it.
 int unlockNotice=-1;
 // Results is the retail VS end-of-race menu (VSRaceEnd): Next Race / Quit
 // Game, or only Next after the final race of a VS set (IsFinalRace).
 bool finalRace=false;
 // The chosen course has a saved Time Trial ghost (kept current by main):
 // its racer (UiCharacters index, vehicle choice) and time.
 bool ghostAvailable=false;unsigned ghostDriver=0,ghostVehicle=0;uint32_t ghostMilliseconds=0;
 // The course's ghost cards (own, staff, expert once unlocked), paged left/right
 // on the Ghost page; main fills them and showGhost copies one into the fields above.
 struct GhostCard {unsigned source=0,driver=0,vehicle=0;uint32_t milliseconds=0;};
 std::array<GhostCard,3> ghostCards{};unsigned ghostCount=0,ghostPage=0,ghostSource=0;
 void showGhost(unsigned page){ghostPage=ghostCount?std::min(page,ghostCount-1):0;ghostAvailable=ghostCount>0;
  const auto& g=ghostCards[ghostPage];ghostDriver=g.driver;ghostVehicle=g.vehicle;ghostMilliseconds=g.milliseconds;ghostSource=g.source;}
 UiFocusState focus{};
 UiAction input(UiInput);
 void advance(unsigned ticks);
 void enter(UiPage);
 void raceStarted(){enter(UiPage::Race);}
 void finish(bool last=false){finalRace=last;enter(UiPage::Results);}
 // The race keeps running behind its end-of-race menu.
 bool simulates()const{return page==UiPage::Race||page==UiPage::Results;}
 // Logical Race is selected before the pause exit animation completes.
 // Its assets must survive every frame of that transition back to gameplay.
 bool retainsRaceAssets()const{return simulates()||page==UiPage::Pause||page==UiPage::QuitConfirm;}
 unsigned count()const;unsigned columns()const;
 UiPauseMenu pauseMenu()const;
 unsigned characterDriver(unsigned slot)const;
 unsigned vehicleChoice(unsigned slot)const;
 unsigned vehicleSlot(unsigned choice)const;
 unsigned engineClassMovie(unsigned slot)const;
 // The engine class a class-page (or rules Class row) slot shows: retail 50cc,
 // 100cc, 150cc, Mirror; Retro Rewind 100cc, 150cc, 200cc, Mirror. retailClass:
 // the retail class whose art and messages the slot uses.
 static UiEngineClass slotClass(unsigned slot);
 static bool slotCC200(unsigned slot);
 static unsigned retailClass(unsigned slot);
 unsigned classSlot()const;
 // Rules page: its rows for the chosen mode, and the focused row and option.
 unsigned ruleRows()const;UiRuleRowInfo ruleRow(unsigned row)const;
 unsigned ruleSlot(unsigned row,unsigned option)const;
 void ruleCursor(unsigned& row,unsigned& option)const;
 // The chosen option of a radio row (Class, CPU, Vehicles, Courses, Items).
 unsigned ruleChoice(UiRuleRow row)const;
 // The race runtime starts races with all 24 retail drivers and all twelve
 // machines each. Other choices stay browsable, but accepting one reports
 // "Not available" on its own page rather than at the final confirmation.
 static bool raceMode(UiMode mode){return mode==UiMode::VS||mode==UiMode::GrandPrix||mode==UiMode::TimeTrials||mode==UiMode::Battle;}
 // Mirror races 150cc with the presentation flipped and steering negated.
 static bool raceEngineClass(UiEngineClass c){return c<=UiEngineClass::Mirror;}
 static bool raceDriver(unsigned driver){return driver<24;}
 static bool raceVehicle(unsigned choice){return choice<12;}
 bool supportsRace()const;
};
struct UiQuad {unsigned sprite;float x,y,w,h,alpha=1;uint32_t tint=0xffffffff;bool bottom=false;float u0=0,v0=0,u1=1,v1=1;unsigned movie=~0u;std::array<uint32_t,4> corners{{0xffffffff,0xffffffff,0xffffffff,0xffffffff}};bool additive=false;unsigned vehicleIcon=~0u;uint32_t vehicleColor0=0,vehicleColor1=0xffffffff;};
struct UiRacer {unsigned driver=0,rank=1,lap=1;bool finished=false;float x=0,z=0;};
struct UiRaceView {std::array<UiRacer,12> racers{};unsigned count=0;unsigned milliseconds=0;};
// The same command list is consumed by Citro3D and the host layout preview.
struct UiTransitionView;
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&);
void composeMenu(const MenuState&,const UiRaceView&,std::vector<UiQuad>&,const UiTransitionView&);
std::vector<UiQuad> composeMenu(const MenuState&,const UiRaceView&);
}
