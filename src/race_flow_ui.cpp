#include <mkw/race_flow_ui.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/display_layout.hpp>
#include <mkw/race_flow_layout.hpp>
#include <mkw/race_results.hpp>
#include <mkw/renderer.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace mkw {
namespace {
GPU_TEXTURE_WRAP_PARAM wrapMode(uint8_t gx){return gx==1?GPU_REPEAT:gx==2?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE;}
static_assert(sizeof(FlowQuad)==sizeof(Renderer::MaterialQuad)&&std::is_standard_layout_v<FlowQuad>&&std::is_standard_layout_v<Renderer::MaterialQuad>,
              "FlowQuad mirrors Renderer::MaterialQuad");
}

struct RaceFlowUi::Impl {
 Texture wipe;
 std::array<Texture,FlowIntroTextures.size()> intro;
 std::array<Texture,FlowResultTextures.size()> results;unsigned loaded=0;bool failed=false;
 FlowDrawer drawer;
 Renderer* renderer=nullptr;
 static void submit(const FlowBatch& batch,void* context){
  auto& m=*static_cast<Impl*>(context);
  Texture* t=batch.texture>=0&&unsigned(batch.texture)<m.results.size()?&m.results[batch.texture]:nullptr;
  if(batch.texture>=0&&(!t||!t->ready))return;
  // The sampler wrap is set where the quads are drawn (the render thread in races).
  m.renderer->drawScreenMaterialQuads(t,reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,batch.color0,batch.color1,
                                     wrapMode(batch.wrapS),wrapMode(batch.wrapT));
 }
};

RaceFlowUi::RaceFlowUi():impl_(std::make_unique<Impl>()){}
RaceFlowUi::~RaceFlowUi()=default;

bool RaceFlowUi::load(){
 auto& m=*impl_;
 if(!m.wipe.ready){
  if(!m.wipe.load("textures/flow_wipe.t3x"))return false;
  C3D_TexSetWrap(&m.wipe.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE); // sampler 0x1c04: clamp, clamp
 }
 for(unsigned i=0;i<m.intro.size();++i)if(!m.intro[i].ready&&!m.intro[i].load(FlowIntroTextures[i]))return false;
 return true;
}

void RaceFlowUi::unload(){impl_->wipe.reset();for(auto& t:impl_->intro)t.reset();unloadResults();}

bool RaceFlowUi::loadResultsStep(){
 auto& m=*impl_;
 if(m.failed)return false;
 if(m.loaded<m.results.size()){
  if(!m.results[m.loaded].load(FlowResultTextures[m.loaded])){m.failed=true;return false;}
  ++m.loaded;
 }
 return m.loaded==m.results.size();
}
void RaceFlowUi::unloadResults(){
 auto& m=*impl_;for(auto& t:m.results)t.reset();
 m.loaded=0;m.failed=false;m.drawer.reset();
}
bool RaceFlowUi::resultsLoaded()const{return impl_->loaded==impl_->results.size();}
bool RaceFlowUi::resultsFailed()const{return impl_->failed;}
unsigned RaceFlowUi::resultsBytes()const{unsigned n=0;for(const auto& t:impl_->results)n+=t.bytes();return n;}

void RaceFlowUi::drawWipe(Renderer& renderer,float progress){
 auto& m=*impl_;
 if(progress<0||!m.wipe.ready)return;
 // AnimationGroup::PlayAnimationAtPercent: the frame is percent x the last frame.
 const float frame=std::clamp(progress,0.f,1.f)*float(FlowWipeTranslateT.size()-1);
 const unsigned i=std::min(unsigned(frame),unsigned(FlowWipeTranslateT.size()-2));
 const float t=FlowWipeTranslateT[i]+(FlowWipeTranslateT[i+1]-FlowWipeTranslateT[i])*(frame-float(i));
 // NW4R texture SRT about the centre: v' = scale (v - 0.5) + translate + 0.5.
 const float v0=(FlowWipeScaleT*-.5f+t+.5f)*FlowWipeTextureV,v1=(FlowWipeScaleT*.5f+t+.5f)*FlowWipeTextureV;
 // tex3ds stores rows bottom-up, so V runs opposite to the Wii's: unflipped,
 // the hold (translate 1) showed the scene and the wipe went black on either
 // side of it, a second time just after the respawn instead of over it.
 // The pane spans the whole 832x456 screen; colour register 0 is black.
 renderer.drawScreenSpriteRegion(m.wipe,0,0,display::Width,240,0,FlowWipeTextureV-v0,1,FlowWipeTextureV-v1,1.f,0x000000ffu);
}

void RaceFlowUi::drawIntro(Renderer& renderer,const char* mode,unsigned icon,const char* course,float alpha){
 auto& m=*impl_;
 if(alpha<=0||!m.intro[0].ready||!m.intro[1].ready)return;
 // Layout units (456 span the screen height, y up) to screen pixels, as the HUD.
 constexpr float Unit=240.f/456.f;
 auto sx=[](float x){return display::CenterX+x*Unit;};auto sy=[](float y){return 120.f-y*Unit;};
 const uint32_t a=uint32_t(std::lround(std::clamp(alpha,0.f,1.f)*255.f));
 std::array<FlowQuad,64> quads{};
 // One line of FlowIntroFont glyphs, its top-left corner at (x, top) or ending at x (right).
 auto line=[&](const char* text,float x,float top,bool right,uint32_t rgb){
  const auto& font=FlowIntroFont;
  const float scaleX=FlowIntroFontW/float(font.width),scaleY=FlowIntroFontH/float(font.height),cellH=float(font.cellH)*scaleY;
  auto glyph=[&](char c)->const FlowGlyph*{for(unsigned i=0;i<font.glyphCount;++i)if(font.glyphs[i].code==char16_t(uint8_t(c)))return &font.glyphs[i];return nullptr;};
  if(right){float w=0;for(const char* p=text;*p;++p)if(const auto* g=glyph(*p))w+=float(g->advance)*scaleX;x-=w;}
  unsigned n=0;
  for(const char* p=text;*p&&n<quads.size();++p){
   const auto* g=glyph(*p);if(!g)continue;
   if(g->w>0){
    auto& q=quads[n++];
    const float x0=sx(x+float(g->left)*scaleX),x1=sx(x+float(g->left+g->w)*scaleX),y0=sy(top),y1=sy(top-cellH);
    const float u0=float(g->x)/float(font.atlasW),u1=float(g->x+g->w)/float(font.atlasW);
    const float v0=float(g->y)/float(font.atlasH),v1=float(g->y+font.cellH)/float(font.atlasH);
    const float xs[4]={x0,x1,x0,x1},ys[4]={y0,y0,y1,y1},us[4]={u0,u1,u0,u1},vs[4]={v0,v0,v1,v1};
    for(unsigned k=0;k<4;++k){q.x[k]=xs[k];q.y[k]=ys[k];q.u[k]=us[k];q.v[k]=vs[k];q.color[k]=(rgb&0xffffff00u)|a;}
   }
   x+=float(g->advance)*scaleX;
  }
  if(n)renderer.drawScreenMaterialQuads(&m.intro[0],reinterpret_cast<const Renderer::MaterialQuad*>(quads.data()),n,0x00000000u,0xffffffffu);
 };
 // Placed from retail (VS, 16:9): the icon centre, the mode line's left edge
 // and middle, the course name's right edge and middle.
 constexpr float IconX=-328,IconY=173,ModeX=-297,ModeY=163,CourseX=370,CourseY=-190;
 const float half=FlowIntroFontH*.5f;
 if(icon<FlowIntroIcons.size()){
  const auto& r=FlowIntroIcons[icon];const float h=FlowIntroIconSize*.5f;
  FlowQuad q{};
  const float x0=sx(IconX-h),x1=sx(IconX+h),y0=sy(IconY+h),y1=sy(IconY-h);
  const float u0=float(r[0])/FlowIntroIconAtlas[0],u1=float(r[0]+r[2])/FlowIntroIconAtlas[0],v0=float(r[1])/FlowIntroIconAtlas[1],v1=float(r[1]+r[3])/FlowIntroIconAtlas[1];
  const float xs[4]={x0,x1,x0,x1},ys[4]={y0,y0,y1,y1},us[4]={u0,u1,u0,u1},vs[4]={v0,v0,v1,v1};
  for(unsigned k=0;k<4;++k){q.x[k]=xs[k];q.y[k]=ys[k];q.u[k]=us[k];q.v[k]=vs[k];q.color[k]=0xffffff00u|a;}
  renderer.drawScreenMaterialQuads(&m.intro[1],reinterpret_cast<const Renderer::MaterialQuad*>(&q),1,0x00000000u,0xffffffffu);
 }
 if(mode&&*mode)line(mode,ModeX,ModeY+half,false,FlowIntroCupColor);
 // course_name.brlyt: a black "shadow" copy 2 units below the text.
 if(course&&*course){line(course,CourseX,CourseY+half-2,true,0x000000ffu);line(course,CourseX,CourseY+half,true,FlowIntroCourseColor);}
}

void RaceFlowUi::step(const RaceResultsFlow& flow){impl_->drawer.step(flow);}

void RaceFlowUi::setAwardLayout(bool award){impl_->drawer.award=award;}
void RaceFlowUi::drawResults(Renderer& renderer,const RaceResultsFlow& flow){
 auto& m=*impl_;
 if(!resultsLoaded())return;
 m.renderer=&renderer;
 m.drawer.draw(flow,&Impl::submit,&m);
}
}
