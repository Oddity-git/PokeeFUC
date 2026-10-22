#include "game/hub_scaling.hpp"

#include "game/damage_numbers.hpp"
#include "game/guest_layout.hpp"
#include "psprecomp/guest_memory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace mhfu::game::hub_scaling {
namespace {

using layout::kExtent;
using layout::kGameSysPointer;
using layout::kGameSysStage;
using layout::kHealth;
using layout::kMostHealth;

struct Original {
    int most{};
    int health{};
    int scaled_most{};
};

struct State {
    bool was_quest{};
    bool active{};
    std::uint16_t last_stage{};
    std::map<std::uint32_t, Original> scaled; // by entity
};

State &state() {
    static State s;
    return s;
}

bool trace_enabled() {
    static const bool on = [] {
        const char *text = std::getenv("MHFU_TRACE_HUB_SCALING");
        return text != nullptr && *text != '\0' && std::string(text) != "0";
    }();
    return on;
}

std::optional<std::uint16_t> loaded_stage(const psprecomp::GuestMemory &memory) {
    if (!memory.contains(kGameSysPointer, 4u)) return std::nullopt;
    const std::uint32_t sys = memory.load32(kGameSysPointer);
    if (!layout::in_ram(sys) || !memory.contains(sys + kGameSysStage, 2u)) return std::nullopt;
    return memory.load16(sys + kGameSysStage);
}

int scale(int value, int from, int to) {
    if (from <= 0) return value;
    return static_cast<int>(std::lround(static_cast<double>(value) * to / from));
}

int clamp(std::int64_t value, int low, int high) {
    return static_cast<int>(std::clamp<std::int64_t>(value, low, high));
}

void restore(psprecomp::GuestMemory &memory, State &s) {
    int restored = 0;
    for (const auto &[entity, original] : s.scaled) {
        if (!memory.contains(entity + kMostHealth, 2u) || !memory.contains(entity + kHealth, 2u)) continue;
        if (static_cast<std::int16_t>(memory.load16(entity + kMostHealth)) != original.scaled_most) continue;
        const int health = static_cast<std::int16_t>(memory.load16(entity + kHealth));
        const int most = clamp(original.most, 1, 0x7FFF);
        const int back = clamp(scale(health, original.scaled_most, original.most), 0, most);
        memory.store16(entity + kMostHealth, static_cast<std::uint16_t>(most));
        memory.store16(entity + kHealth, static_cast<std::uint16_t>(back));
        ++restored;
    }
    s.scaled.clear();
    if (trace_enabled() && restored > 0) std::cout << "[hub] off: " << restored << " monsters put back\n";
}

void apply(psprecomp::GuestMemory &memory, State &s, int percent, bool trace) {
    const int factor = std::clamp(percent, 1, 100);
    for (const HealthSeen &seen : quest_health(memory)) {
        if (seen.most < kMinLargeHealth) continue;
        const std::uint32_t entity = seen.object;
        auto found = s.scaled.find(entity);
        if (found != s.scaled.end() && found->second.scaled_most == seen.most) continue;
        const int scaled_most = clamp(scale(seen.most, 100, factor), 1, 0x7FFF);
        const int scaled_health = clamp(scale(seen.health, seen.most, scaled_most), 0, scaled_most);
        s.scaled[entity] = Original{seen.most, seen.health, scaled_most};
        memory.store16(entity + kMostHealth, static_cast<std::uint16_t>(scaled_most));
        memory.store16(entity + kHealth, static_cast<std::uint16_t>(scaled_health));
        if (trace)
            std::cout << "[hub] scale " << std::hex << entity << std::dec << ": " << seen.health << "/" << seen.most
                      << " -> " << scaled_health << "/" << scaled_most << "\n";
    }
}

} // namespace

int health_percent() noexcept {
    static const int percent = [] {
        const char *text = std::getenv("MHFU_HUB_SCALING_HEALTH");
        if (text == nullptr || *text == '\0') return kDefaultHealthPercent;
        char *end = nullptr;
        const long value = std::strtol(text, &end, 10);
        if (end == text || *end != '\0' || value < 1 || value > 100) return kDefaultHealthPercent;
        return static_cast<int>(value);
    }();
    return percent;
}

void frame(psprecomp::GuestMemory &memory, bool enabled, bool offline) {
    State &s = state();
    const bool quest = layout::quest_running(memory);
    const std::optional<std::uint16_t> stage = loaded_stage(memory);

    if (!quest) {
        if (s.was_quest) {
            restore(memory, s);
            s.active = false;
            s.was_quest = false;
        }
        if (stage) s.last_stage = *stage;
        return;
    }

    if (!s.was_quest) {
        s.was_quest = true;
        s.active = enabled && offline && s.last_stage == kGatheringHallStage;
        if (trace_enabled())
            std::cout << "[hub] quest start: last stage " << s.last_stage << ", offline " << (offline ? 1 : 0)
                      << ", scaling " << (s.active ? 1 : 0) << "\n";
    }

    if (!enabled || !offline) {
        if (s.active) restore(memory, s);
        s.active = false;
        return;
    }
    if (s.active) apply(memory, s, health_percent(), trace_enabled());
}

void reset() {
    state() = State{};
}

} // namespace mhfu::game::hub_scaling
