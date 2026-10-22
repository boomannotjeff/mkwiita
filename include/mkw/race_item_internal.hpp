#pragma once
// Helpers shared by the race item sources (race_items.cpp, item_world.cpp, item_native.cpp).
#include "mkw/race_items.hpp"
#include "mkw/held_items.hpp"
#include "mkw/blooper.hpp"
#include <algorithm>
#include <cmath>

namespace mkw::items {
inline constexpr float Pi=3.14159265358979323846f;
inline void tick(uint16_t& timer){if(timer)--timer;}
// A Mushroom washes ink into its fade instead of popping it off, and cancels a pending one.
inline void washInk(ItemHud& h){h.ink=std::min<uint16_t>(h.ink,BlooperInkFade);h.inkDelay=0;}
inline bool shell(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::BlueShell;}
// Bob-ombs trail behind the kart like Bananas until the button is released.
inline bool singleHeld(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::Banana||kind==ItemKind::FakeBox||kind==ItemKind::Bobomb;}
inline bool trailing(ItemKind kind){return singleHeld(kind)||kind==ItemKind::TripleBanana;}
// Objects whose OnKill is a blast (Bob-omb 0x8079cf60, Blue Shell 0x807a376c).
inline bool blastItem(ItemKind kind){return kind==ItemKind::Bobomb||kind==ItemKind::BlueShell;}
inline bool trapItem(ItemKind kind){return kind==ItemKind::Banana||kind==ItemKind::FakeBox||kind==ItemKind::Bobomb;}
inline bool triple(ItemKind kind){return kind==ItemKind::TripleMushroom||kind==ItemKind::TripleBanana||kind==ItemKind::TripleGreenShell||kind==ItemKind::TripleRedShell;}
/// The object a triple (or single) inventory item throws.
inline ItemKind baseKind(ItemKind kind){
    return kind==ItemKind::TripleBanana?ItemKind::Banana:kind==ItemKind::TripleGreenShell?ItemKind::GreenShell:
        kind==ItemKind::TripleRedShell?ItemKind::RedShell:kind==ItemKind::TripleMushroom?ItemKind::Mushroom:kind;
}
inline Vec3 itemMotion(const ItemRacer& r){return r.haveVelocity?r.velocity:normalized(r.forward,{0,0,1})*r.speed;}
// Held items follow the kart without its wheelie pitch: forward levelled onto levelUp.
inline void heldFrame(const ItemRacer& r,Vec3& forward,Vec3& up){
    up=normalized(length(r.levelUp)>.1f?r.levelUp:r.up,{0,1,0});
    forward=normalized(r.forward-up*dot(r.forward,up),normalized(r.forward,{0,0,1}));
}
inline TrailingItemPose trailingPose(const ItemRacer& r,float terrainRadius=40){
    Vec3 forward,up;heldFrame(r,forward,up);
    return {r.position+forward*(r.itemGeometry[3]*r.scale.z),forward,up,r.scale,
        r.itemGeometry[0]*(r.bike?.8f:.9f)+40,r.itemGeometry[2]*r.scale.y+terrainRadius,
        r.speed,r.speedRatio,r.hop,dot(r.haveVelocity?r.velocity:r.forward*r.speed,r.forward)>0,!r.grounded};
}
// RMCU object IDs: green=0, red=1, banana=2, mushroom=3, star=4,
// blue=5, lightning=6, fake=7, cloud=8, bomb=9. Inventory IDs differ.
inline unsigned retailObjectId(ItemKind kind){
    switch(kind){
    case ItemKind::GreenShell:case ItemKind::TripleGreenShell:return 0;
    case ItemKind::RedShell:case ItemKind::TripleRedShell:return 1;
    case ItemKind::Banana:case ItemKind::TripleBanana:return 2;
    case ItemKind::Mushroom:case ItemKind::TripleMushroom:case ItemKind::GoldenMushroom:return 3;
    case ItemKind::Star:return 4;
    case ItemKind::BlueShell:return 5;
    case ItemKind::Lightning:return 6;
    case ItemKind::FakeBox:return 7;
    case ItemKind::ThunderCloud:return 8;
    case ItemKind::Bobomb:return 9;
    default:return 14;
    }
}
}
