#include <mkw/display_layout.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/menu_movie_assets.hpp>
#include <mkw/menu_animations.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_index.hpp>
#include <mkw/menu_text_motion.hpp>
#include <mkw/progress.hpp>
#include <mkw/garage_skins.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
constexpr float XScale=display::MenuScaleX,YScale=display::MenuScaleY;
// Choices the license has not unlocked yet are drawn dimmed.
constexpr uint32_t LockedTint=0x505050ffu;
constexpr const char* CupNames[]={"Mushroom Cup","Flower Cup","Star Cup","Special Cup","Shell Cup","Banana Cup","Leaf Cup","Lightning Cup"};
// Cups and courses by menu index (Courses: the catalog, retail or Retro Rewind).
const char* courseName(unsigned course){return Courses[std::min(course,RaceCourseCount-1)].name;}
constexpr bool RetroRewind=RetroRewindEdition;
#ifdef MKW_RR
const char* cupName(unsigned cup){return Courses[std::min(cup,RaceCupCount-1)*4].cup;}
#else
const char* cupName(unsigned cup){return CupNames[std::min(cup,7u)];}
#endif
// The battle cups' OK glow icons follow the race cups' (tools/cook_menu_ui.py).
constexpr unsigned BattleGlow=RaceCupCount;
struct Pose {
 float x=0,y=0,sx=1,sy=1,delay=0,parentScaleX=1;
 bool moving=false;UiMotionKind kind=UiMotionKind::Slide;
 const char* source=nullptr;
 float alpha=1;
 unsigned focusSlot=~0u;
};
float sourceAlpha(float base,float animation){
 const unsigned a=unsigned(std::clamp(std::lround(base*255.f),0l,255l));
 const unsigned b=unsigned(std::clamp(std::lround(animation*255.f),0l,255l));
 return float(((a+1)*(b+1)-1)>>8)/255.f;
}
// Short derived resource names (base + suffix) without heap allocation.
struct Name {
 char text[64];
 Name(const char* a,const char* b=""){std::snprintf(text,sizeof text,"%s%s",a,b);}
 operator const char*()const{return text;}
 const char* c_str()const{return text;}
};
Pose control(const char* name,const char* variant,unsigned focusSlot=~0u){
 Pose value;value.source=name;value.focusSlot=focusSlot;
 if(const auto* p=uiFindControlPose(name,variant)){value.x=p->x;value.y=p->y;value.sx=p->sx;value.sy=p->sy;value.alpha=p->alpha;}
 if(const auto* motion=uiFindControlMotion(name,variant)){value.delay=motion->delay;value.moving=motion->enabled;}
 if(!std::strcmp(name,"Back"))value.kind=UiMotionKind::Back;
 if(!std::strcmp(name,"MenuObiTitleText"))value.kind=UiMotionKind::PageTitle;
 if(!std::strcmp(name,"MenuObiInstructionText"))value.kind=UiMotionKind::Instruction;
 if(!std::strcmp(name,"MenuObiTop")||!std::strcmp(name,"MenuObiTopRaceConfirm"))value.kind=UiMotionKind::ObiTop;
 if(!std::strcmp(name,"MenuObiBottomRaceConfirm"))value.kind=UiMotionKind::ObiBottom;
 if(!std::strcmp(name,"RaceConfirmBack")||!std::strcmp(name,"BlackBack"))value.kind=UiMotionKind::BlackBack;
 return value;
}
Pose child(Pose parent,Pose p){
 p.x=parent.x+p.x*parent.sx;p.y=parent.y+p.y*parent.sy;
 p.sx*=parent.sx;p.sy*=parent.sy;p.parentScaleX=parent.sx;
 p.delay=std::max(0.f,parent.delay+p.delay);p.alpha=sourceAlpha(parent.alpha,p.alpha);return p;
}
Pose motionOnly(const char* name,const char* variant){Pose p=control(name,variant);p.x=p.y=0;p.sx=p.sy=1;return p;}
Pose numbered(const char* name,unsigned i,const char* prefix="Button"){
 char key[64];std::snprintf(key,sizeof key,"%s%u",prefix,i);return control(name,key,i);
}
// A cup button. Retro Rewind places them as Pulsar's PULSelectCup: a 4x2 grid
// in column order (cup 0 top left, cup 1 below it) between two page arrows.
Pose cupSlot(const char* name,unsigned i){
 Pose p=numbered(name,i);
 char key[16];std::snprintf(key,sizeof key,"Button%u",i);
 if(RetroRewind)if(const auto* q=uiFindControlPose("PULSelectCup",key)){p.x=q->x;p.y=q->y;p.sx=q->sx;p.sy=q->sy;}
 return p;
}
struct Lookup {
 std::array<unsigned,std::size(UiSprites)> keys{},texts{};
 Lookup(){for(unsigned i=0;i<keys.size();++i)keys[i]=texts[i]=i;
  auto order=[](auto& a,bool text){std::sort(a.begin(),a.end(),[text](unsigned a,unsigned b){int c=std::strcmp(text?UiSprites[a].text:UiSprites[a].key,text?UiSprites[b].text:UiSprites[b].key);return c<0||(c==0&&a<b);});};order(keys,false);order(texts,true);
 }
 unsigned find(const char* key,bool text=false,unsigned font=0,unsigned style=0)const{
  const auto& a=text?texts:keys;
  auto it=std::lower_bound(a.begin(),a.end(),key,[text](unsigned i,const char* k){return std::strcmp(text?UiSprites[i].text:UiSprites[i].key,k)<0;});
  if(it==a.end()||std::strcmp(text?UiSprites[*it].text:UiSprites[*it].key,key))return unsigned(std::size(UiSprites));
  unsigned fallback=*it,styled=unsigned(std::size(UiSprites));
  if(text)for(;it!=a.end()&&!std::strcmp(UiSprites[*it].text,key);++it){
   const auto& r=UiSprites[*it];if(r.style==style){if(r.font==font)return *it;if(r.font==0)styled=*it;}
  }
  if(styled<std::size(UiSprites))return styled;
  return fallback;
 }
};
const Lookup& lookup(){static const Lookup value;return value;}
class Composer {
public:
 const MenuState& state;std::vector<UiQuad>& out;const UiTransitionView* transition;
 Composer(const MenuState& s,std::vector<UiQuad>& q,const UiTransitionView* t):state(s),out(q),transition(t){}
 UiTextMotion focusMotion(const UiPaneInfo& pane,Pose p)const{
  if(transition&&transition->preload)return {};
  if(!p.source||p.focusSlot>=UiFocusControlCount||std::strstr(pane.panel,"_ok"))return {};
  const auto focus=uiFocusAnimation(state,p.focusSlot);
  if(std::strcmp(focus.control,p.source)||!focus.clip)return {};
  const auto* reference=uiFindAnimationClip(p.source,"Select",std::strstr(pane.panel,"_free")?"Free":"Select");
  return uiTextMotion(pane,{{focus.clip,focus.frame,0,reference}});
 }
 void applyMotion(float& x,float& y,float& w,float& h,float& alpha,Pose p,const UiTextMotion& m,float baseAlpha=1)const{
  if(!m.visible){alpha=0;return;}
  if(p.sx!=0&&p.sy!=0){
   const float localX=(x-p.x)/p.sx,localY=-(y+p.y)/p.sy;
   x=p.x+(localX*m.sx+m.tx)*p.sx;y=-p.y-(localY*m.sy+m.ty)*p.sy;
   w*=m.sx;h*=m.sy;
  }
  alpha=m.absoluteAlpha?baseAlpha*m.alpha:alpha*m.alpha;
 }
 UiTextMotion panelFocus(const char* key,Pose p)const{
  Name family(key);char* suffix=std::strstr(family.text,"_underlay");if(!suffix)suffix=std::strstr(family.text,"_overlay");if(suffix)*suffix=0;
  const auto* root=pane(family,"pause_w_null");if(!root)root=pane(family,"pause_w_fade_n");
  if(!root)root=pane(family,"null_all");
  return root?focusMotion(*root,p):UiTextMotion{};
 }
 void quad(unsigned i,float x,float y,float w,float h,float alpha=1,uint32_t tint=0xffffffff,Pose p={}){
  if(i>=std::size(UiSprites)||w<=0||h<=0)return;
  float controlAlpha=p.alpha;
  if(transition&&transition->preload)controlAlpha=1;
  if(transition&&transition->active&&p.moving&&(!transition->section||transition->entering)){
   const auto& t=*transition;
   const float progress=uiPageTransitionProgress(t.elapsed,p.delay,t.maxDelay,t.entering,t.back,t.exitHold);
   const auto motion=uiControlTransition(p.kind,progress,state.page==UiPage::MainMenu,p.parentScaleX);
   x+=motion.x;y-=motion.y;controlAlpha=sourceAlpha(controlAlpha,motion.alpha);
  }
  alpha=sourceAlpha(alpha,controlAlpha);
  if(alpha<=0)return;
  x=display::CenterX+x*XScale;y=120+y*YScale;w*=XScale;h*=YScale;
  // Source layouts deliberately bleed beyond the projection; crop their UVs,
  // never squeeze a complete pane into the remaining visible screen space.
  // The 3DS shows 16:9 in 225 of its 240 lines (7.5 above and below); the Vita's
  // canvas is 16:9 already, so its menus use the full height.
#ifdef __vita__
  constexpr float Top=0,Bottom=display::Height;
#else
  constexpr float Top=7.5f,Bottom=232.5f;
#endif
  float l=std::max(x,0.f),t=std::max(y,Top),r=std::min(x+w,display::Width),b=std::min(y+h,Bottom);
  if(r<=l||b<=t)return;
  out.push_back({i,l,t,r-l,b-t,alpha,tint,false,(l-x)/w,(t-y)/h,(r-x)/w,(b-y)/h});
 }
 void sprite(const char* key,float x,float y,float w,float h,float a=1,uint32_t tint=0xffffffff,Pose p={}){quad(uiFindSpriteKey(key),x,y,w,h,a,tint,p);}
 void region(const char* key,float x,float y,float w,float h,float u0,float v0,float u1,float v1,Pose p,float alpha,const char* focusKey=nullptr){
  x=p.x+x*p.sx;y=-p.y+y*p.sy;w*=p.sx;h*=p.sy;
  applyMotion(x,y,w,h,alpha,p,panelFocus(focusKey?focusKey:key,p));
  const auto first=out.size();sprite(key,x,y,w,h,alpha,0xffffffff,p);
  if(out.size()>first){auto& q=out.back();q.u0=u0+(u1-u0)*q.u0;q.u1=u0+(u1-u0)*q.u1;q.v0=v0+(v1-v0)*q.v0;q.v1=v0+(v1-v0)*q.v1;}
 }
 void panel(const char* key,Pose p={},float alpha=1){
  const auto* found=uiFindPanel(key);if(!found)return;const auto& r=*found;
  if(const auto* border=uiFindBorderLoop(key)){const auto& loop=*border;
   region(key,loop.x,loop.y,loop.w,loop.h,(loop.x-r.x)/r.w,(loop.y-r.y)/r.h,(loop.x+loop.w-r.x)/r.w,(loop.y+loop.h-r.y)/r.h,p,alpha);
   const auto* clip=uiFindAnimationClip(loop.control,"Loop","Loop");
   const float sourceFrame=clip&&clip->duration>0?std::fmod(float(state.age)*clip->rate,clip->duration):float(state.age);
   const unsigned frame=unsigned(sourceFrame)%loop.frames;
   for(unsigned at=loop.firstSlice;at<loop.firstSlice+loop.sliceCount;++at){const auto& slice=UiBorderSlices[at];
    // Residency is keyed by page/selection, not loop frame. Every atlas a
    // future frame can use must be requested before the page is revealed.
    const bool resident=transition&&transition->preload&&(at==loop.firstSlice||std::strcmp(slice.key,UiBorderSlices[at-1].key));
    if(slice.frame==frame||resident)region(slice.key,slice.x,slice.y,slice.w,slice.h,slice.u0,slice.v0,slice.u1,slice.v1,p,alpha,key);
   }
   return;
  }
  float x=p.x+r.x*p.sx,y=-p.y+r.y*p.sy,w=r.w*p.sx,h=r.h*p.sy;
  // These flattened button panels share an animated parent. Per-pane
  // submissions (movies, icons, text) sample their full ancestry separately.
  applyMotion(x,y,w,h,alpha,p,panelFocus(key,p));
  sprite(key,x,y,w,h,alpha,0xffffffff,p);
 }
 void graph(float fraction,Pose p){
  fraction=std::clamp(fraction,0.f,1.f);if(fraction<=0)return;
  if(const auto* r=uiFindPanel("vehicle_ability_graph"))sprite(r->key,p.x+r->x*p.sx,-p.y+r->y*p.sy,r->w*p.sx*fraction,r->h*p.sy,1,0xffffffff,p);
 }
 const UiPaneInfo* pane(const char* base,const char* name)const{return uiFindPane(base,name);}
 Name buttonState(const char* base,unsigned slot)const{return Name(base,state.cursor==slot?"_select":"_free");}
 void movieOverlay(const char* base,Pose p){panel(Name(base,"_overlay"),p);}
 void movieButton(const char* base,Pose p){panel(Name(base,"_underlay"),p);}
 // hidden: a pane the cook baked out of the panel image, filled at runtime.
 void inPane(const char* key,const char* base,const char* name,Pose p={},float a=1,bool hidden=false){
  if(auto* r=pane(base,name);r&&(r->visible||hidden)){
   const auto before=out.size();
   float x=p.x+r->x*p.sx,y=-p.y+r->y*p.sy,w=r->w*p.sx,h=r->h*p.sy,alpha=a*r->alpha;
   applyMotion(x,y,w,h,alpha,p,focusMotion(*r,p),a);
   sprite(key,x,y,w,h,alpha,0xffffffff,p);
   if(out.size()==before)return;
   auto& q=out.back();
   auto colorAt=[&](float u,float v){uint32_t color=0;
    for(unsigned shift:{24u,16u,8u,0u}){
     const float a=float((r->vertexColors[0]>>shift)&255),b=float((r->vertexColors[1]>>shift)&255),c=float((r->vertexColors[2]>>shift)&255),d=float((r->vertexColors[3]>>shift)&255);
     // Preserve the source gradient even when the transition clips the pane.
     const float value=u>=v?a+u*(b-a)+v*(d-b):a+u*(d-c)+v*(c-a);
     color|=uint32_t(std::clamp(std::lround(value),0l,255l))<<shift;
    }return color;
   };
   q.corners={colorAt(q.u0,q.v0),colorAt(q.u1,q.v0),colorAt(q.u0,q.v1),colorAt(q.u1,q.v1)};
  }
 }
 void label(const char* text,float cx,float y,float width,float height,uint32_t tint=0xffffffff,int align=0,unsigned font=0){
  unsigned i=lookup().find(text,true,font);if(i>=std::size(UiSprites))return;
  const auto& r=UiSprites[i];float h=height,w=r.w/r.h*h;
  if(w>width){h*=width/w;w=width;}
  float x=align<0?cx:align>0?cx-w:cx-w/2;
  quad(i,x,y,w,h,1,tint);
 }
 void textSprite(unsigned index,const UiPaneInfo& pane,Pose p,uint32_t color=0xffffffff){
  if(index>=std::size(UiSprites)||!pane.visible)return;
  const auto& glyphs=UiSprites[index];const auto& cell=UiFontCells[glyphs.font];
  float w=glyphs.w*(pane.fontW>0?pane.fontW/cell[0]:pane.h/cell[1])*p.sx;
  float h=glyphs.h*(pane.fontH>0?pane.fontH/cell[1]:pane.h/cell[1])*p.sy;
  const float available=pane.w*p.sx;w=std::min(w,available);h=std::min(h,pane.h*p.sy);
  float x=p.x+pane.x*p.sx+(available-w)*(pane.textAlign==0?0:pane.textAlign==2?1:.5f);
  float y=-p.y+pane.y*p.sy+(pane.h*p.sy-h)*(pane.textPosition/3==0?0:pane.textPosition/3==2?1:.5f);
  float alpha=pane.alpha;
  // Sample the pane and its ancestors, including independent X/Y scaling.
  // OK glows already receive their explicit confirmation transform in glow().
  if(p.source&&!(transition&&transition->preload)&&!std::strstr(pane.panel,"_ok")){
   auto frame=[](const UiAnimationClip* clip,float age){
    if(!clip)return 0.f;
    float f=age*clip->rate;
    if(clip->loop&&clip->duration>0)f=std::fmod(f,clip->duration);
    else f=std::clamp(f,0.f,clip->duration);
    return clip->reverse?clip->duration-f:f;
   };
   const auto* loop=uiFindAnimationClip(p.source,"Loop","Loop");
   if(!loop)loop=uiFindAnimationClip(p.source,"eAFLoop","Loop");
   const bool prompt=!std::strcmp(p.source,"PressStart");
   const auto* fade=prompt?uiFindAnimationClip(p.source,"eAFFade","Fadein"):nullptr;
   const bool pressed=prompt&&transition&&transition->active&&!transition->entering&&!transition->back;
   const auto* press=pressed?uiFindAnimationClip(p.source,"eAFPressStart","PressStart"):nullptr;
   const auto focus=uiFocusAnimation(state,p.focusSlot);
   const bool bound=p.focusSlot<UiFocusControlCount&&!std::strcmp(focus.control,p.source);
   const auto* reference=bound?uiFindAnimationClip(p.source,"Select",std::strstr(pane.panel,"_free")?"Free":"Select"):nullptr;
   const auto motion=uiTextMotion(pane,{{fade,frame(fade,float(state.age)),fade?fade->duration-1:0},
                                       {bound?focus.clip:nullptr,focus.frame,0,reference},
                                       {loop,frame(loop,float(state.age)),frame(loop,0)},
                                       {press,frame(press,pressed?transition->holdFrame:0),frame(press,0)}});
   if(!motion.visible)return;
   if(p.sx!=0&&p.sy!=0){
    const float localX=(x-p.x)/p.sx,localY=-(y+p.y)/p.sy;
    x=p.x+(localX*motion.sx+motion.tx)*p.sx;
    y=-p.y-(localY*motion.sy+motion.ty)*p.sy;
    w*=motion.sx;h*=motion.sy;
   }
   alpha=motion.absoluteAlpha?motion.alpha:alpha*motion.alpha;
  }
  quad(index,x,y,w,h,transition&&transition->preload?1.f:std::clamp(alpha,0.f,1.f),color,p);
 }
 void namedText(const char* key,const char* base,const char* name,Pose p){
  if(const auto* r=pane(base,name))textSprite(lookup().find(key),*r,p);
 }
 void text(const char* text,const char* base,Pose p={},uint32_t tint=0xffffffff,const char* name="text"){
  const auto* r=pane(base,name);if(!r)return;
  const auto* shadow=pane(base,"text_shadow");
  if(!shadow&&!std::strcmp(name,"text"))shadow=pane(base,"TextBox_sha");
  if(shadow)textSprite(lookup().find(text,true,shadow->font,shadow->textStyle),*shadow,p);
  textSprite(lookup().find(text,true,r->font,r->textStyle),*r,p,tint);
 }
 // Text set one character at a time from single-character sprites, for
 // strings the cook can't bake ahead (times). Laid out like the pane's text.
 void chars(const char* text,const char* base,Pose p={},uint32_t tint=0xffffffff,const char* name="text"){
  const auto* r=pane(base,name);if(!r)return;
  const auto& cell=UiFontCells[0];const float scale=r->fontW>0?r->fontW/cell[0]:r->h/cell[1];
  std::array<unsigned,32> glyph{};std::array<float,32> advance{};unsigned n=0;float total=0;
  for(const char* c=text;*c&&n<glyph.size();++c){const char one[2]{*c,0};
   const unsigned i=lookup().find(one,true,r->font,r->textStyle);glyph[n]=i;
   // A space has no glyph to bake: it only moves the next character along.
   advance[n]=i<std::size(UiSprites)?UiSprites[i].w*scale:*c==' '?cell[0]*scale*.4f:0;total+=advance[n++];}
  float x=r->x+std::max(0.f,r->w-total)*(r->textAlign==0?0:r->textAlign==2?1:.5f);
  for(unsigned k=0;k<n;++k){if(glyph[k]>=std::size(UiSprites)){x+=advance[k];continue;}
   UiPaneInfo one=*r;one.x=x;one.w=UiSprites[glyph[k]].w*scale;one.textAlign=0;
   textSprite(glyph[k],one,p,tint);x+=one.w;}
 }
 void icon(const char* prefix,unsigned i,const char* base,const char* name,Pose p={},float a=1,bool hidden=false){char key[64];std::snprintf(key,sizeof key,"%s%u",prefix,i);inPane(key,base,name,p,a,hidden);}
 // A focusable button's live animation state, when this pose is bound to it.
 bool boundFocus(Pose p,UiFocusView& view)const{
  if(!p.source||p.focusSlot>=UiFocusControlCount)return false;
  view=uiFocusAnimation(state,p.focusSlot);
  return !std::strcmp(view.control,p.source);
 }
 // MovieLayout draws a movie pane with its own TEV (PAL fn_1_EB7AC 805fbb60)
 // and reads the pane material's texture SRT rotation, rounded, as a mode.
 // Mode 3 (black_base while a drift/class/VS/battle button is Free; the
 // Select group overrides the Loop group) scales the colour by K3 = 127/255
 // (0x808963e0..ef also halve the chroma terms); 0 is the plain movie.
 bool dimMovie(Pose p,const char* name,bool select)const{
  UiFocusView view;const bool live=boundFocus(p,view)&&view.clip;
  const UiAnimationClip* clip=live?view.clip:p.source?uiFindAnimationClip(p.source,"Select",select?"Select":"Free"):nullptr;
  return clip&&std::lround(uiAnimationValue(*clip,name,"RLTS",2,live?view.frame:0.f,0.f))==3;
 }
 void movie(const char* prefix,unsigned i,const char* base,const char* name,Pose p={},float a=1,float u0=0,float v0=0,float u1=1,float v1=1){
  char key[80];std::snprintf(key,sizeof key,"%s%u",prefix,i);
  unsigned frame=0;if(const auto* movie=uiFindMovie(key))frame=unsigned(double(state.age)*movie->fps*UiMovieFrameCount/(60.0*movie->frames))%UiMovieFrameCount;
  char sampled[96];std::snprintf(sampled,sizeof sampled,"%s_%u",key,frame);
  char family[64];std::snprintf(family,sizeof family,"%s",base);bool select=false;
  for(const char* suffix:{"_select","_free"}){const size_t n=std::strlen(family),m=std::strlen(suffix);
   if(n>m&&!std::strcmp(family+n-m,suffix)){family[n-m]=0;select=suffix[1]=='s';break;}}
  // MovieLayout's calc (PAL 807e8698) rewrites each movie pane's own alpha
  // every frame: 1 until the layout activates, then (now-start)*7 up to 255.
  // The authored alpha (drift_select black_base: 70) never reaches a playing
  // movie, so it is divided out here; ancestors' alpha still applies.
  const auto* node=uiFindPaneNode(family,name);
  const float own=node&&node->alpha>0?node->alpha:1.f;
  const auto before=out.size();
  inPane(sampled,base,name,p,a/own);
  if(out.size()>before){auto& q=out.back();q.u0=u0+(u1-u0)*q.u0;q.u1=u0+(u1-u0)*q.u1;q.v0=v0+(v1-v0)*q.v0;q.v1=v0+(v1-v0)*q.v1;
   if(const unsigned n=uiFindMovieRegion(key);n<std::size(UiMovieRegions))q.movie=n;
   if(!(transition&&transition->preload)&&dimMovie(p,name,select))q.tint=0x7f7f7fffu;
  }
 }
 void glow(const char* family,Pose p,const char* caption=nullptr,unsigned icon=~0u){
  if(!transition||transition->entering||(!transition->active&&!transition->preload&&!transition->confirming)||transition->back||!p.source||std::strcmp(p.source,transition->selectedControl))return;
  const auto* clip=uiFindAnimationClip(p.source,"OK","OK");if(!clip)return;
  float frame=transition->holdFrame*clip->rate;
  if(frame>=clip->duration){
   if(!*clip->next)return;
   frame-=clip->duration;clip=uiFindAnimationClip(p.source,"OK",clip->next);if(!clip)return;
   if(clip->loop&&clip->duration>0)frame=std::fmod(frame,clip->duration);
  }
  const Name base(family,"_ok");
  for(const auto& g:UiGlows){
   if(std::strcmp(g.panel,family))continue;
   const float alpha=transition->preload?1.f:std::clamp(uiAnimationValue(*clip,g.pane,"RLVC",16,frame,0.f)/255.f,0.f,1.f);if(alpha<=0)continue;
   const float sx=uiAnimationValue(*clip,g.pane,"RLPA",6,frame,1.f),sy=uiAnimationValue(*clip,g.pane,"RLPA",7,frame,1.f);
   Pose pose=p;pose.x+=g.pivotX*p.sx*(1-sx);pose.y-=g.pivotY*p.sy*(1-sy);pose.sx*=sx;pose.sy*=sy;
   const auto first=out.size();
   if(g.variant==0&&!*g.key){if(caption)text(caption,base.c_str(),pose,0xffffffff,g.pane);}
   else if(g.variant==0)panel(g.key,pose);
   else if((g.variant==1||g.variant==2)&&icon!=~0u){char key[64];std::snprintf(key,sizeof key,"%s%u",g.key,icon);panel(key,pose);}
   else if(g.variant==3&&icon<12){
    char key[64];std::snprintf(key,sizeof key,"vehicle_%c%u","sml"[UiCharacters[std::min(state.selection.driver,25u)].weight],icon);
    inPane(key,base.c_str(),g.pane,pose);
    if(out.size()>first){auto& q=out.back();q.vehicleIcon=icon;q.vehicleColor0=g.materialColors[0];q.vehicleColor1=g.materialColors[1];}
   }
   for(auto at=first;at<out.size();++at){out[at].alpha*=alpha;out[at].additive=g.blend!=0;}
  }
 }
 void button(const char* base,unsigned slot,Pose p,const char* caption=nullptr){
  p.focusSlot=slot;
  auto key=buttonState(base,slot);panel(key.c_str(),p);
  if(caption){text(caption,key.c_str(),p);if(state.cursor==slot)glow(base,p,caption);}
 }
 void chrome(const char* title,const char* description=""){
  panel("header",motionOnly("MenuObiTop","MenuObiTop"));panel(*description?"footer_help":"footer",control("MenuObiBottom","MenuObiBottom"));
  text(title,"header_text",control("MenuObiTitleText","MenuObiTitleText"));
  Pose back=control("Back","ButtonBack");panel("back_free",back);inPane("button_back","back_free","text",back);
  if(*description)text(description,"footer_help",control("MenuObiInstructionText","MenuObiInstructionText"),0xffffffff);
 }
 void unlocked(){
  if(state.unlockNotice<0)return;const unsigned i=unsigned(state.unlockNotice);
  const char* cls=Progress::unlockableClass(i);
  panel("button_select",{0,-12,1.1f,cls?2.6f:2.f});label("Unlocked!",0,cls?-62:-52,400,37,0xffe040ffu);
  label(Progress::unlockableName(i),0,cls?-22:-12,400,37);if(cls)label(cls,0,18,400,37);
 }
 void notice(){if(!state.unavailable)return;panel("button_select",{0,-12,.95f,1.35f});label(state.locked?"Locked":"Not available",0,-20,400,37);}
 void dim(std::size_t from){for(auto i=from;i<out.size();++i)out[i].tint=LockedTint;}
};
}
std::vector<UiQuad> composeMenu(const MenuState& state,const UiRaceView& race){std::vector<UiQuad> out;composeMenu(state,race,out);return out;}
static void composeMenuImpl(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q,const UiTransitionView* transition){
 // The race and its end-of-race menu draw themselves (RaceFlowUi draws the
 // retail result boards and buttons over the running race).
 q.clear();q.reserve(256);if(s.page==UiPage::Race||s.page==UiPage::Results)return;
 if(s.page==UiPage::Confirm){
  MenuState base=s;
  if(transition&&transition->underlay)base=*transition->underlay;
  else{
   base.page=s.selection.mode==UiMode::GrandPrix?UiPage::Cup:s.selection.mode==UiMode::Battle?UiPage::BattleStage:UiPage::Course;
   base.cursor=base.page==UiPage::Cup?s.selection.course/4%8:base.page==UiPage::BattleStage?s.selection.battleStage%5:s.selection.course%4;
   base.cupPage=s.selection.course/4/8;
  }
  UiTransitionView underlayView;
  underlayView.preload=transition&&transition->preload;
  if(transition&&transition->underlay){
   underlayView.confirming=true;
   underlayView.holdFrame=std::max(0.f,transition->underlayHoldFrame);
   underlayView.selectedControl=transition->underlaySelectedControl;
  }
  composeMenuImpl(base,race,q,underlayView.confirming||underlayView.preload?&underlayView:nullptr);
 }
 Composer c(s,q,transition);const unsigned driver=std::min(s.selection.driver,unsigned(UiCharacters.size()-1));
 const bool preload=transition&&transition->preload;
 const unsigned course=std::min(s.selection.course,RaceCourseCount-1),weight=UiCharacters[driver].weight;
 const char* modes[]={"Grand Prix","Time Trials","VS","Battle"};
 // The first quad is always the background, allowing the original 3D menu
 // scene to be inserted between it and the authored controls.
 if(s.page!=UiPage::Confirm)c.panel(s.page==UiPage::Title||s.page==UiPage::Intro?"title_scene":s.page==UiPage::MainMenu||s.page==UiPage::License?"title_blur_scene":"background");
 switch(s.page){
 case UiPage::Intro:
  if(!q.empty())if(const unsigned i=uiFindMovieRegion("intro_movie");i<std::size(UiMovieRegions))q.back().movie=i;
  break;
 case UiPage::Strap:
  c.sprite(s.age<180?"strap_a":"strap_b",-304,-228,608,456);
  // Residency covers every age-dependent variant this page can show.
  if(preload)c.sprite(s.age<180?"strap_b":"strap_a",-304,-228,608,456);
  break;
 case UiPage::Title:
  {
   const Pose p=control("PressStart","press_start");
   c.text("Press the A Button","press_start",p,0xffffffff,"text_00");
   const auto first=q.size();
   c.text("Press the A Button","press_start",p,0xffffffff,"text_light");
   for(auto i=first;i<q.size();++i)q[i].additive=UiPressStartLightAdditive;
  }break;
 case UiPage::License:{
  c.chrome("Select License");
  for(unsigned i=0;i<4;++i){char name[32];std::snprintf(name,sizeof name,"License%u",i);Pose p=control(i<2?"LicenseSelectT":"LicenseSelectB",name,i);auto base=c.buttonState(i<2?"license_top_empty":"license_bottom_empty",i);c.panel(base.c_str(),p);
   // A used slot shows its license's name, set a character at a time.
   if(const auto& l=progress().license[i];l.used)c.chars(l.name,base.c_str(),p,0xffffffff,"new");else c.text("NEW",base.c_str(),p,0xffffffff,"new");if(i==s.cursor)c.glow(i<2?"license_top_empty":"license_bottom_empty",p);
  }
  Pose option=control("LicenseSelectS","Option");c.panel("option_free",option);c.text("Settings","option_free",option);
  break;
 }
 case UiPage::MainMenu:{
  const char* titles[]={"Single Player","Multiplayer","Nintendo WFC","Mario Kart Channel"};
  const char* groups[]={"TopMenuSingleWaku","TopMenuMultiWaku","TopMenuWifiWaku","TopMenuChannelWaku"};
  for(unsigned i=0;i<4;++i){Pose p=control(groups[i],groups[i],UiFocusMainParentBase+i);c.panel("main_free",p);// common_w069: text_sha, text (alpha 128), text_light, then text_00, the opaque title drawn last. The frames never select (uiFocus selected()), so text_light stays in its Free pose.
   for(const char* field:{"text_sha","text","text_light","text_00"})c.text(titles[i],"main",p,0xffffffff,field);
   if(i==0){auto b=child(p,control("TopMenuSingle","ButtonSingle",0));auto base=c.buttonState("main_button",0);c.movieButton(base.c_str(),b);c.movie("main_movie_",0,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);c.text("1\nPlayer",base.c_str(),b);if(s.cursor==0)c.glow("main_button",b,"1\nPlayer");}
   if(i==1||i==2){unsigned n=i==1?3:2;for(unsigned k=0;k<n;++k){char v[48];std::snprintf(v,sizeof v,i==1?"ButtonMulti%uP":"ButtonWifi%uP",k+(i==1?2:1));Pose b=child(p,control(i==1?"TopMenuMulti":"TopMenuWifi",v,i==1?1+k:4+k));const auto slot=i==1?1+k:4+k;auto base=c.buttonState(i==1?"main_multi":"main_wifi",slot);c.movieButton(base.c_str(),b);c.movie("main_movie_",slot,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);char caption[20];const unsigned players=k+(i==1?2:1);std::snprintf(caption,sizeof caption,"%u\n%s",players,players==1?"Player":"Players");c.text(caption,base.c_str(),b);if(s.cursor==slot)c.glow(i==1?"main_multi":"main_wifi",b,caption);}}
   if(i==3){Pose b=child(p,control("TopMenuChannel","ButtonChannel",6));auto base=c.buttonState("main_button",6);c.movieButton(base.c_str(),b);c.movie("main_movie_",6,base.c_str(),"black_base",b);c.movieOverlay(base.c_str(),b);}
  }
  c.chrome("Main Menu",s.cursor==0?UiHelpMain[0]:s.cursor<4?UiHelpMainMulti[s.cursor-1]:s.cursor<6?UiHelpWfc[s.cursor-4]:UiHelpMain[3]);
  // License Settings overlaps the top banner and draws above it.
  {Pose edit=control("FileAdmin","ButtonFileAdmin");c.panel("license_edit_free",edit);c.text("License Settings","license_edit_free",edit);}break;
 }
 case UiPage::Mode:{
  const char* variants[]={"ButtonGP","ButtonTA","ButtonVS","ButtonBT"};
  for(unsigned i=0;i<4;++i){Pose p=control("SingleTop",variants[i],i);auto base=c.buttonState("mode",i);c.movieButton(base.c_str(),p);
   // The native page reconstructs ONE selected movie across four rows and
   // four narrow panes. The wide pane at the right is the caption backing.
   const char* panes[]={"black_base","black_base_00","black_base_01","black_base_02"};
   for(unsigned j=0;j<4;++j)c.movie("mode_movie_",std::min(s.cursor,3u),base.c_str(),panes[j],p,1,j*.25f,i*.25f,(j+1)*.25f,(i+1)*.25f);
   c.movieOverlay(base.c_str(),p);
   c.text(modes[i],base.c_str(),p,0xffffffff);if(s.cursor==i)c.glow("mode",p,modes[i]);}
  c.chrome("Single Player",UiHelpMode[std::min(s.cursor,3u)]);break;
 }
 case UiPage::EngineClass:{
  // Retro Rewind: 100cc, 150cc, 200cc, Mirror (MenuState::slotClass); 200cc uses 150cc's art.
  const char* names[]={"50cc","100cc","150cc","Mirror"};const char* rrNames[]={"100cc","150cc","200cc","Mirror"};const char* variants[]={"Button50cc","Button100cc","Button150cc","ButtonMirror"};
  for(unsigned i=0;i<4;++i){Pose p=control("ClassSelect2",variants[i],i);auto base=c.buttonState("class",i);const unsigned art=MenuState::retailClass(i);
   c.movieButton(base.c_str(),p);c.movie("class_movie_",s.engineClassMovie(i),base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);
   {const auto first=q.size();c.icon("class_",art,base.c_str(),"kinoko_set_p",p);if(art==3&&!progress().mirror())c.dim(first);}
   char caption[64];
   if(RetroRewind)std::snprintf(caption,sizeof caption,"%s\nKarts and Bikes",rrNames[i]);
   else std::snprintf(caption,sizeof caption,"%s\n%s",names[i],i==0&&!s.selection.mixedEngineTypes?"Karts Only":i==1&&!s.selection.mixedEngineTypes?"Bikes Only":"Karts and Bikes");
   c.text(caption,base.c_str(),p);if(s.cursor==i)c.glow("class",p,caption);}
  if(RetroRewind){c.chrome("Engine Classes",UiHelpEngineClassesAllKartBike[MenuState::retailClass(std::min(s.cursor,3u))]);break;}
  {Pose toggle=control("ClassChange","ClassChange");char key[48];
   for(unsigned blink=0;blink<(preload?2u:1u);++blink){const unsigned frame=((s.age/30)+blink)%2;
    std::snprintf(key,sizeof key,"button_switch_shadow_%u",frame);c.namedText(key,"class_switch","close_sha",toggle);
    std::snprintf(key,sizeof key,"button_switch_%u",frame);c.namedText(key,"class_switch","close",toggle);}}
  c.chrome("Engine Classes",s.selection.mixedEngineTypes&&s.cursor<2?UiHelpEngineClassesAllKartBike[s.cursor]:UiHelpEngineClasses[std::min(s.cursor,3u)]);break;
 }
 case UiPage::Character:{
  Pose parent=control("CharacterSelectNULL","CharacterSelectNULL");constexpr unsigned within[]={0,1,4,6,2,3,5,7};
  for(unsigned i=0;i<s.count();++i){char group[48];std::snprintf(group,sizeof group,"CharacterSelect4_%u",i<24?i/8:3);Pose p=child(parent,numbered(group,i<24?within[i%8]:i-24));p.focusSlot=i;c.button("character",i,p);{const auto first=q.size();c.icon("driver_",s.characterDriver(i),c.buttonState("character",i).c_str(),"chara",p);if(!progress().character(s.characterDriver(i)))c.dim(first);}if(s.cursor==i)c.glow("character",p,nullptr,s.characterDriver(i));}
  Pose name=control("CharacterSelectName","CharacterSelectName");c.panel("character_name",name);c.text(UiCharacters[s.characterDriver(s.cursor)].name,"character_name",name,0xffffffff,"mii_name");
  // Retro Rewind skins: the chosen one under the name (tools/cook_menu_ui.py bakes the labels).
  const unsigned driverSkins=s.characterDriver(s.cursor)<GarageSkinCounts.size()?GarageSkinCounts[s.characterDriver(s.cursor)]:0;
  if(RetroRewind&&driverSkins){char skin[16];if(s.selection.skin)std::snprintf(skin,sizeof skin,"Skin %u",s.selection.skin);else std::snprintf(skin,sizeof skin,"Default");
   c.label(skin,name.x,-name.y+26,240,24);c.chrome("Characters","L / R: Skin");break;}
  c.chrome("Characters");break;
 }
 case UiPage::Vehicle:{
  Pose parent=control("MachineSelectNULL6","MachineSelectNULL");
  for(unsigned i=0;i<s.count();++i){
   unsigned row=i/s.columns();Pose p=child(parent,numbered("MachineSelectNULL6",row,"MachineSelectNULL"));
   p=child(p,numbered(s.columns()==2?"MachineSelect2_6":"MachineSelect1_6",i%s.columns()));p.focusSlot=i;
   c.button("vehicle",i,p);const auto base=c.buttonState("vehicle",i);
   char key[48];std::snprintf(key,sizeof key,"vehicle_%c%u","sml"[weight],s.vehicleChoice(i));
   // Original common_w030_machine_12 pane submission order and C0/C1.
   // The free-state grey overlay keeps its authored50/255 pane opacity.
   const char* layers[]={"chara_shadow","chara","chara_c_down"};
   for(unsigned layer=0;layer<3;++layer){
    const auto first=q.size();c.inPane(key,base.c_str(),layers[layer],p);
    if(q.size()>first){auto& icon=q.back();icon.vehicleIcon=s.vehicleChoice(i);
     icon.vehicleColor0=0;icon.vehicleColor1=layer==0?0x000000ffu:!progress().vehicle(weight,s.vehicleChoice(i))?LockedTint:layer==1&&s.cursor==i?0xffffffffu:0xc8c8c8ffu;
    }
   }
   if(s.cursor==i)c.glow("vehicle",p,nullptr,s.vehicleChoice(i));
  }
  Pose name=control("MachineSelectName","MachineSelectName");c.panel("vehicle_name",name);c.text(UiVehicleNames[weight][s.vehicleChoice(s.cursor)],"vehicle_name",name,0xffffffff,"mii_name");
  const char* stats[]={"Speed","Weight","Acceleration","Handling","Drift","Off-Road","Mini-Turbo"};const char* variants[]={"Speed","Weight","Accel","Handling","Drift","Dirt","Miniturbo"};
  Pose base=control("MachineAbilityNULL","MachineAbilityNULL");for(unsigned i=0;i<7;++i){Pose p=child(base,control("MachineAbility",variants[i]));c.panel("vehicle_ability",p);const unsigned value=UiDriverStats[UiCharacters[driver].retailId][i]+UiVehicleStats[weight][s.vehicleChoice(s.cursor)][i];c.graph(float(value+(i==0?10:5))/(i==0?35:30),p);c.text(stats[i],"vehicle_ability",p,0xffffffff,"ability_text");}
  c.chrome("Vehicles");break;
 }
 case UiPage::Drift:
  // drift_select's lower clip is the automatic demo, the upper one manual.
  for(unsigned i=0;i<2;++i){Pose p=control("PadEasy",i==0?"ButtonEasy":"ButtonNormal",i);auto base=c.buttonState("drift",i);c.movieButton(base.c_str(),p);c.movie("drift_movie_",1-i,base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);c.text(i==0?"Automatic":"Manual",base.c_str(),p);if(s.cursor==i)c.glow("drift",p,i==0?"Automatic":"Manual");}c.chrome("Drift",UiHelpDrift[std::min(s.cursor,1u)]);
  {Pose help=control("DriftHelp","DriftHelp");c.panel("drift_help_free",help);c.text("Help","drift_help_free",help);}break;
 case UiPage::VSMode:case UiPage::BattleMode:{
  bool battle=s.page==UiPage::BattleMode;const char* names[]={battle?"Balloon Battle":"Solo Race",battle?"Coin Runners":"Team Race"};
  for(unsigned i=0;i<2;++i){Pose p=control(battle?"BattleSelect":"VSSelect",battle?(i?"ButtonCoin":"ButtonBalloon"):(i?"ButtonTeam":"ButtonIndividual"),i);auto base=c.buttonState(battle?"battle":"vs",i);c.movieButton(base.c_str(),p);c.movie(battle?"battle_mode_movie_":"vs_movie_",i,base.c_str(),"black_base",p);c.movieOverlay(base.c_str(),p);c.text(names[i],base.c_str(),p);if(s.cursor==i)c.glow(battle?"battle":"vs",p,names[i]);}
  c.chrome(battle?"Battle":"VS");break;
 }
 case UiPage::Rules:{
  // Pages::VSSettings / BattleSettings: RadioBase rows with their RadioOption
  // buttons (the chosen one in its Choice-on state), the UpDown race or win
  // count, and OK. Battle shares the layouts under its own control names.
  const bool battle=s.selection.mode==UiMode::Battle;
  const char* base=battle?"BattleSettingRadioBase":"VSSettingRadioBase";
  const char* option=battle?"BattleSettingRadioOption":"VSSettingRadioOption";
  unsigned focusRow,focusOption;s.ruleCursor(focusRow,focusOption);
  static const char* rowVariants[]={"RadioClass","RadioLevel","RadioMachine","RadioCourse","RadioItem"};
  static const char* optionVariants[]={"OptionClass","OptionLevel","OptionMachine","OptionCourse","OptionItem"};
  static const char* rowLabels[]={"Class","CPU","CPU Vehicles","Courses","Items"};
  static const char* options[5][4]={{"50cc","100cc","150cc","Mirror"},{"Easy","Normal","Hard",""},{"All","Karts","Bikes",""},{"Choose","Random","In Order",""},{"Balanced","Aggressive","Strategic","None"}};
  for(unsigned row=0;row<s.ruleRows();++row){
   const auto info=s.ruleRow(row);const bool focused=row==focusRow;
   if(info.row==UiRuleRow::Ok){
    Pose ok=control(battle?"BattleSetting":"VSSetting","ButtonOK",s.ruleSlot(row,0));
    c.button("button",s.ruleSlot(row,0),ok,"OK");continue;
   }
   if(info.row==UiRuleRow::Count){
    const Pose updown=control(battle?"BattleSettingUpDownBase":"VSSettingUpDownBase","UpDownRaceNum");
    const char* rowBase=focused?"rule_row_select":"rule_row_free";
    c.panel(rowBase,updown);c.text(battle?"Win Count":"Race Count",rowBase,updown);
    const Pose value=child(updown,control(battle?"BattleSettingUpDownValue":"VSSettingUpDownValue","Value"));
    c.panel(focused?"rule_value_select":"rule_value_free",value);
    const Pose left=child(updown,control(battle?"BattleSettingUpDownButtonL":"VSSettingUpDownButtonL","LeftButton"));
    const Pose right=child(updown,control(battle?"BattleSettingUpDownButtonR":"VSSettingUpDownButtonR","RightButton"));
    c.panel(focused?"rule_left_select":"rule_left_free",left);c.panel(focused?"rule_right_select":"rule_right_free",right);
    char label[24];std::snprintf(label,sizeof label,battle?"Wins: %u":"Races: %u",battle?s.selection.battleWins:s.selection.raceCount);
    c.text(label,"rule_value_text",child(value,control(battle?"BattleSettingUpDownText":"VSSettingUpDownText","Text")));
    continue;
   }
   const unsigned kind=unsigned(info.row);
   const Pose rowPose=control(base,rowVariants[kind]);
   const char* rowBase=focused?"rule_row_select":"rule_row_free";
   c.panel(rowBase,rowPose);c.text(battle&&info.row==UiRuleRow::Courses?"Stages":rowLabels[kind],rowBase,rowPose);
   const unsigned chosen=s.ruleChoice(info.row);
   for(unsigned i=0;i<info.options;++i){
    char variant[24];std::snprintf(variant,sizeof variant,"%s%u",optionVariants[kind],i);
    const Pose p=child(rowPose,control(option,variant));
    const bool on=i==chosen,select=focused&&i==focusOption;
    char panel[32];std::snprintf(panel,sizeof panel,"%s%s",on?"rule_option_on":"rule_option",select?"_select":"_free");
    static constexpr const char* rrClasses[]={"100cc","150cc","200cc","Mirror"};
    const char* label=RetroRewind&&info.row==UiRuleRow::Class?rrClasses[std::min(i,3u)]:options[kind][i];
    c.panel(panel,p);c.text(label,panel,p,0xffffffff,on?"active_text":"text");
   }
  }
  const auto row=s.ruleRow(focusRow).row;
  const char* help=row==UiRuleRow::Class?UiHelpRulesClass[std::min(focusOption,3u)]:row==UiRuleRow::Cpu?UiHelpRulesCpu[std::min(focusOption,2u)]
   :row==UiRuleRow::Vehicles?UiHelpRulesVehicles[std::min(focusOption,2u)]:row==UiRuleRow::Courses?(battle?UiHelpRulesStages:UiHelpRulesCourses)[std::min(focusOption,2u)]
   :row==UiRuleRow::Items?UiHelpRulesItems[std::min(focusOption,3u)]:row==UiRuleRow::Count?UiHelpRulesCount[battle]:UiHelpRulesOk[battle];
  c.chrome("Rules",help);break;
 }
 case UiPage::Cup:case UiPage::Course:{
  // Eight cup slots per page (cupPage: more than one in the Retro Rewind edition).
  const bool pickCup=s.page==UiPage::Cup;
  const unsigned first=(pickCup?s.cupPage:course/4/8)*8,shown=std::min(8u,RaceCupCount-first);
  const unsigned selectedSlot=pickCup?std::min(s.cursor,shown-1):course/4%8,selectedCup=first+selectedSlot;
  Pose cups=control("CupSelectNULL","CupSelectCupNULL"),courses=control("CupSelectNULL","CupSelectCourseNULL");
  // The chosen cup draws last (retail lifts it to z+200) over its neighbours.
  for(unsigned n=0;n<shown;++n){const unsigned i=n<selectedSlot?n:n==shown-1?selectedSlot:n+1,cup=first+i;
   Pose p=child(cups,cupSlot(pickCup?"CupSelectCup":"CourseSelectCup",i));const char* base=i==selectedSlot?"cup_select":"cup_free";
   if(!pickCup){
    // CtrlMenuCourseSelectCupSub::calcSelf (USA 807de7d4): t advances 20 per
    // frame to 300. The chosen cup slides to the CupSelectCupNULL origin and
    // grows to 1.5x; the other cups fade out (alpha 300-t, clamped to 255).
    const float t=std::min(300.f,20.f*float(s.age+1));
    if(i==selectedSlot){const float k=t/300.f;p.x+=(cups.x-p.x)*k;p.y+=(cups.y-p.y)*k;p.sx*=1+t/600.f;p.sy*=1+t/600.f;}
    else{p.alpha*=std::clamp(300.f-t,0.f,255.f)/255.f;if(p.alpha<=0)continue;}
   }
   c.panel(base,p);
   {const auto mark=q.size();c.icon("cup_",cup,base,"icon",p);
    if(!(s.selection.mode==UiMode::TimeTrials?progress().anyClassCup(cup):progress().cup(unsigned(s.selection.engineClass),cup))
       ||!(Courses[cup*4].supported||Courses[cup*4+1].supported||Courses[cup*4+2].supported||Courses[cup*4+3].supported))c.dim(mark);}
   // Retro Rewind shows no cup names (its cups have none).
   if(RetroRewind){if(pickCup&&i==selectedSlot)c.glow("cup",p,nullptr,cup);}
   else if(i==selectedSlot||(pickCup&&uiFocusAnimation(s,i).changing())){
    c.text(cupName(cup),"cup_select",p);if(pickCup&&i==selectedSlot)c.glow("cup",p,cupName(cup),cup);
   }
  }
  if(pickCup){
   if(RetroRewind){
    // No preview movie: the course names over the background, and the page arrows.
    c.panel("ghost_arrow_left_free",control("CupsArrowL","CupsArrowL"));c.panel("ghost_arrow_right_free",control("CupsArrowR","CupsArrowR"));
   }else{
    Pose movie=child(cups,control("CourseSelectMovie","RaceCupMovie"));
    c.panel("course_movie_frame",movie);c.movie("cup_movie_",selectedCup,"course_movie_frame","black_base",movie);
   }
   const unsigned highlighted=(unsigned(double(s.age)*59.94/60.0)/120)%4;
   for(unsigned i=0;i<4;++i){Pose p=child(courses,numbered("CupSelectCourse",i,"Course"));
    c.text(courseName(selectedCup*4+i),i==highlighted?"cup_course_select":"cup_course_free",p);
    if(preload)c.text(courseName(selectedCup*4+i),i==highlighted?"cup_course_free":"cup_course_select",p);
   }
  }else{
   for(unsigned i=0;i<4;++i){Pose p=child(courses,numbered("CourseSelectCourse",i));unsigned ci=selectedCup*4+i;
    const char* base=s.cursor==i?"course_select":"course_free";const auto mark=q.size();c.movieButton(base,p);
    // Retro Rewind tracks show their course map (tools/cook_menu_ui.py map_N) in place of a movie.
    if(RetroRewind)c.icon("map_",ci,base,"black_base",p);else c.movie("course_preview_",ci,base,"black_base",p);
    c.movieOverlay(base,p);c.text(courseName(ci),base,p);if(s.cursor==i)c.glow("course",p,courseName(ci));
    if(!Courses[ci].supported)c.dim(mark);
   }
  }
  c.chrome(pickCup?"Cups":"Courses",RetroRewind&&pickCup?"L / R: More Cups":"");break;
 }
 case UiPage::BattleCup:case UiPage::BattleStage:{
  const bool pick=s.page==UiPage::BattleCup;const unsigned cup=pick?std::min(s.cursor,1u):std::min(s.selection.battleStage/5,1u);
  const char* names[]={"Wii Stages","Retro Stages"};
  const Pose cups=control("BattleCupSelectNULL","CupSelectCupNULL"),stages=control("BattleCupSelectNULL","CupSelectCourseNULL");
  for(unsigned i=0;i<2;++i){
   const Pose p=child(cups,numbered(pick?"BattleCupSelectCup":"BattleStageSelectCup",i));
   const char* base=i==cup?"cup_select":"cup_free";
   c.panel(base,p);c.icon("battle_cup_",i,base,"icon",p);
   if(i==cup||(pick&&uiFocusAnimation(s,i).changing())){
    c.text(names[i],"cup_select",p);if(pick&&i==cup)c.glow("cup",p,names[i],BattleGlow+i);
   }
  }
  if(pick){
   const Pose movie=child(cups,control("CourseSelectMovie","BattleCupMovie"));
   c.panel("course_movie_frame",movie);c.movie("battle_cup_movie_",cup,"course_movie_frame","black_base",movie);
   const unsigned highlighted=(unsigned(double(s.age)*59.94/60.0)/120)%5;
   for(unsigned i=0;i<5;++i){
    const Pose p=child(stages,numbered("BattleCupSelectStage",i,"Course"));
    c.text(UiBattleStageNames[cup*5+i],i==highlighted?"battle_course_select":"battle_course_free",p);
    if(preload)c.text(UiBattleStageNames[cup*5+i],i==highlighted?"battle_course_free":"battle_course_select",p);
   }
  }else{
   // The selected stage's native atlas region is split across five controls,
   // matching BattleStage_ControlStage_OnSelect rather than five previews.
   for(unsigned i=0;i<5;++i){
    const Pose p=child(stages,numbered("BattleStageSelectStage",i));
    const char* base=s.cursor==i?"course_select":"course_free";
    c.movieButton(base,p);c.movie("battle_preview_",cup*5+std::min(s.cursor,4u),base,"black_base",p,1,0,float(i)/5,1,float(i+1)/5);
    c.movieOverlay(base,p);c.text(UiBattleStageNames[cup*5+i],base,p);
    if(s.cursor==i)c.glow("course",p,UiBattleStageNames[cup*5+i]);
   }
  }
  c.chrome(pick?"Cups":"Stages");break;
 }
 case UiPage::Ghost:{
  const char* names[]={"Race This Ghost","Watch the Replay","Solo Time Trials"};
  const char* variants[]={"Run","Watch","Alone"};
  // Without a ghost the page is Solo alone: no card, no ghost buttons.
  if(!s.ghostAvailable){c.button("ghost",0,control("TimeAttackGhostList",variants[2]),names[2]);c.chrome("Ghosts");break;}
  // The ghost's card (common_w075_ghost_data): course, time, racer and machine.
  const Pose info=control("GhostInfo","TimeAttackGhostList");
  c.panel("ghost_info",info);
  c.text(courseName(s.selection.course),"ghost_info",info,0xffffffff,"course_name");
  {const uint32_t ms=s.ghostMilliseconds;char time[16];std::snprintf(time,sizeof time,"%u'%02u\"%03u",unsigned(ms/60000),unsigned(ms/1000%60),unsigned(ms%1000));
   c.chars(time,"ghost_info",info,0xffffffff,"time");}
  const unsigned driver=std::min(s.ghostDriver,25u),weight=UiCharacters[driver].weight;
  c.text(UiCharacters[driver].name,"ghost_info",info,0xffffffff,"player");
  c.icon("driver_",driver,"ghost_info","chara",info,1,true);
  {const unsigned vehicle=std::min(s.ghostVehicle,11u);char key[32];std::snprintf(key,sizeof key,"vehicle_%c%u","sml"[weight],vehicle);
   const auto first=q.size();c.inPane(key,"ghost_info","machine",info,1,true);
   if(q.size()>first){auto& icon=q.back();icon.vehicleIcon=vehicle;icon.vehicleColor0=0;icon.vehicleColor1=0xffffffffu;}}
  for(unsigned i=0;i<3;++i)c.button("ghost",i,control("TimeAttackGhostList",variants[i]),names[i]);
  c.chrome("Ghosts");break;
 }
 case UiPage::Confirm:{
  c.panel("confirm_back",control("RaceConfirmBack","BlackBack"));
  c.panel("confirm_top",control("MenuObiTopRaceConfirm","MenuEndConfirmObiTop"));
  c.panel("confirm_bottom",control("MenuObiBottomRaceConfirm","MenuEndConfirmObiBottom"));
  c.text("Start Race","header_text",control("MenuObiTitleText","MenuEndConfirmObiTitleText"));
  Pose back=control("Back","ButtonBackMenuEndConfirm");c.panel("back_free",back);c.inPane("button_back","back_free","text",back);
  c.button("confirm",0,control("MenuEndConfirm","ButtonOK"),"OK");break;
 }
 case UiPage::Pause:{
  c.chrome("Pause");const char* variants[]={"ButtonContinue","ButtonRestart","ButtonQuit"};
  // The native port has three actions. Retain that layout, while using the
  // original race controller's delays and shared common_w004 animations.
  for(unsigned i=0;i<3;++i){auto p=control(uiPauseMenuControl(s),variants[i]);p.y=90.f-float(i)*85;
   c.button("button",i,p,i==0?"Continue":i==1?"Restart":"Quit");}break;
 }
 case UiPage::QuitConfirm:{
  c.chrome("Quit this race?");auto yes=control("YesNoButton","Option0"),no=control("YesNoButton","Option1");
  yes.x=no.x=0;yes.y=50;no.y=-55;c.button("button",0,yes,"Yes");c.button("button",1,no,"No");break;
 }
 case UiPage::Error:{c.chrome("Race unavailable");auto p=control("ConfirmOK","OK");p.x=p.y=0;
  c.button("button",0,p,"Choose another course");break;}
 default:break;
 }
 c.notice();c.unlocked();
}
void composeMenu(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q){composeMenuImpl(s,race,q,nullptr);}
void composeMenu(const MenuState& s,const UiRaceView& race,std::vector<UiQuad>& q,const UiTransitionView& t){composeMenuImpl(s,race,q,&t);}

}
