#pragma once
#include <mkw/animation.hpp>
#include <mkw/renderer.hpp>
#include <memory>
namespace mkw {
class VehicleAnimation;
// Rosalina's Luma, drawn where VehicleAnimation's LumaMotion puts it (ported from
// MKW-3DS v0.6.0 src/luma_visual.cpp). One per racer: the pose is per instance,
// the textures are shared through Model's cache.
class LumaVisual {
public:
    bool load();
    // Poses the wait clip for this frame; nothing to draw unless the racer is Rosalina.
    void prepare(const ViewFrustum& view,const VehicleAnimation& vehicle,unsigned tick);
    void draw(Renderer& renderer) const;
    // CPU-skinned poses share one scratch buffer: prepare those on the main thread.
    bool gpuSkinned() const {return animation_&&animation_->gpuSkinned();}
private:
    std::unique_ptr<Model> model_;
    std::unique_ptr<DriverAnimation> animation_;
    C3D_Mtx matrix_{};
    int wait_=-1;
    bool visible_=false;
};
}
