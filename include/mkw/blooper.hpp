#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// USA StaticR REL: GessoManager 0x2c9eb4/0x2ca03c; start CHR0 is70
// frames, then30 pause, three frames per player order, attack contact at148.
inline constexpr std::array<uint8_t,12> BlooperPlayerOrder{3,1,4,11,5,9,2,6,10,8,7,0};
inline constexpr unsigned BlooperInkDuration=450; // applyInk 0x6f1c0, .data+0x2dd4
inline constexpr unsigned BlooperSelectionDelay=100;
// The ink's last frames fade out (HudInkFrames); boosts and Mushrooms wash it into them.
inline constexpr unsigned BlooperInkFade=95;
inline constexpr unsigned BlooperBoostWash=3; // ink frames spent per frame while boosting
// The screen's attack animation (SE_ITM_GESO_ATK) starts this long before the ink lands.
inline constexpr unsigned BlooperAttackLead=148;
inline constexpr unsigned BlooperManagerDuration=100+3*11+176;
inline constexpr unsigned blooperHitDelay(unsigned player){return 70+30+BlooperAttackLead+3*BlooperPlayerOrder[player%12];}
}
