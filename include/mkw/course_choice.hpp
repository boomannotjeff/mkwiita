#pragma once
#include <Common.hh>
namespace mkw {
#ifdef MKW_RR
inline constexpr bool RetroRewindEdition=true;
#else
inline constexpr bool RetroRewindEdition=false;
#endif
// One menu course. id is the retail course Kinoko runs it as; key numbers its
// cooked files (models/course_NN*): the course ID for retail courses, 100+
// for Retro Rewind tracks (tools/course_catalog.py). music: the retail course
// whose music plays when the track has none of its own (Pulsar's music slot).
struct CourseChoice {const char* name; Kinoko::Course id; const char* archive; const char* models; const char* sky; const char* cup; bool supported; bool cpuCompletionVerified; unsigned key=~0u,music=~0u;
    constexpr unsigned fileKey() const {return key==~0u?unsigned(id):key;}
    constexpr unsigned musicKey() const {return music==~0u?unsigned(id):music;}};
} // namespace mkw
