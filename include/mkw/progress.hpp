#pragma once
#include <array>
#include <cstdint>
#include <mkw/course_catalog.hpp>
namespace mkw {
// Cups of four races (8 retail; the Retro Rewind edition's Retro and Custom cups).
inline constexpr unsigned RaceCupCount=RaceCourseCount/4;
// Licenses and what each one has unlocked (the retail unlock conditions, as
// IGN's Mario Kart Wii unlockables list gives them). Unlocks are not stored:
// they follow from the results kept here, so they can't drift from them.
// ux0:data/mkw/save.bin holds the four licenses and the Settings switch that
// unlocks everything for every license.
struct License {
    uint8_t used=0;
    char name[11]{};                         // up to 10 characters (a Mii name's length)
    // Grand Prix by engine class (50cc, 100cc, 150cc, Mirror) and cup: the best
    // trophy (0 none, 1 bronze, 2 silver, 3 gold) and rank in stars (0..3).
    std::array<std::array<uint8_t,RaceCupCount>,4> trophy{},stars{};
    std::array<uint32_t,RaceCourseCount> bestTimes{}; // Time Trial milliseconds per course, 0 none
    uint32_t timeTrialCourses=0;             // the first 32 courses finished in Time Trials, one bit each
};
struct Progress {
    static constexpr unsigned Licenses=4;
#ifdef MKW_RR
    bool unlockAll=true;                     // Retro Rewind: everything is unlocked
#else
    bool unlockAll=false;
#endif
    std::array<License,Licenses> license{};
    unsigned current=0;                      // the license chosen on the License page
    License& me(){return license[current<Licenses?current:0];}
    const License& me()const{return license[current<Licenses?current:0];}
    bool load();
    bool save()const;
    // A new license in slot: named, empty. The first license ever made takes
    // over the best times and ghosts saved before licenses existed.
    void create(unsigned slot,const char* name);
    void erase(unsigned slot);
    // Courses whose expert staff ghost the license has unlocked (a Time Trial at
    // or under StaffGhosts[course].unlock).
    unsigned expertGhosts()const;
    // What the current license may choose. Drivers are UiCharacters indices,
    // vehicles a weight class (0 small, 1 medium, 2 large) and its machine choice.
    bool character(unsigned driver)const;
    bool vehicle(unsigned weight,unsigned choice)const;
    bool cup(unsigned engineClass,unsigned cup)const;
    bool anyClassCup(unsigned cup)const;     // Time Trials: a cup open in any class
    bool mirror()const;
    bool mixedTypes(unsigned engineClass)const; // 50cc bikes, 100cc karts
    // Everything a license can unlock, one bit each (Settings' Unlock everything
    // aside): compared before and after a result to announce what it opened.
    static constexpr unsigned Unlockables=51;
    uint64_t unlockables()const;
    // An unlockable's menu label and, for cups and the karts/bikes switch, its class.
    static const char* unlockableName(unsigned i);
    static const char* unlockableClass(unsigned i);
    // Results. place is 1-based; points the Grand Prix total (60 at most).
    void grandPrix(unsigned engineClass,unsigned cup,unsigned place,unsigned points);
    // A finished Time Trial; true when it is the course's new best.
    bool timeTrial(unsigned course,uint32_t milliseconds);
};
Progress& progress();
}
