#include "platform/android_performance.hpp"

#include <dlfcn.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace mhfu::android {
namespace {

// The time a game frame's work should take: three quarters of the PSP's
// 29.97 frames a second, so the system raises the clock while a frame still
// has room rather than once it is late.
constexpr std::int64_t kTargetWorkNs = 25'000'000;

// Resolved at run time: the functions exist from API 33, and the app runs on
// Android 10 and later.
struct Hints {
    using GetManager = void *(*)();
    using CreateSession = void *(*)(void *, const std::int32_t *, std::size_t, std::int64_t);
    using ReportActual = int (*)(void *, std::int64_t);
    void *session{};
    ReportActual report{};
    bool opened{};
};

Hints &hints() {
    static Hints value;
    return value;
}

// Android's scheduler may leave this mostly-sleeping game thread on an
// efficiency core. Pin it to the performance cores when their capacities are
// readable; if the cpuset does not allow it, leave the scheduler alone.
void pin_to_performance_cores() {
    if (std::getenv("MHFU_NO_BIG_CORES") != nullptr) return;
    struct Cpu {
        int id;
        long capacity;
    };
    std::vector<Cpu> cpus;
    for (int id = 0; id < 64; ++id) {
        std::ifstream in("/sys/devices/system/cpu/cpu" + std::to_string(id) + "/cpu_capacity");
        long capacity = 0;
        if (in >> capacity && capacity > 0) cpus.push_back({id, capacity});
    }
    if (cpus.size() < 2u) return;
    long fastest = 0;
    for (const Cpu &cpu : cpus) fastest = std::max(fastest, cpu.capacity);
    cpu_set_t set;
    CPU_ZERO(&set);
    std::string ids;
    for (const Cpu &cpu : cpus)
        if (cpu.capacity * 5 >= fastest * 3) {
            CPU_SET(cpu.id, &set);
            ids += (ids.empty() ? "" : ",") + std::to_string(cpu.id);
        }
    if (CPU_COUNT(&set) == 0 || CPU_COUNT(&set) == static_cast<int>(cpus.size())) return;
    if (sched_setaffinity(0, sizeof(set), &set) == 0)
        std::cout << "[perf] game thread pinned to performance CPUs " << ids << "\n";
    else
        std::cout << "[perf] cannot pin the game thread to CPUs " << ids << ": errno " << errno << "\n";
}

void open(Hints &hints) {
    hints.opened = true;
    pin_to_performance_cores();
    if (std::getenv("MHFU_NO_PERFORMANCE_HINT") != nullptr) {
        std::cout << "[perf] performance hints off (MHFU_NO_PERFORMANCE_HINT)\n";
        return;
    }
    void *library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) return;
    const auto get_manager = reinterpret_cast<Hints::GetManager>(dlsym(library, "APerformanceHint_getManager"));
    const auto create_session =
        reinterpret_cast<Hints::CreateSession>(dlsym(library, "APerformanceHint_createSession"));
    hints.report = reinterpret_cast<Hints::ReportActual>(dlsym(library, "APerformanceHint_reportActualWorkDuration"));
    if (get_manager == nullptr || create_session == nullptr || hints.report == nullptr) {
        std::cout << "[perf] no performance hints on this Android version\n";
        return;
    }
    void *manager = get_manager();
    const std::int32_t thread = static_cast<std::int32_t>(gettid());
    hints.session = manager != nullptr ? create_session(manager, &thread, 1u, kTargetWorkNs) : nullptr;
    std::cout << "[perf] performance hints " << (hints.session != nullptr ? "on" : "unavailable")
              << " for the game's thread, target " << kTargetWorkNs / 1'000'000 << " ms of work a frame\n";
}

} // namespace

void report_frame_work(std::int64_t work_ns) {
    Hints &state = hints();
    if (!state.opened) open(state);
    if (state.session == nullptr || work_ns <= 0) return;
    state.report(state.session, work_ns);
}

namespace {

std::string read_text(const std::string &path) {
    std::ifstream in(path);
    std::string text;
    std::getline(in, text);
    return text;
}

long read_number(const std::string &path) {
    std::ifstream in(path);
    long value = 0;
    if (!(in >> value)) return std::numeric_limits<long>::min();
    return value;
}

} // namespace

std::string state_line() {
    std::ostringstream out;
    out << "core " << sched_getcpu();
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        out << " affinity";
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &set)) out << ' ' << cpu;
    }
    errno = 0;
    const int nice = getpriority(PRIO_PROCESS, 0);
    if (errno == 0) out << " nice " << nice;
    bool any = false;
    for (int policy = 0; policy < 16; ++policy) {
        const long khz =
            read_number("/sys/devices/system/cpu/cpufreq/policy" + std::to_string(policy) + "/scaling_cur_freq");
        if (khz == std::numeric_limits<long>::min() || khz <= 0) continue;
        out << (any ? ' ' : ' ') << 'p' << policy << '=' << khz / 1000;
        any = true;
    }
    if (!any)
        for (int cpu = 0; cpu < 16; ++cpu) {
            const long khz =
                read_number("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq");
            if (khz == std::numeric_limits<long>::min() || khz <= 0) continue;
            out << (any ? ' ' : ' ') << 'c' << cpu << '=' << khz / 1000;
            any = true;
        }
    if (any) out << "MHz";
    bool temp = false;
    for (int zone = 0; zone < 24; ++zone) {
        const std::string base = "/sys/class/thermal/thermal_zone" + std::to_string(zone);
        const long millidegrees = read_number(base + "/temp");
        if (millidegrees == std::numeric_limits<long>::min()) continue;
        std::string type = read_text(base + "/type");
        if (type.empty()) type = "zone" + std::to_string(zone);
        out << (temp ? ' ' : ' ') << type << '=' << millidegrees / 1000;
        temp = true;
    }
    const std::string status = read_text("/sys/class/power_supply/battery/status");
    const long capacity = read_number("/sys/class/power_supply/battery/capacity");
    const long battery_temp = read_number("/sys/class/power_supply/battery/temp");
    if (!status.empty()) out << " battery " << status;
    if (capacity != std::numeric_limits<long>::min()) out << ' ' << capacity << '%';
    if (battery_temp != std::numeric_limits<long>::min()) out << ' ' << battery_temp / 10 << "C";
    return out.str();
}

} // namespace mhfu::android
