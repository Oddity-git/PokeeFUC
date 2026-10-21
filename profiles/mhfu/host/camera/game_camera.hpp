#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "title.hpp"

namespace psprecomp {
class Runtime;
struct AllegrexContext;
class GuestMemory;
}

// Drives the game's own camera from camera::take() (camera_input.hpp).
//
// USA Freedom Unite, and FUComplete at its own addresses (kCameraUpdate and
// the rest below). The port wraps the camera update and, before that update
// builds the view, sets the yaw and the preset offset it is about to read. The
// game then runs its own smoothing, collision and recentre. A camera update
// that is already placing the view itself is left alone.
namespace mhfu::camera {

using RotationFunction = void (*)(psprecomp::Runtime &, psprecomp::AllegrexContext &);

// Where the code the driver hooks is, in this build's executable (title.hpp).
// FUComplete's Portable 2nd G has the same functions as Freedom Unite at other
// addresses; game_camera.cpp lists the instructions each one is checked by.
inline constexpr bool kCameraFreedomUnite = kTitle == Title::FreedomUnite;
// The camera update.
inline constexpr std::uint32_t kCameraUpdate = kCameraFreedomUnite ? 0x08886E50u : 0x08886B54u;
// The projection update: compares the camera's field of view with the one the
// projection was built with, and builds it again when they differ. The game
// writes the field at +0xC every frame before this call.
inline constexpr std::uint32_t kProjectionUpdate = kCameraFreedomUnite ? 0x08815384u : 0x08816144u;
// The aim function in game_task, for action 1001.
inline constexpr std::uint32_t kAimFunction = kCameraFreedomUnite ? 0x09AA4DD0u : 0x09A9FFA0u;
// game_task's weapon switch: its table of cases, the bow's case, and that
// case's call towards the bow's handler (jal 0x09BB4F18, in FUComplete jal
// 0x09BB00C8).
inline constexpr std::uint32_t kWeaponTable = kCameraFreedomUnite ? 0x09C0C830u : 0x09C079B0u;
inline constexpr std::uint32_t kBowCase = kCameraFreedomUnite ? 0x09A8AF98u : 0x09A86198u;
inline constexpr std::uint32_t kBowCaseCall = kCameraFreedomUnite ? 0x0E6ED3C6u : 0x0E6EC032u;

// One instruction or constant of the game that the driver depends on.
struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
    const char *what;
};
// Everything the driver relies on in the game's code, checked before it is
// ever installed. Exposed for tests, which build a stand-in for that code.
[[nodiscard]] std::span<const CodeWord> game_code_signature();
// The aim function's prologue. It is not part of game_code_signature(): that
// function is in an overlay, so it is not in memory when the camera driver
// starts.
[[nodiscard]] std::span<const CodeWord> aim_code_signature();

// Checks the game's code against game_code_signature() and remembers the
// generated function behind the camera update. Nothing is hooked yet: that
// waits until the player turns Analog camera on, so a player who never does
// pays nothing at all. Returns false, having said why, if the code differs.
bool prepare_game_camera(psprecomp::Runtime &runtime, RotationFunction original);

// Once per presented game frame, at the game's flip (never per interpolated
// present): installs the hook the first time the option is on, and notices
// when the camera update has stopped running.
void game_camera_frame(psprecomp::Runtime &runtime);

// The port is driving the game's camera right now, so the game must not also
// act on the second stick: its own turn and vertical presets would fight ours.
[[nodiscard]] bool game_camera_driving();

// A bow or a bowgun is aiming under the driver. The aim function is shown the
// right stick, or the mouse, for that call only; the movement stick is left
// as the player is holding it.
[[nodiscard]] bool game_camera_aim_boost();

// A bow or a bowgun is aiming, read at the last flip from the game's own aim
// functions, whether or not the driver is on.
[[nodiscard]] bool game_camera_hunter_aiming();

// While a bow aims under the driver: the D-pad bits (PSP up 0x10, right 0x20,
// down 0x40, left 0x80) to add to the pad sample for the right stick at `x`
// and `y`, each -127 to 127, centred. The bow's aim reads only the D-pad, from
// code no wrapper can reach. 0 otherwise, and for an axis pushed too little.
[[nodiscard]] std::uint32_t game_camera_aim_dpad(int x, int y);

// The deflection, already centred, to show the aim function on its next call.
// Zero on both axes leaves the stick the game copied from the pad. `scale_x`
// and `scale_y` are how far that axis of the stick is pushed, from -1 to 1:
// the wrapper replaces the game's step on that axis with Aim speed times
// this, before the aim camera reads the heading. Zero leaves the step for
// the follow camera's driver (the mouse, whose size is its own degrees).
void game_camera_present_aim(int x, int y, float scale_x = 0.0f, float scale_y = 0.0f);

// A mouse has no stick for the game's aim code to read. While a bow or a
// bowgun aims and the mouse has moved, the direction, at full length, the
// aim stick should show the game this sample so its aim steps the mouse's
// way; the driver then sizes those steps from the mouse's degrees. Each axis
// is fully on or off.
struct StickDirection {
    float x{};
    float y{};
};
[[nodiscard]] std::optional<StickDirection> game_camera_mouse_aim();

// Once per game flip, after the mouse's motion for the coming frame is in.
// The game's camera, and the scope's view, are built from the aim after the
// aim code has made its own fixed step and before the driver resizes it, so
// that step shows for a frame: the view jumps by it whenever the game starts,
// stops or reverses a step, which is what the mouse makes it do. Here the step
// the game will make for the direction the mouse is about to show (learned
// from its earlier steps in this aim) is taken off the aim in advance; the
// next camera update puts it back before resizing, or the next flip does if
// no update came.
void game_camera_anticipate_aim(psprecomp::Runtime &runtime);

// Where the port does not drive the camera (Analog camera off, the village,
// a camera mode without a driver) the game's own turn is one speed, on or
// off, and the mouse can only switch it: -1 or +1 while the mouse moves left
// or right fast enough this frame, 0 otherwise. Vertical motion has nowhere
// to go there: the game's second stick up and down are recentring commands.
[[nodiscard]] int game_camera_mouse_stock_turn();

// Full-deflection speed for the current camera: Aim speed while a bow or a
// bowgun aims, Camera speed otherwise.
[[nodiscard]] float game_camera_degrees_per_second();

// Scales the projection camera's field of view at `camera + 0xC`, which the
// game sets before each projection update. False when the value there is not
// a plausible field of view, or `scale` is 1.
bool scale_projection_fov(psprecomp::GuestMemory &memory, std::uint32_t camera, float scale);

// The quest camera's eye and look-at point in world space, as its last update
// left them (+0x0 and +0x10 of the camera structure). Nothing when no update
// ran in the last two frames, or before the hook below is in place. The GE
// view this game draws the world with carries no translation, so this is
// where the camera's position is (damage numbers place themselves with it).
struct CameraPose {
    std::array<float, 3> eye{};
    std::array<float, 3> look_at{};
};
[[nodiscard]] std::optional<CameraPose> game_camera_pose(const psprecomp::GuestMemory &memory);
// Something besides Analog camera and lock-on needs the pose. Installs the
// camera update's wrapper at the next flip; while nothing drives the camera
// that wrapper only records where the camera structure is.
void game_camera_want_pose(bool wanted);

// Whether Focus camera (input/bindings.hpp) is held this frame. With nothing
// bound to it, the focus turn stays on L and this is ignored.
void set_focus_camera_held(bool held);

} // namespace mhfu::camera
