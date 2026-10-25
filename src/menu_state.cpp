#include <mkw/menu_ui.hpp>
#include <mkw/progress.hpp>
#include <mkw/controls.hpp>
#include <mkw/course_catalog.hpp>
#include <mkw/garage_skins.hpp>
#include <algorithm>
#include <cmath>

namespace mkw {
namespace {
constexpr unsigned KartChoices[]={0,1,2,6,7,8};
constexpr unsigned BikeChoices[]={3,4,5,9,10,11};
// Each row pairs a kart and bike, in the original machine family order.
constexpr unsigned AllVehicleChoices[]={0,3,1,4,2,5,6,9,7,10,8,11};
constexpr unsigned CupsPerPage=8,CupPages=(RaceCupCount+CupsPerPage-1)/CupsPerPage;
// Retro Rewind tracks outside the cooked cups are listed but not available.
bool cupReady(unsigned cup){for(unsigned i=cup*4;i<cup*4+4;++i)if(!Courses[i].supported)return false;return true;}

// Bikes in 50cc and karts in 100cc wait for the class's trophies (Progress::mixedTypes).
// Retro Rewind always offers both.
bool mixed(const UiSelection& s){return RetroRewindEdition||(s.mixedEngineTypes&&progress().mixedTypes(unsigned(s.engineClass)));}
bool kartsOnly(const UiSelection& s){return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC50&&!mixed(s);}
bool bikesOnly(const UiSelection& s){return s.mode==UiMode::GrandPrix&&s.engineClass==UiEngineClass::CC100&&!mixed(s);}
// A cup the current license may race: Time Trials take one open in any class.
bool cupOpen(const UiSelection& s,unsigned cup){return s.mode==UiMode::TimeTrials?progress().anyClassCup(cup):progress().cup(unsigned(s.engineClass),cup);}

unsigned moveMainMenu(unsigned cursor,const UiInput& key){
 // License Settings (slot 7) sits above the top row's right end.
 const unsigned settings=unsigned(UiMainMenuControls.size());
 if(cursor==settings)return key.down||key.left?3:cursor;
 if(key.up&&cursor<4)return settings;
 const auto& origin=UiMainMenuControls[cursor];
 if(key.up||key.down){
  // There are two visual rows, with different child counts. Choose the
  // closest child in the other row using original control centers.
  unsigned best=cursor;float distance=1e9f;
  for(unsigned i=0;i<UiMainMenuControls.size();++i){
   const auto& target=UiMainMenuControls[i];
   if(target.y==origin.y)continue;
   const float delta=std::fabs(target.x-origin.x);
   if(delta<distance){distance=delta;best=i;}
  }
  return best;
 }
 const unsigned first=cursor<4?0:4,last=cursor<4?3:6;
 return key.left?(cursor==first?last:cursor-1):(cursor==last?first:cursor+1);
}

void normalize(UiSelection& s){
 if(s.driver>=UiCharacters.size())s.driver=0;
 if(s.vehicle>=12)s.vehicle=0;
 if(s.course>=RaceCourseCount)s.course=0;
 if(s.battleStage>=10)s.battleStage=0;
 if(s.license>=4)s.license=0;
 if(s.cpuLevel>2)s.cpuLevel=1;
 if(s.cpuVehicles>2)s.cpuVehicles=0;
 if(s.courseOrder>2)s.courseOrder=0;
 if(s.itemMode>3||(s.mode==UiMode::Battle&&s.itemMode>2))s.itemMode=0;
 if(std::find(UiRaceCounts.begin(),UiRaceCounts.end(),s.raceCount)==UiRaceCounts.end())s.raceCount=4;
 if(std::find(UiBattleWins.begin(),UiBattleWins.end(),s.battleWins)==UiBattleWins.end())s.battleWins=2;
 if(unsigned(s.mode)>unsigned(UiMode::Battle))s.mode=UiMode::GrandPrix;
 if(unsigned(s.engineClass)>unsigned(UiEngineClass::Mirror))s.engineClass=UiEngineClass::CC150;
}

// VS/Battle with Random or In Order courses skip the cup and course pages.
bool autoCourse(const UiSelection& s){return (s.mode==UiMode::VS||s.mode==UiMode::Battle)&&s.courseOrder!=0;}
// The next course (or stage) of an automatic order; first starts the set.
void nextCourse(UiSelection& s,bool first,unsigned seed){
 const bool battle=s.mode==UiMode::Battle;const unsigned count=battle?10u:RaceCourseCount;
 unsigned& at=battle?s.battleStage:s.course;
 if(s.courseOrder==1){
  // Random (Pages::VSSettings "Randomly choose courses"): any course but the last one.
  unsigned x=seed*2654435761u^0x9e3779b9u;x^=x>>15;x*=2246822519u;x^=x>>13;
  const unsigned pick=x%(first?count:count-1);
  at=first||pick<at?pick:pick+1;
 }else at=first?0:(at+1)%count;
 // Skip courses that are not available (all are, outside the Retro Rewind edition).
 if(!battle)for(unsigned n=0;n<count&&!Courses[at].supported;++n)at=(at+1)%count;
}
unsigned gridColumn(UiPage page,unsigned slot,unsigned columns){
 // The two Mii buttons occupy the right half of a seventh character row.
 return page==UiPage::Character&&slot>=24?slot-22:slot%columns;
}
unsigned moveGrid(UiPage page,unsigned cursor,unsigned count,unsigned columns,const UiInput& key){
 if(count<=1)return 0;
 const bool vertical=key.up||key.down;
 const int direction=(key.up||(!vertical&&key.left))?-1:1;
 const unsigned rows=(count+columns-1)/columns;
 const unsigned column=gridColumn(page,cursor,columns),row=cursor/columns;
 const unsigned limit=vertical?rows:columns;
 // Search along one axis only. This preserves spatial navigation and skips the
 // two empty slots preceding the Mii buttons instead of selecting a phantom.
 for(unsigned step=1;step<=limit;++step){
  const unsigned target=(int(vertical?row:column)+int(limit)+direction*int(step))%limit;
  for(unsigned candidate=0;candidate<count;++candidate){
   const unsigned cr=candidate/columns,cc=gridColumn(page,candidate,columns);
   if(vertical?(cc==column&&cr==target):(cr==row&&cc==target))return candidate;
  }
 }
 return cursor;
}
}

// Retro Rewind drops 50cc for 200cc after 150cc (Pulsar's 200cc: 150cc at 1.5x speed).
UiEngineClass MenuState::slotClass(unsigned slot){
 slot=std::min(slot,3u);
 if(!RetroRewindEdition)return UiEngineClass(slot);
 constexpr UiEngineClass classes[]={UiEngineClass::CC100,UiEngineClass::CC150,UiEngineClass::CC150,UiEngineClass::Mirror};
 return classes[slot];
}
bool MenuState::slotCC200(unsigned slot){return RetroRewindEdition&&slot==2;}
unsigned MenuState::retailClass(unsigned slot){return unsigned(slotClass(slot));}
unsigned MenuState::classSlot()const{
 for(unsigned i=0;i<4;++i)if(slotClass(i)==selection.engineClass&&slotCC200(i)==selection.cc200)return i;
 return RetroRewindEdition?1:unsigned(selection.engineClass);
}
unsigned MenuState::ruleRows()const{return selection.mode==UiMode::Battle?unsigned(UiBattleRules.size()):unsigned(UiVSRules.size());}
UiRuleRowInfo MenuState::ruleRow(unsigned row)const{
 return selection.mode==UiMode::Battle?UiBattleRules[std::min(row,unsigned(UiBattleRules.size()-1))]:UiVSRules[std::min(row,unsigned(UiVSRules.size()-1))];
}
unsigned MenuState::ruleSlot(unsigned row,unsigned option)const{
 unsigned slot=0;for(unsigned r=0;r<row&&r<ruleRows();++r)slot+=ruleRow(r).options;
 return slot+std::min(option,ruleRow(row).options-1);
}
void MenuState::ruleCursor(unsigned& row,unsigned& option)const{
 unsigned slot=cursor;
 for(row=0;row+1<ruleRows()&&slot>=ruleRow(row).options;++row)slot-=ruleRow(row).options;
 option=std::min(slot,ruleRow(row).options-1);
}
unsigned MenuState::ruleChoice(UiRuleRow row)const{
 switch(row){
 case UiRuleRow::Class:return classSlot();
 case UiRuleRow::Cpu:return selection.cpuLevel;
 case UiRuleRow::Vehicles:return selection.cpuVehicles;
 case UiRuleRow::Courses:return selection.courseOrder;
 case UiRuleRow::Items:return selection.itemMode;
 default:return 0;
 }
}

unsigned MenuState::count()const{
 switch(page){
 case UiPage::Rules:return ruleSlot(ruleRows()-1,0)+1;
 case UiPage::License:return Progress::Licenses+1; // the four licenses and Options
 case UiPage::Mode:case UiPage::EngineClass:return 4;
 case UiPage::Options:return 2;
 case UiPage::Controls:return ControlActionCount+1; // the actions, OK
 case UiPage::GameSettings:return 4;
 case UiPage::Results:return finalRace?1:2;
 case UiPage::MainMenu:return unsigned(UiMainMenuControls.size())+1; // the tiles and License Settings
 case UiPage::LicenseSettings:return 3;
 case UiPage::EraseLicense:return 2;
 case UiPage::Character:return unsigned(UiCharacterDrivers.size());
 case UiPage::Vehicle:return kartsOnly(selection)||bikesOnly(selection)?6:12;
 // VS/Battle mode: the two modes and the corner Rules button; Drift: the two types and Help.
 case UiPage::VSMode:case UiPage::BattleMode:case UiPage::Drift:return 3;
 case UiPage::BattleCup:case UiPage::QuitConfirm:return 2;
 case UiPage::Cup:return std::min(CupsPerPage,RaceCupCount-cupPage*CupsPerPage);
 case UiPage::Course:return 4;
 case UiPage::BattleStage:return 5;
 case UiPage::Ghost:return ghostAvailable?3:1; // without a ghost only Solo is offered
 case UiPage::Pause:return pauseMenu().count;
 default:return 1;
 }
}

UiPauseMenu MenuState::pauseMenu()const{
 static constexpr UiPauseButton TimeTrials[]={UiPauseButton::Continue,UiPauseButton::Restart,UiPauseButton::ChangeCourse,UiPauseButton::ChangeCharacter,UiPauseButton::Quit};
 static constexpr UiPauseButton Other[]={UiPauseButton::Continue,UiPauseButton::Quit};
 return selection.mode==UiMode::TimeTrials?UiPauseMenu{TimeTrials,5}:UiPauseMenu{Other,2};
}

const char* MenuState::settingLabel(unsigned row)const{
 if(row+1>=count())return "OK";
 if(page==UiPage::Controls)return ControlActionNames[row];
 constexpr const char* labels[]={"Framerate","Minimap","Unlock Everything"};
 return labels[std::min(row,2u)];
}
const char* MenuState::settingValue(unsigned row)const{
 if(page==UiPage::Controls)return ControlButtonNames[unsigned(controls().active()[row])];
 switch(row){
 case 0:return settings.framerate60?"60 FPS":"30 FPS";
 case 1:return settings.wiiMap?"Wii":"Outline";
 default:return settings.unlockAll?"On":"Off";
 }
}
void MenuState::stepSetting(unsigned row,int direction){
 (void)direction; // every setting has two values
 if(page==UiPage::Controls){binding=int(row);return;} // main binds the next button pressed
 switch(row){
 case 0:settings.framerate60=!settings.framerate60;break;
 case 1:settings.wiiMap=!settings.wiiMap;break;
 case 2:settings.unlockAll=!settings.unlockAll;break;
 default:break;
 }
}

unsigned MenuState::columns()const{
 switch(page){
 case UiPage::License:case UiPage::MainMenu:return 2;
 case UiPage::Character:case UiPage::Cup:return 4;
 case UiPage::Vehicle:return kartsOnly(selection)||bikesOnly(selection)?1:2;
 case UiPage::BattleCup:return 2;
 default:return 1;
 }
}

unsigned MenuState::characterDriver(unsigned slot)const{
 return UiCharacterDrivers[std::min(slot,unsigned(UiCharacterDrivers.size()-1))];
}

unsigned MenuState::vehicleChoice(unsigned slot)const{
 if(kartsOnly(selection))return KartChoices[std::min(slot,5u)];
 if(bikesOnly(selection))return BikeChoices[std::min(slot,5u)];
 return AllVehicleChoices[std::min(slot,11u)];
}

unsigned MenuState::vehicleSlot(unsigned choice)const{
 const unsigned limit=kartsOnly(selection)||bikesOnly(selection)?6:12;
 for(unsigned i=0;i<limit;++i)if(vehicleChoice(i)==choice)return i;
 return 0;
}

unsigned MenuState::engineClassMovie(unsigned slot)const{
 slot=retailClass(slot);
 return (RetroRewindEdition||(selection.mixedEngineTypes&&progress().mixedTypes(slot)))&&slot<2?slot+4:slot;
}

bool MenuState::supportsRace()const{
 // Browsing a retail menu does not silently change the chosen race rules or
 // substitute a different character/machine when that runtime is unavailable.
 const bool courseReady=selection.mode==UiMode::Battle?selection.battleStage<10:selection.course<RaceCourseCount&&Courses[selection.course].supported;
 return raceMode(selection.mode)&&(selection.mode!=UiMode::GrandPrix||raceEngineClass(selection.engineClass))&&raceDriver(selection.driver)&&raceVehicle(selection.vehicle)&&courseReady;
}

UiInput UiDirectionRepeat::step(bool up,bool down,bool left,bool right,unsigned frames){
 // fn_1_DF4E4's mask: up 1 (else down 2), right 4 (else left 8). The count
 // restarts only from or to no direction; fn_1_DF6BC fires at 0 and 15+15k.
 const unsigned mask=(up?1u:down?2u:0u)|(right?4u:left?8u:0u);
 auto repeats=[](unsigned held){return held<15?0u:(held-15)/15+1;};
 const bool press=mask&&!mask_;const unsigned before=held_;
 held_=mask&&mask_?held_+frames:0;mask_=mask;
 UiInput key;
 if(mask&&(press||repeats(held_)>repeats(before))){key.up=mask&1;key.down=mask&2;key.right=mask&4;key.left=mask&8;}
 return key;
}

void MenuState::advance(unsigned ticks){age+=ticks;focusAge+=ticks;sectionAge+=ticks;uiFocusAdvance(*this,ticks);}

void MenuState::enter(UiPage next){
 normalize(selection);
 // Leaving the race (results, pause) is a section change: a new MenuBG.
 if(retainsRaceAssets())sectionAge=0;
 page=next;age=focusAge=0;unavailable=locked=messageOpen=false;binding=-1;cursor=0;
 switch(page){
 case UiPage::License:cursor=selection.license;break;
 case UiPage::Mode:cursor=unsigned(selection.mode);break;
 case UiPage::EngineClass:cursor=classSlot();break;
 case UiPage::VSMode:cursor=selection.teams?1:0;break;
 case UiPage::BattleMode:cursor=selection.coinRunners?1:0;break;
 case UiPage::Rules:cursor=ruleSlot(0,ruleChoice(ruleRow(0).row));break;
 case UiPage::Character:
  for(unsigned i=0;i<UiCharacterDrivers.size();++i)if(characterDriver(i)==selection.driver){cursor=i;break;}
  break;
 case UiPage::Vehicle:cursor=vehicleSlot(selection.vehicle);break;
 case UiPage::Drift:cursor=selection.automatic?0:1;break;
 case UiPage::Cup:cupPage=selection.course/4/CupsPerPage;cursor=selection.course/4%CupsPerPage;break;
 case UiPage::Course:cursor=selection.course%4;break;
 case UiPage::BattleCup:cursor=selection.battleStage/5;break;
 case UiPage::BattleStage:cursor=selection.battleStage%5;break;
 case UiPage::Ghost:cursor=ghostAvailable?std::min(unsigned(selection.ghost),2u):0;break;
 case UiPage::QuitConfirm:case UiPage::EraseLicense:cursor=1;break;
 default:break;
 }
 uiFocusReset(*this);
}

UiAction MenuState::input(UiInput key){
 normalize(selection);
 cursor=std::min(cursor,count()-1);
 if(unlockNotice>=0){if(key.accept||key.back||key.start)unlockNotice=-1;return UiAction::None;}
 if(page==UiPage::Race){if(key.start)enter(UiPage::Pause);return UiAction::None;}
 if(messageOpen){if(key.accept||key.back)messageOpen=false;return UiAction::None;}
 // RaceMenuPage_onInit (PAL 0x80858D60) gives the pause pages only an input
 // handler 2 (START), fn_1_349CE4 0x8085A098: close and resume. No page of
 // the family has a Back handler, so B does nothing on Pause or QuitConfirm.
 if(page==UiPage::Pause&&key.start){enter(UiPage::Race);return UiAction::ResumeRace;}
 if((page==UiPage::Pause||page==UiPage::QuitConfirm)&&key.back)return UiAction::None;
 if(key.back){
  switch(page){
  case UiPage::Intro:case UiPage::License:enter(UiPage::Title);break;
  case UiPage::MainMenu:enter(UiPage::License);break;
  case UiPage::Options:enter(UiPage::License);cursor=Progress::Licenses;break;
  case UiPage::LicenseSettings:enter(UiPage::MainMenu);cursor=unsigned(UiMainMenuControls.size());break;
  case UiPage::Records:enter(UiPage::LicenseSettings);break;
  case UiPage::EraseLicense:enter(UiPage::LicenseSettings);cursor=2;break;
  case UiPage::Controls:case UiPage::GameSettings:{const unsigned at=page==UiPage::Controls?0:1;settingsChanged=true;enter(UiPage::Options);cursor=at;break;}
  case UiPage::Mode:enter(UiPage::MainMenu);break;
  case UiPage::EngineClass:case UiPage::VSMode:case UiPage::BattleMode:enter(UiPage::Mode);break;
  case UiPage::Rules:enter(selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::VSMode);cursor=2;break;
  case UiPage::Character:
   enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Mode);break;
  case UiPage::Vehicle:enter(UiPage::Character);break;
  case UiPage::Drift:enter(UiPage::Vehicle);break;
  case UiPage::Cup:case UiPage::BattleCup:enter(UiPage::Drift);break;
  case UiPage::Course:enter(UiPage::Cup);break;
  case UiPage::BattleStage:enter(UiPage::BattleCup);break;
  case UiPage::Ghost:enter(UiPage::Course);break;
  case UiPage::Confirm:case UiPage::Error:
   if(autoCourse(selection)){enter(UiPage::Drift);break;}
   enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Course);break;
  case UiPage::QuitConfirm:enter(UiPage::Pause);cursor=pauseMenu().count-1;break;
  default:break;
  }
  return UiAction::None;
 }
 if(key.switchType&&page==UiPage::EngineClass){
  // GPClassSelect::OnStartPress (USA 80825168) uses one toggle for both
  // unlocked classes. The current highlighted class does not limit it.
  if(!progress().mixedTypes(0)&&!progress().mixedTypes(1)){unavailable=locked=true;return UiAction::None;}
  selection.mixedEngineTypes=!selection.mixedEngineTypes;
  focusAge=0;unavailable=locked=false;
  return UiAction::None;
 }
 if(RetroRewindEdition&&(key.pageLeft||key.pageRight)&&page==UiPage::Character){
  // The focused character's skins, then back to the default.
  const unsigned driver=characterDriver(cursor),skins=driver<GarageSkinCounts.size()?GarageSkinCounts[driver]:0;
  // Skins that did not cook (GarageSkinMask) are skipped.
  for(unsigned n=0;skins&&n<=skins;++n){
   selection.skin=(selection.skin+(key.pageLeft?skins:1))%(skins+1);
   if(!selection.skin||(GarageSkinMask[driver]>>selection.skin&1))break;
  }
  focusAge=0;unavailable=false;
  return UiAction::None;
 }
 if(RetroRewindEdition&&page==UiPage::Cup&&(key.up||key.down||key.left||key.right)){
  // Pulsar's cup grid: slot = column*2 + row. Past either end, the next page.
  const unsigned previous=cursor,row=cursor%2,col=cursor/2;unsigned n=count();
  if(key.up||key.down){if((cursor^1u)<n)cursor^=1u;}
  else if(key.right&&col+1<(n+1)/2)cursor=std::min(cursor+2,n-1);
  else if(key.left&&col>0)cursor-=2;
  else if(CupPages>1){
   cupPage=(cupPage+(key.left?CupPages-1:1))%CupPages;n=count();
   cursor=std::min(key.left?((n+1)/2-1)*2+row:row,n-1);
  }
  if(cursor!=previous)focusAge=0;
  uiFocusSelect(*this,previous);unavailable=false;
  return UiAction::None;
 }
 if(page==UiPage::Cup&&(key.pageLeft||key.pageRight)&&CupPages>1){
  cupPage=(cupPage+(key.pageLeft?CupPages-1:1))%CupPages;cursor=std::min(cursor,count()-1);
  focusAge=0;unavailable=false;uiFocusReset(*this);
  return UiAction::None;
 }
 if(page==UiPage::Ghost&&ghostCount>1&&(key.left||key.right||key.pageLeft||key.pageRight)){
  showGhost((ghostPage+(key.left||key.pageLeft?ghostCount-1:1))%ghostCount);focusAge=0;return UiAction::None;
 }
 if(page==UiPage::Records&&(key.left||key.right||key.pageLeft||key.pageRight)){
  recordsCup=(recordsCup+(key.left||key.pageLeft?RaceCupCount-1:1))%RaceCupCount;focusAge=0;return UiAction::None;
 }
 if(page==UiPage::GameSettings&&(key.left||key.right)){
  if(cursor+1<count()){stepSetting(cursor,key.left?-1:1);focusAge=0;}
  return UiAction::None;
 }
 if(page==UiPage::Rules&&(key.up||key.down||key.left||key.right)){
  // Rows move up and down, keeping the column; left and right move within a
  // row, and step the race (or win) count on its UpDown row.
  unsigned row,option;ruleCursor(row,option);const unsigned rows=ruleRows();
  const auto info=ruleRow(row);
  if(info.row==UiRuleRow::Count&&(key.left||key.right)){
   const bool battle=selection.mode==UiMode::Battle;
   unsigned& value=battle?selection.battleWins:selection.raceCount;
   const unsigned* first=battle?UiBattleWins.data():UiRaceCounts.data();
   const unsigned n=battle?unsigned(UiBattleWins.size()):unsigned(UiRaceCounts.size());
   unsigned at=0;while(at<n&&first[at]!=value)++at;
   at=key.left?(at==0?n-1:at-1):(at+1>=n?0:at+1);
   value=first[at];focusAge=0;
  }else if(key.left||key.right){
   if(info.options>1){option=key.left?(option==0?info.options-1:option-1):(option+1>=info.options?0:option+1);cursor=ruleSlot(row,option);focusAge=0;}
  }else{
   row=key.up?(row==0?rows-1:row-1):(row+1>=rows?0:row+1);
   // Entering a radio row lands on its chosen option.
   const auto next=ruleRow(row);
   cursor=ruleSlot(row,next.options>1?(next.row==info.row?option:ruleChoice(next.row)):0);focusAge=0;
  }
  unavailable=false;
  return UiAction::None;
 }
 if(key.up||key.down||key.left||key.right){
  const unsigned previousCursor=cursor;
  if(page==UiPage::MainMenu)cursor=moveMainMenu(cursor,key);
  else if(columns()==1){
   // Horizontal input also traverses a one-column control group, as it does
   // when navigating the same controls with the Wii controller sideways.
   const int step=key.up||(!key.down&&key.left)?-1:1;
   cursor=(int(cursor)+int(count())+step)%count();
  }else cursor=moveGrid(page,cursor,count(),columns(),key);
  if(cursor!=previousCursor){focusAge=0;if(page==UiPage::Character)selection.skin=0;}
  uiFocusSelect(*this,previousCursor);unavailable=false;
 }
 if(!key.accept&&!(key.start&&(page==UiPage::Title||page==UiPage::Strap||page==UiPage::Intro)))return UiAction::None;
 locked=false;
 switch(page){
 case UiPage::Strap:enter(UiPage::Intro);break;
 case UiPage::Intro:enter(UiPage::Title);break;
 case UiPage::Title:enter(UiPage::License);break;
 case UiPage::License:
  // An empty slot asks for a name first (main shows the keyboard, then creates it).
  if(cursor==Progress::Licenses){enter(UiPage::Options);break;}
  if(!progress().license[std::min(cursor,Progress::Licenses-1)].used){nameRequest=true;break;}
  selection.license=cursor;enter(UiPage::MainMenu);break;
 case UiPage::MainMenu:if(cursor==0)enter(UiPage::Mode);else if(cursor==UiMainMenuControls.size())enter(UiPage::LicenseSettings);else unavailable=true;break;
 // Records, Change Mii (needs Mii support) and Erase License.
 case UiPage::LicenseSettings:if(cursor==0){recordsCup=0;enter(UiPage::Records);}else if(cursor==1)unavailable=true;else enter(UiPage::EraseLicense);break;
 case UiPage::Records:enter(UiPage::LicenseSettings);break;
 case UiPage::EraseLicense:if(cursor==0){eraseRequest=true;enter(UiPage::License);}else{enter(UiPage::LicenseSettings);cursor=2;}break;
 case UiPage::Options:enter(cursor==0?UiPage::Controls:UiPage::GameSettings);break;
 case UiPage::Controls:case UiPage::GameSettings:
  if(cursor+1<count()){stepSetting(cursor,1);break;}
  {const unsigned at=page==UiPage::Controls?0:1;settingsChanged=true;enter(UiPage::Options);cursor=at;}break;
 case UiPage::Mode:
  if(!raceMode(UiMode(cursor))){unavailable=true;break;}
  selection.mode=UiMode(cursor);
  enter(selection.mode==UiMode::GrandPrix?UiPage::EngineClass:selection.mode==UiMode::VS?UiPage::VSMode:selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::Character);break;
 case UiPage::EngineClass:
  if(!raceEngineClass(slotClass(cursor))){unavailable=true;break;}
  if(slotClass(cursor)==UiEngineClass::Mirror&&!progress().mirror()){unavailable=locked=true;break;}
  selection.engineClass=slotClass(cursor);selection.cc200=slotCC200(cursor);enter(UiPage::Character);break;
 // The rules are optional, behind the corner Rules button (RuleSetting), as in retail.
 case UiPage::VSMode:
  if(cursor==2){enter(UiPage::Rules);break;}
  selection.teams=cursor==1;enter(UiPage::Character);break;
 case UiPage::BattleMode:
  if(cursor==2){enter(UiPage::Rules);break;}
  selection.coinRunners=cursor==1;enter(UiPage::Character);break;
 case UiPage::Rules:{
  // A radio option becomes the row's choice; OK goes on to the characters.
  unsigned row,option;ruleCursor(row,option);
  switch(ruleRow(row).row){
  case UiRuleRow::Class:
   if(slotClass(option)==UiEngineClass::Mirror&&!progress().mirror()){unavailable=locked=true;break;}
   selection.engineClass=slotClass(option);selection.cc200=slotCC200(option);break;
  case UiRuleRow::Cpu:selection.cpuLevel=option;break;
  case UiRuleRow::Vehicles:selection.cpuVehicles=option;break;
  case UiRuleRow::Courses:selection.courseOrder=option;break;
  case UiRuleRow::Items:selection.itemMode=option;break;
  case UiRuleRow::Count:break;
  case UiRuleRow::Ok:enter(selection.mode==UiMode::Battle?UiPage::BattleMode:UiPage::VSMode);cursor=2;break;
  }
  break;
 }
 case UiPage::Character:
  // Includes the two Mii outfits, which also need Mii data.
  if(!raceDriver(characterDriver(cursor))){unavailable=true;break;}
  if(!progress().character(characterDriver(cursor))){unavailable=locked=true;break;}
  selection.driver=characterDriver(cursor);enter(UiPage::Vehicle);break;
 case UiPage::Vehicle:
  if(!raceVehicle(vehicleChoice(cursor))){unavailable=true;break;}
  if(!progress().vehicle(UiCharacters[std::min(selection.driver,unsigned(UiCharacters.size()-1))].weight,vehicleChoice(cursor))){unavailable=locked=true;break;}
  selection.vehicle=vehicleChoice(cursor);enter(UiPage::Drift);break;
 case UiPage::Drift:
  if(cursor==2){messageOpen=true;break;} // Help: the drift explanation window
  selection.automatic=cursor==0;
  if(autoCourse(selection)){nextCourse(selection,true,age+focusAge*7919u+entropy);enter(UiPage::Confirm);break;}
  enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);break;
 case UiPage::Cup:{
  // A Grand Prix selects the whole cup. VS and Time Trials select a course.
  const unsigned cup=cupPage*CupsPerPage+cursor;
  if(!cupOpen(selection,cup)){unavailable=locked=true;break;}
  if(selection.mode==UiMode::GrandPrix&&!cupReady(cup)){unavailable=true;break;}
  if(selection.course/4!=cup||selection.mode==UiMode::GrandPrix)selection.course=cup*4;
  enter(selection.mode==UiMode::GrandPrix?UiPage::Confirm:UiPage::Course);break;
 }
 case UiPage::Course:
  if(!Courses[selection.course/4*4+cursor].supported){unavailable=true;break;}
  selection.course=selection.course/4*4+cursor;enter(selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Confirm);break;
 case UiPage::Ghost:
  selection.ghost=ghostAvailable?cursor:2;selection.ghostSource=ghostSource;enter(UiPage::Confirm);break;
 case UiPage::BattleCup:
  if(selection.battleStage/5!=cursor)selection.battleStage=cursor*5;
  enter(UiPage::BattleStage);break;
 case UiPage::BattleStage:selection.battleStage=selection.battleStage/5*5+cursor;enter(UiPage::Confirm);break;
 case UiPage::Confirm:if(supportsRace())return UiAction::StartRace;unavailable=true;break;
 // RaceMenuPage_onButtonFront (PAL 0x8085A0F4), by button id.
 case UiPage::Pause:
  switch(pauseMenu().buttons[cursor]){
  case UiPauseButton::Continue:enter(UiPage::Race);return UiAction::ResumeRace;
  case UiPauseButton::Restart:return UiAction::RestartRace;
  case UiPauseButton::ChangeCourse:enter(UiPage::Cup);break;
  case UiPauseButton::ChangeCharacter:enter(UiPage::Character);break;
  // Case 1: with the section's confirmation page (0x2C, only Grand Prix
  // section 0x1E among the single-player races) it pushes that page,
  // otherwise it quits at once (section 0x41).
  case UiPauseButton::Quit:enter(selection.mode==UiMode::GrandPrix?UiPage::QuitConfirm:UiPage::MainMenu);break;
  }
  break;
 // Page 0x2C: ButtonConfirmQuit quits as case 1 does; ButtonConfirmContinue
 // (case 0x17) closes the pages and resumes while paused.
 case UiPage::QuitConfirm:
  if(cursor==0)enter(UiPage::MainMenu);else{enter(UiPage::Race);return UiAction::ResumeRace;}break;
 case UiPage::Results:
  // RaceMenu::OnButtonClick (USA 0x80838664): Next goes to the VS course
  // choice (section 0x4B); Quit and the final race's Next return to the
  // menus (section 0x41). A Grand Prix's award ceremony runs in between (main.cpp).
  // A Grand Prix goes straight on to the cup's next course.
  if(cursor==0&&!finalRace&&selection.mode==UiMode::GrandPrix){selection.course=selection.course/4*4+std::min(selection.course%4+1,3u);enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace&&autoCourse(selection)){nextCourse(selection,false,age+focusAge*7919u+entropy);enter(UiPage::Confirm);return UiAction::StartRace;}
  if(cursor==0&&!finalRace)enter(selection.mode==UiMode::Battle?UiPage::BattleCup:UiPage::Cup);
  else enter(UiPage::MainMenu);
  break;
 case UiPage::Error:if(autoCourse(selection)){enter(UiPage::Drift);break;}enter(selection.mode==UiMode::GrandPrix?UiPage::Cup:selection.mode==UiMode::Battle?UiPage::BattleStage:selection.mode==UiMode::TimeTrials?UiPage::Ghost:UiPage::Course);break;
 default:break;
 }
 return UiAction::None;
}
}
