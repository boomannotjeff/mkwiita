#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {

// Team battle rules (Balloon Battle and Coin Runners), presentation-free.
// Two teams of up to six (0 red, 1 blue), shuffled at the start of each set.
//
// Balloon Battle: three balloons each (five at most). Popping an enemy's
// balloon scores a point for the attacker; losing the last one costs a point
// (never below zero) and brings three new ones. Falling off loses a balloon
// without scoring for anyone. A dash (Mushroom, Star, Mega) into an enemy
// steals a balloon. After losing a balloon a racer is safe for a moment.
//
// Coin Runners: coins picked up score; a hit drops some of them.
class BalloonBattle {
public:
    static constexpr unsigned MaxRacers = 12;
    static constexpr unsigned StartingBalloons = 3;
    static constexpr unsigned MaxBalloons = 5; // fn_1_359D28: `count < 5`
    static constexpr unsigned DurationTicks = 180 * 60;
    static constexpr unsigned SafeTicks = 90;      // after losing a balloon
    static constexpr unsigned PopupTicks = 60;     // "+1" / "-1" shown this long
    static constexpr unsigned CoinDrop = 3;        // coins lost per hit (Coin Runners)

    enum class Mode : uint8_t { Balloon, Coin };

    void reset(unsigned racers, Mode mode = Mode::Balloon) {
        count_ = std::clamp(racers, 1u, MaxRacers);
        mode_ = mode;
        balloons_.fill(0);
        scores_.fill(0);
        safe_.fill(0);
        for (unsigned i = 0; i < count_; ++i)
            balloons_[i] = mode == Mode::Balloon ? StartingBalloons : 0;
        hits_ = 0;
        popup_ = 0;
        popupTicks_ = 0;
    }

    // One 60 Hz tick while the battle runs.
    void step() {
        for (unsigned i = 0; i < count_; ++i)
            if (safe_[i]) --safe_[i];
        if (popupTicks_) --popupTicks_;
    }

    static unsigned team(unsigned racer) { return racer < MaxRacers ? teams_[racer] : racer % 2; }
    // Six red and six blue in a random order (Fisher-Yates); call once per set.
    static void shuffleTeams(uint32_t seed) {
        for (unsigned i = 0; i < MaxRacers; ++i) teams_[i] = uint8_t(i % 2);
        for (unsigned i = MaxRacers - 1; i > 0; --i) {
            seed = seed * 1664525u + 1013904223u;
            std::swap(teams_[i], teams_[(seed >> 8) % (i + 1)]);
        }
    }
    bool enemies(unsigned a, unsigned b) const { return a < count_ && b < count_ && team(a) != team(b); }

    // An item or object hit `victim`; attacker is the item's owner (or out of range).
    // Returns true when the hit counted (a balloon popped or coins dropped).
    bool hit(unsigned victim, unsigned attacker) {
        if (victim >= count_ || safe_[victim]) return false;
        // No friendly fire: a teammate's item costs nothing (your own still does).
        if (attacker < count_ && attacker != victim && !enemies(victim, attacker)) return false;
        const bool scored = attacker < count_ && enemies(victim, attacker);
        if (mode_ == Mode::Coin) {
            const unsigned lost = std::min<unsigned>(scores_[victim], CoinDrop);
            addScore(victim, -int(lost));
            safe_[victim] = SafeTicks;
            ++hits_;
            return lost > 0;
        }
        if (!balloons_[victim]) return false;
        --balloons_[victim];
        if (scored) addScore(attacker, 1);
        if (!balloons_[victim]) {
            addScore(victim, -1);
            balloons_[victim] = StartingBalloons;
        }
        safe_[victim] = SafeTicks;
        ++hits_;
        return true;
    }

    // Fell off the arena: a balloon (or coins) lost, nobody scores.
    bool fall(unsigned racer) { return hit(racer, MaxRacers); }

    // A dash into an enemy takes one of their balloons.
    bool steal(unsigned thief, unsigned victim) {
        // Only a racer that still has balloons can steal (fn_1_359D28).
        if (mode_ != Mode::Balloon || !enemies(thief, victim) || safe_[victim] || !balloons_[victim] || !balloons_[thief]) return false;
        if (!hit(victim, thief)) return false;
        if (balloons_[thief] < MaxBalloons) ++balloons_[thief];
        return true;
    }

    // Coin Runners: a coin picked up.
    void coin(unsigned racer) {
        if (mode_ == Mode::Coin && racer < count_) addScore(racer, 1);
    }

    Mode mode() const { return mode_; }
    unsigned racers() const { return count_; }
    unsigned balloons(unsigned racer) const { return racer < count_ ? balloons_[racer] : 0; }
    unsigned score(unsigned racer) const { return racer < count_ ? scores_[racer] : 0; }
    bool safe(unsigned racer) const { return racer < count_ && safe_[racer]; }
    unsigned hits() const { return hits_; }

    unsigned teamScore(unsigned t) const {
        unsigned total = 0;
        for (unsigned i = 0; i < count_; ++i)
            if (team(i) == t) total += scores_[i];
        return total;
    }
    // 0 red ahead, 1 blue ahead, 2 tied.
    unsigned leader() const {
        const unsigned red = teamScore(0), blue = teamScore(1);
        return red == blue ? 2 : red > blue ? 0 : 1;
    }

    // The player's last score change and how long it stays on screen.
    int popup() const { return popupTicks_ ? popup_ : 0; }
    unsigned popupAge() const { return PopupTicks - popupTicks_; }

    // Team first (the winning team on top), then points, then balloons.
    unsigned rank(unsigned racer) const {
        if (racer >= count_)
            return count_;
        unsigned result = 1;
        const unsigned lead = leader();
        auto key = [&](unsigned i) {
            const unsigned teamOrder = lead == 2 ? team(i) : (team(i) == lead ? 0 : 1);
            return std::array<int, 4>{int(teamOrder), -int(scores_[i]), -int(balloons_[i]), int(i)};
        };
        for (unsigned i = 0; i < count_; ++i)
            if (i != racer && key(i) < key(racer))
                ++result;
        return result;
    }

private:
    void addScore(unsigned racer, int delta) {
        if (racer >= count_ || !delta) return;
        const int next = std::clamp(int(scores_[racer]) + delta, 0, 999);
        const int change = next - int(scores_[racer]);
        scores_[racer] = uint16_t(next);
        if (racer == 0 && change) { popup_ = change; popupTicks_ = PopupTicks; }
    }

    static inline std::array<uint8_t, MaxRacers> teams_{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
    std::array<uint8_t, MaxRacers> balloons_{};
    std::array<uint16_t, MaxRacers> scores_{};
    std::array<uint16_t, MaxRacers> safe_{};
    unsigned count_ = 0;
    unsigned hits_ = 0;
    Mode mode_ = Mode::Balloon;
    int popup_ = 0;
    unsigned popupTicks_ = 0;
};

} // namespace mkw
