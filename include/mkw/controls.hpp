#pragma once
#include <array>
#include <cstdint>

namespace mkw {
// Race controls: which Vita button does each action. START (pause), the
// D-pad (tricks) and the stick (steering, item aim) are fixed.
enum class ControlAction : uint8_t {Accelerate,Brake,LookBehind,Drift,Item,Trick,Speedometer,Count};
enum class ControlButton : uint8_t {Cross,Circle,Square,Triangle,L,R,Select,Count};
using ControlBindings=std::array<ControlButton,size_t(ControlAction::Count)>;
inline constexpr unsigned ControlActionCount=unsigned(ControlAction::Count),ControlButtonCount=unsigned(ControlButton::Count);
inline constexpr const char* ControlActionNames[ControlActionCount]={"Accelerate","Brake","Look Behind","Drift","Use Item","Trick / Wheelie","Speedometer"};
inline constexpr const char* ControlButtonNames[ControlButtonCount]={"Cross","Circle","Square","Triangle","L","R","SELECT"};
// Default: Circle brakes and Square looks behind.
inline constexpr ControlBindings DefaultControls{{ControlButton::Cross,ControlButton::Circle,ControlButton::Square,ControlButton::R,ControlButton::L,ControlButton::Triangle,ControlButton::Select}};
// Classic: 1.4.0's layout (Square brakes, Circle looks behind).
inline constexpr ControlBindings ClassicControls{{ControlButton::Cross,ControlButton::Square,ControlButton::Circle,ControlButton::R,ControlButton::L,ControlButton::Triangle,ControlButton::Select}};
struct ControlSettings {
    ControlBindings bindings=DefaultControls; // quality.txt control_<action>=<button>
    const ControlBindings& active()const{return bindings;}
    // The action takes the button; the action that had it gets this action's old one, so none is lost.
    void bind(ControlAction action,ControlButton button){
        for(auto& b:bindings)if(b==button)b=bindings[size_t(action)];
        bindings[size_t(action)]=button;
    }
};
inline ControlSettings& controls(){static ControlSettings value;return value;}
}
