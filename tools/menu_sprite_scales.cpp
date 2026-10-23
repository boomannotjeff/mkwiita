// Host tool for tools/optimize_menu_textures.py.
// Composes every menu page through the production composer (pages, modes,
// cursors, selections, ages, transitions and OK-glow pulses) and reports, per
// sprite, the largest on-screen size of its full UV extent in top-LCD pixels.
// Movie and vehicle-model quads are not textures and are skipped.
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/controls.hpp>
#include <mkw/progress.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace mkw;
static std::vector<float> maxW(std::size(UiSprites),0.f),maxH(std::size(UiSprites),0.f);
static void record(const std::vector<UiQuad>& quads){
 for(const auto& q:quads){
  if(q.movie!=~0u||q.vehicleIcon!=~0u||q.sprite>=std::size(UiSprites))continue;
  const float du=std::fabs(q.u1-q.u0),dv=std::fabs(q.v1-q.v0);
  if(du>1e-4f)maxW[q.sprite]=std::max(maxW[q.sprite],q.w/du);
  if(dv>1e-4f)maxH[q.sprite]=std::max(maxH[q.sprite],q.h/dv);
 }
}
int main(int argc,char** argv){
 if(argc<2){std::fprintf(stderr,"usage: %s output.json\n",argv[0]);return 2;}
 const UiPage pages[]={UiPage::Strap,UiPage::Intro,UiPage::Title,UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::VSMode,UiPage::BattleMode,UiPage::Rules,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::BattleCup,UiPage::BattleStage,UiPage::Ghost,UiPage::Confirm,UiPage::Pause,UiPage::QuitConfirm,UiPage::Results,UiPage::Error,UiPage::Options,UiPage::Controls,UiPage::GameSettings,UiPage::LicenseSettings,UiPage::Records,UiPage::EraseLicense};
 UiRaceView race{};race.count=12;for(unsigned i=0;i<12;++i){race.racers[i].driver=i;race.racers[i].rank=i+1;}
 std::vector<UiQuad> q;
 for(UiPage page:pages)for(unsigned mode=0;mode<4;++mode)for(unsigned variant=0;variant<24;++variant)for(int mixed=0;mixed<2;++mixed){
  if(mixed&&page!=UiPage::EngineClass&&page!=UiPage::Vehicle)continue;
  MenuState s;s.selection.mode=UiMode(mode);s.selection.driver=variant;s.selection.vehicle=variant%12;
  s.selection.course=(variant*4+mode)%32;s.selection.battleStage=variant%10;s.selection.mixedEngineTypes=mixed;
  s.selection.engineClass=UiEngineClass(variant%4);
  // Every rules option chosen (Choice on) and every race/win count.
  s.selection.cpuLevel=variant%3;s.selection.cpuVehicles=(variant/3)%3;s.selection.courseOrder=(variant/9)%3;
  s.selection.itemMode=variant%(mode==unsigned(UiMode::Battle)?3:4);
  s.selection.raceCount=UiRaceCounts[variant%UiRaceCounts.size()];s.selection.battleWins=UiBattleWins[variant%UiBattleWins.size()];
  // Options: both control presets and every settings value.
  controls().custom=variant%2;s.settings={variant%2==0,variant/2%2==0};progress().unlockAll=variant/4%2;s.recordsCup=variant%RaceCupCount;
  // License cards: every trophy colour in every cell, the cups open or not.
  for(unsigned l=0;l<2;++l){auto& lic=progress().license[l];lic.used=true;lic.name[0]='A';lic.name[1]=0;
   for(unsigned k=0;k<4;++k)for(unsigned c=0;c<8;++c)lic.trophy[k][c]=uint8_t((variant+k+c+l)%4*(variant%3!=0));}
  // Ghost page: no ghost, or 1-3 ghost cards (own, staff, expert).
  s.ghostCount=variant%4;for(unsigned g=0;g<s.ghostCount;++g)s.ghostCards[g]={g,variant%24,variant%12,90000};s.showGhost(variant/4%3);
  s.enter(page);
  for(unsigned cursor=0;cursor<s.count();++cursor){
   MenuState t=s;t.cursor=cursor;
   for(unsigned age:{0u,31u,61u,121u,181u,241u,361u,481u}){
    MenuState u=t;u.advance(age);
    composeMenu(u,race,q);record(q);
    u.unavailable=true;composeMenu(u,race,q);record(q);u.unavailable=false;
    u.messageOpen=true;composeMenu(u,race,q);record(q);u.messageOpen=false;
   }
   if(variant>1&&page!=UiPage::Character&&page!=UiPage::Vehicle)continue;
   for(int back=0;back<2;++back){
    MenuState logical=t;logical.advance(30);MenuTransition tr;UiInput key;key.accept=!back;key.back=back;
    tr.input(logical,key);
    for(unsigned frame=0;frame<120&&tr.active();++frame){composeMenu(tr.visual(logical),race,q,tr.view());record(q);tr.advance(logical,1);}
   }
  }
 }
 FILE* f=std::fopen(argv[1],"w");if(!f)return 1;
 std::fprintf(f,"{\n");bool first=true;
 for(unsigned i=0;i<std::size(UiSprites);++i){
  if(maxW[i]<=0&&maxH[i]<=0)continue;
  std::fprintf(f,"%s \"%s\": [%.4f, %.4f]",first?"":",\n",UiSprites[i].key,maxW[i],maxH[i]);first=false;
 }
 std::fprintf(f,"\n}\n");std::fclose(f);
 return 0;
}
