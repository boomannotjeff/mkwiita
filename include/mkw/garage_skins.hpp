#pragma once
#include <mkw/garage.hpp>
namespace mkw {
// Retro Rewind character skins (tools/cook_rr_skins.py): driver (roster index), skin (1+), vehicle slot.
// The retail game has none; the Retro Rewind edition's cook rewrites this file.
struct GarageSkin {unsigned driver,skin,slot;GarageChoice choice;};
inline constexpr std::array<GarageSkin,0> GarageSkins{};
inline constexpr std::array<unsigned,24> GarageSkinCounts{};
// Bit s: skin s cooked (a skin some vehicle of which did not convert is left out).
inline constexpr std::array<unsigned,24> GarageSkinMask{};
inline const GarageChoice* garageSkin(unsigned driver,unsigned skin,unsigned slot){
    for(const auto& s:GarageSkins)if(s.driver==driver&&s.skin==skin&&s.slot==slot)return &s.choice;
    return nullptr;
}
}
