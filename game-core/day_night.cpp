#include "day_night.h"


#include <algorithm>
#include <cmath>

namespace {

constexpr int kQuarter = DayNight::kDayLength / 4;
constexpr float kMaxDark = 128.0f;  // Screen.MAXDARK


}  // namespace

const char* timeName(DayNight::Time time) {
    switch (time) {
        case DayNight::Time::Morning: return "Morning";
        case DayNight::Time::Day: return "Day";
        case DayNight::Time::Evening: return "Evening";
        case DayNight::Time::Night: return "Night";
    }
    return "?";
}

void DayNight::step() {
    if (++tick_ >= kDayLength) {  // back to morning
        tick_ = 0;
        pastDay1_ = true;
    }
}

DayNight::Time DayNight::time() const { return static_cast<Time>(std::min(tick_ / kQuarter, 3)); }

void DayNight::setTime(Time time) {
    tick_ = static_cast<int>(time) * kQuarter;
    pastDay1_ = true;  // like Updater.changeTimeOfDay
}

void DayNight::restore(int tick, bool pastDay1) {
    tick_ = tick;
    pastDay1_ = pastDay1;
}

float DayNight::darkness() const {
    // Screen.overlay: fade in through the evening, full at night, fade out through the morning (except day 1).
    const float progress = static_cast<float>(tick_ % kQuarter) / static_cast<float>(kQuarter);
    float darkFactor = 0.0f;
    switch (time()) {
        case Time::Morning: darkFactor = pastDay1_ ? (1.0f - progress) * kMaxDark : 0.0f; break;
        case Time::Day: darkFactor = 0.0f; break;
        case Time::Evening: darkFactor = progress * kMaxDark; break;
        case Time::Night: darkFactor = kMaxDark; break;
    }
    return darkFactor / 160.0f;
}
