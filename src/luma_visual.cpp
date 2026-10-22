#include <mkw/luma_visual.hpp>
#include <mkw/vehicle_animation.hpp>
#include <mkw/asset_pack.hpp>
#include <cmath>
namespace mkw {
bool LumaVisual::load(){
    // TicoModel (USA 807cad0c): common tico.brres, model tico, looping wait (tools/cook_battle.py).
    // The race copy (tools/cook_luma.py); game files without it fall back to the battle one.
    const bool race=pack::contains("models/luma.gmdl");
    auto model=std::make_unique<Model>();
    if(!model->load(race?"models/luma.gmdl":"models/rosalina_luma.gmdl"))return false;
    auto animation=std::make_unique<DriverAnimation>();
    if(!animation->load(race?"models/luma.dskm":"models/rosalina_luma.dskm",*model))return false;
    const int wait=animation->find("wait");
    if(wait<0)return false;
    animation->prepareLitSkinning(*model);
    model_=std::move(model);animation_=std::move(animation);wait_=wait;visible_=false;
    return true;
}
void LumaVisual::prepare(const ViewFrustum& view,const VehicleAnimation& vehicle,unsigned tick){
    visible_=false;if(!model_||!animation_||!vehicle.hasLuma())return;
    const auto& motion=vehicle.lumaMotion();const auto scale=vehicle.visualScale();
    const auto x=motion.right()*scale.x,y=motion.up()*scale.y,z=motion.forward()*scale.z,p=motion.position();
    Mtx_Identity(&matrix_);
    matrix_.r[0].x=x.x;matrix_.r[0].y=y.x;matrix_.r[0].z=z.x;matrix_.r[0].w=p.x;
    matrix_.r[1].x=x.y;matrix_.r[1].y=y.y;matrix_.r[1].z=z.y;matrix_.r[1].w=p.y;
    matrix_.r[2].x=x.z;matrix_.r[2].y=y.z;matrix_.r[2].z=z.z;matrix_.r[2].w=p.z;
    const float duration=animation_->duration(unsigned(wait_));
    const float frame=duration>0?std::fmod(float(vehicle.lumaFrame()),duration):0.f;
    const auto frustum=view.model(matrix_);
    visible_=animation_->apply(*model_,wait_,frame,tick,-1,0,&frustum);
}
void LumaVisual::draw(Renderer& renderer)const{
    if(!visible_)return;
    if(animation_->gpuSkinned())renderer.setSkinPalette(animation_->palette(),animation_->matrixCount(),21);
    renderer.draw(*model_,matrix_);
    renderer.setSkinPalette(nullptr,0,12);
}
}
