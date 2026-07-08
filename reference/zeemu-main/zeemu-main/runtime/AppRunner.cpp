#include "runtime/AppRunner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "brew/BrewBitmap.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewLoader.h"
#include "brew/BrewShell.h"
#include "brew/BrewThreadScheduler.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/EndianMemory.h"
#include "cpu/memory/VirtualMemory.h"
#include "graphics/RenderBackend.h"
#include "runtime/AEEHelperTable.h"
#include "runtime/GuestCallRunner.h"
#include "vfs/VirtualFileSystem.h"
namespace {

struct GuestDisplayProfile {
    int width = 640;
    int height = 480;
    const char* name = "zeebo";
};

bool arm_condition_passed(uint32_t cpsr, uint32_t cond) {
    const bool n = (cpsr & 0x80000000u) != 0;
    const bool z = (cpsr & 0x40000000u) != 0;
    const bool c = (cpsr & 0x20000000u) != 0;
    const bool v = (cpsr & 0x10000000u) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xa: return n == v;
    case 0xb: return n != v;
    case 0xc: return !z && n == v;
    case 0xd: return z || n != v;
    case 0xe: return true;
    default: return false;
    }
}

enum class InputBindingKind {
    HidButton,
    HidAxis,
    BrewKey,
};

enum class KeyboardProfile {
    Hid,
    BrewMenu,
    Hybrid,
};

enum class GamepadProfile {
    Off,
    Standard,
};

struct InputBinding {
    const char* name;
    SDL_Keycode key;
    InputBindingKind kind;
    uint32_t code;
    uint32_t down_value = 0;
};

struct ScriptedInputEvent {
    uint64_t at_ms = 0;
    const InputBinding* binding = nullptr;
    bool down = false;
    uint32_t device_index = 0;
};

struct GamepadRuntimeState {
    bool left_trigger_down = false;
    bool right_trigger_down = false;
    uint8_t hat = SDL_HAT_CENTERED;
    int axis_count = 0;
    int button_count = 0;
    int hat_count = 0;
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> gamepad_buttons{};
    std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> gamepad_axes{};
    std::array<bool, 32> joystick_buttons{};
    std::array<Sint16, 16> joystick_axes{};
    bool left_stick_left = false;
    bool left_stick_right = false;
    bool left_stick_up = false;
    bool left_stick_down = false;
};

GuestDisplayProfile display_profile_for_target(uint32_t target_clsid) {
    if (target_clsid == 0x010132E0u) {
        return {200, 200, "brew-i3d-sample"};
    }
    return {};
}

const std::vector<InputBinding>& input_bindings() {
    using K = InputBindingKind;
    static const std::vector<InputBinding> bindings = {
        // Host keyboard/gamepad input models the physical Zeebo controller.
        // Buttons are also BREW keys: native BREW apps and Zeebo titles see
        // the same AVK_* navigation/action events, while IHID remains available
        // for titles that consume controller state directly.
        {"up",        SDLK_UP,        K::HidButton, 0x0106C3FE}, // A: D-pad up
        {"down",      SDLK_DOWN,      K::HidButton, 0x0106C400}, // A: D-pad down
        {"left",      SDLK_LEFT,      K::HidButton, 0x0106C3FF}, // A: D-pad left
        {"right",     SDLK_RIGHT,     K::HidButton, 0x0106C401}, // A: D-pad right
        {"enter",     SDLK_RETURN,    K::BrewKey,   0xE035}, // AVK_SELECT
        {"space",     SDLK_SPACE,     K::BrewKey,   0xE035}, // AVK_SELECT
        {"1",         SDLK_1,         K::HidButton, 0x0106C40B}, // printed 1 -> AVK_GP_1
        {"2",         SDLK_2,         K::HidButton, 0x0106C40C}, // printed 2 -> AVK_GP_2
        {"3",         SDLK_3,         K::HidButton, 0x0106C40D}, // printed 3 -> AVK_GP_3
        {"4",         SDLK_4,         K::HidButton, 0x0106C40A}, // printed 4 -> AVK_GP_4
        {"z",         static_cast<SDL_Keycode>('z'), K::HidButton, 0x0106C40B}, // legacy keyboard alias for printed 1
        {"x",         static_cast<SDL_Keycode>('x'), K::HidButton, 0x0106C40C}, // legacy keyboard alias for printed 2
        {"c",         static_cast<SDL_Keycode>('c'), K::HidButton, 0x0106C40D}, // legacy keyboard alias for printed 3
        {"v",         static_cast<SDL_Keycode>('v'), K::HidButton, 0x0106C40A}, // legacy keyboard alias for printed 4
        {"escape",    SDLK_ESCAPE,    K::BrewKey,   0xE030}, // AVK_CLR
        {"backspace", SDLK_BACKSPACE, K::BrewKey,   0xE030}, // AVK_CLR
        {"home",      SDLK_HOME,      K::HidButton, 0x0106C403}, // E Home: Zeebo SDK maps Joystick_Back to HOME
        {"h",         static_cast<SDL_Keycode>('h'), K::HidButton, 0x0106C403}, // E Home
        {"q",         static_cast<SDL_Keycode>('q'), K::HidButton, 0x0106C406}, // left shoulder upper / ZL
        {"e",         static_cast<SDL_Keycode>('e'), K::HidButton, 0x0106C408}, // right shoulder upper / ZR
        {"lshift",    SDLK_LSHIFT,    K::HidButton, 0x0106C407}, // left shoulder lower
        {"rshift",    SDLK_RSHIFT,    K::HidButton, 0x0106C409}, // right shoulder lower
        {"hid-back",  0,              K::HidButton, 0x0106C403}, // explicit SDK Back UID
        {"hid-left-stick",  0,        K::HidButton, 0x0106C404}, // B1 thumbstick button
        {"hid-right-stick", 0,        K::HidButton, 0x0106C405}, // B2 thumbstick button

        {"w",         static_cast<SDL_Keycode>('w'), K::HidAxis, 0x0106C4D1, 0x0000}, // B1 left Y up
        {"s",         static_cast<SDL_Keycode>('s'), K::HidAxis, 0x0106C4D1, 0xffff}, // B1 left Y down
        {"a",         static_cast<SDL_Keycode>('a'), K::HidAxis, 0x0106C4D0, 0x0000}, // B1 left X left
        {"d",         static_cast<SDL_Keycode>('d'), K::HidAxis, 0x0106C4D0, 0xffff}, // B1 left X right
        {"i",         static_cast<SDL_Keycode>('i'), K::HidAxis, 0x0106C4CF, 0x0000}, // B2 right Y up
        {"k",         static_cast<SDL_Keycode>('k'), K::HidAxis, 0x0106C4CF, 0xffff}, // B2 right Y down
        {"j",         static_cast<SDL_Keycode>('j'), K::HidAxis, 0x0106C4CE, 0x0000}, // B2 right X left
        {"l",         static_cast<SDL_Keycode>('l'), K::HidAxis, 0x0106C4CE, 0xffff}, // B2 right X right

        {"avk-up",     0, K::BrewKey, 0xE031},
        {"avk-down",   0, K::BrewKey, 0xE032},
        {"avk-left",   0, K::BrewKey, 0xE033},
        {"avk-right",  0, K::BrewKey, 0xE034},
        {"avk-select", 0, K::BrewKey, 0xE035},
        {"avk-clear",  0, K::BrewKey, 0xE030},
        {"avk-soft1",  0, K::BrewKey, 0xE036},
        {"avk-soft2",  0, K::BrewKey, 0xE037},
        {"avk-gp1",    0, K::BrewKey, 0xE063},
        {"avk-gp2",    0, K::BrewKey, 0xE064},
        {"avk-gp3",    0, K::BrewKey, 0xE065},
        {"avk-gp4",    0, K::BrewKey, 0xE066},
        {"avk-gp5",    0, K::BrewKey, 0xE067},
        {"avk-gp6",    0, K::BrewKey, 0xE068},
        {"avk-gp-sl",  0, K::BrewKey, 0xE069},
        {"avk-gp-sr",  0, K::BrewKey, 0xE06A},
    };
    return bindings;
}

const InputBinding* input_binding_for_sdl_key(SDL_Keycode key) {
    for (const auto& binding : input_bindings()) {
        if (binding.key != 0 && binding.key == key) {
            return &binding;
        }
    }
    return nullptr;
}

const char* input_binding_kind_name(InputBindingKind kind) {
    switch (kind) {
        case InputBindingKind::HidButton: return "hid-button";
        case InputBindingKind::HidAxis: return "hid-axis";
        case InputBindingKind::BrewKey: return "brew-key";
        default: return "unknown";
    }
}

uint32_t keyboard_id_for_binding(const InputBinding& binding) {
    switch (binding.kind) {
        case InputBindingKind::HidButton:
            switch (binding.code) {
                case 0x0106C3FE: return 82; // AEEHID_KeyboardID_UpArrow
                case 0x0106C400: return 81; // AEEHID_KeyboardID_DownArrow
                case 0x0106C3FF: return 80; // AEEHID_KeyboardID_LeftArrow
                case 0x0106C401: return 79; // AEEHID_KeyboardID_RightArrow
                case 0x0106C40B: return 30; // AEEHID_KeyboardID_1
                case 0x0106C40C: return 31; // AEEHID_KeyboardID_2
                case 0x0106C40D: return 32; // AEEHID_KeyboardID_3
                case 0x0106C40A: return 33; // AEEHID_KeyboardID_4
                case 0x0106C402: return 40; // AEEHID_KeyboardID_Enter
                case 0x0106C403: return 74; // AEEHID_KeyboardID_Home
                default: return 0;
            }
        case InputBindingKind::HidAxis:
            switch (binding.code) {
                case 0x0106C4D1: return binding.down_value == 0 ? 26 : 22; // W/S
                case 0x0106C4D0: return binding.down_value == 0 ? 4 : 7;   // A/D
                default: return 0;
            }
        case InputBindingKind::BrewKey:
            switch (binding.code) {
                case 0xE031: return 82; // AVK_UP
                case 0xE032: return 81; // AVK_DOWN
                case 0xE033: return 80; // AVK_LEFT
                case 0xE034: return 79; // AVK_RIGHT
                case 0xE035: return 40; // AVK_SELECT
                case 0xE030: return 41; // AVK_CLR
                case 0xE063: return 30; // AVK_GP_1
                case 0xE064: return 31; // AVK_GP_2
                case 0xE065: return 32; // AVK_GP_3
                case 0xE066: return 33; // AVK_GP_4
                default: return 0;
            }
        default:
            return 0;
    }
}

const InputBinding* named_input_binding(const char* binding_name) {
    for (const auto& binding : input_bindings()) {
        if (binding_name == std::string(binding.name)) {
            return &binding;
        }
    }
    return nullptr;
}

std::string lower_trimmed(std::string text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    std::string out;
    out.reserve(last - first);
    for (size_t i = first; i < last; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
    }
    return out;
}

const InputBinding* input_binding_for_name(const std::string& name) {
    const std::string key = lower_trimmed(name);

    auto named = [&](const char* binding_name) -> const InputBinding* { return named_input_binding(binding_name); };

    if (key == "dpad-up") return named("up");
    if (key == "dpad-down") return named("down");
    if (key == "dpad-left") return named("left");
    if (key == "dpad-right") return named("right");
    if (key == "button1" || key == "btn1" || key == "face1" || key == "c1") return named("1");
    if (key == "button2" || key == "btn2" || key == "face2" || key == "c2") return named("2");
    if (key == "button3" || key == "btn3" || key == "face3" || key == "c3" ||
        key == "info" || key == "submenu") return named("3");
    if (key == "button4" || key == "btn4" || key == "face4" || key == "c4" || key == "context") return named("4");
    if (key == "home-button" || key == "stage" || key == "start") return named("home");
    if (key == "confirm" || key == "select" || key == "back" || key == "cancel" || key == "clear" || key == "clr" || key == "esc") {
        return named(key == "confirm" || key == "select" ? "avk-select" : "avk-clear");
    }
    if (key == "d1" || key == "zl" || key == "sl" || key == "left-trigger" || key == "left-shoulder") return named("q");
    if (key == "d2" || key == "zr" || key == "sr" || key == "right-trigger" || key == "right-shoulder") return named("e");
    if (key == "ls-up" || key == "left-stick-up") return named("w");
    if (key == "ls-down" || key == "left-stick-down") return named("s");
    if (key == "ls-left" || key == "left-stick-left") return named("a");
    if (key == "ls-right" || key == "left-stick-right") return named("d");
    if (key == "rs-up" || key == "right-stick-up") return named("i");
    if (key == "rs-down" || key == "right-stick-down") return named("k");
    if (key == "rs-left" || key == "right-stick-left") return named("j");
    if (key == "rs-right" || key == "right-stick-right") return named("l");
    if (key == "avk-enter") return named("avk-select");
    if (key == "avk-clr") return named("avk-clear");

    for (const auto& binding : input_bindings()) {
        if (key == binding.name) {
            return &binding;
        }
    }
    return nullptr;
}

KeyboardProfile keyboard_profile_from_env() {
    const char* raw = std::getenv("ZEEMU_KEYBOARD_PROFILE");
    const std::string profile = raw ? lower_trimmed(raw) : "";
    if (profile == "brew" || profile == "brew-menu" || profile == "avk") {
        return KeyboardProfile::BrewMenu;
    }
    if (profile == "hybrid" || profile == "hid+brew" || profile == "brew+hid") {
        return KeyboardProfile::Hybrid;
    }
    return KeyboardProfile::Hybrid;
}

GamepadProfile gamepad_profile_from_env() {
    const char* raw = std::getenv("ZEEMU_GAMEPAD_PROFILE");
    const std::string profile = raw ? lower_trimmed(raw) : "";
    if (profile == "off" || profile == "none" || profile == "disabled") {
        return GamepadProfile::Off;
    }
    return GamepadProfile::Standard;
}

const char* keyboard_profile_name(KeyboardProfile profile) {
    switch (profile) {
        case KeyboardProfile::BrewMenu: return "brew-menu";
        case KeyboardProfile::Hybrid: return "standard";
        case KeyboardProfile::Hid:
        default: return "standard";
    }
}

const char* gamepad_profile_name(GamepadProfile profile) {
    switch (profile) {
        case GamepadProfile::Standard: return "standard";
        case GamepadProfile::Off:
        default: return "off";
    }
}

const InputBinding* brew_menu_binding_for_sdl_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: return named_input_binding("avk-up");
        case SDLK_DOWN: return named_input_binding("avk-down");
        case SDLK_LEFT: return named_input_binding("avk-left");
        case SDLK_RIGHT: return named_input_binding("avk-right");
        case SDLK_RETURN:
        case SDLK_SPACE:
            return named_input_binding("avk-select");
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            return named_input_binding("avk-clear");
        default:
            return nullptr;
    }
}

std::vector<const InputBinding*> input_bindings_for_sdl_event(SDL_Keycode key,
                                                              SDL_Keymod mod,
                                                              KeyboardProfile profile) {
    std::vector<const InputBinding*> out;
    const InputBinding* brew_menu = brew_menu_binding_for_sdl_key(key);
    const bool ctrl_brew = (mod & SDL_KMOD_CTRL) != 0 && brew_menu != nullptr;

    if (profile == KeyboardProfile::BrewMenu && brew_menu) {
        out.push_back(brew_menu);
        return out;
    }

    const InputBinding* hid = input_binding_for_sdl_key(key);
    if (hid) {
        out.push_back(hid);
    }

    if (!hid && (profile == KeyboardProfile::Hybrid || ctrl_brew) && brew_menu) {
        out.push_back(brew_menu);
    }
    return out;
}

const InputBinding* xbox_hid_button_binding(SDL_GamepadButton button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return named_input_binding("up");
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return named_input_binding("down");
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return named_input_binding("left");
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return named_input_binding("right");
        case SDL_GAMEPAD_BUTTON_SOUTH: return named_input_binding("1");       // Xbox A
        case SDL_GAMEPAD_BUTTON_EAST: return named_input_binding("2");        // Xbox B
        case SDL_GAMEPAD_BUTTON_WEST: return named_input_binding("3");        // Xbox X
        case SDL_GAMEPAD_BUTTON_NORTH: return named_input_binding("4");       // Xbox Y
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return named_input_binding("q");
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return named_input_binding("e");
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return named_input_binding("hid-left-stick");
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return named_input_binding("hid-right-stick");
        case SDL_GAMEPAD_BUTTON_START:
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return named_input_binding("home");
        case SDL_GAMEPAD_BUTTON_BACK:
            return named_input_binding("hid-back");
        default:
            return nullptr;
    }
}

std::vector<const InputBinding*> input_bindings_for_gamepad_button(SDL_GamepadButton button,
                                                                   GamepadProfile profile) {
    std::vector<const InputBinding*> out;
    if (profile == GamepadProfile::Standard) {
        if (const InputBinding* binding = xbox_hid_button_binding(button)) {
            out.push_back(binding);
        }
    }
    return out;
}

const InputBinding* brew_key_fallback_for_hid_binding(const InputBinding& binding) {
    if (binding.kind != InputBindingKind::HidButton) {
        return nullptr;
    }
    // ZeeboSDKInstaller BREW404 IHID Extension:
    // AEEHIDButtons.h defines the physical Zeebo button UID rotation, and
    // the developer manual defines the default HID UID -> AVK event mapping.
    switch (binding.code) {
        case 0x0106C3FE: return named_input_binding("avk-up");
        case 0x0106C400: return named_input_binding("avk-down");
        case 0x0106C3FF: return named_input_binding("avk-left");
        case 0x0106C401: return named_input_binding("avk-right");
        case 0x0106C402: return named_input_binding("avk-clear");
        case 0x0106C403: return named_input_binding("avk-clear");  // Home generates AVK_CLR on Zeebo.
        case 0x0106C404: return named_input_binding("avk-soft1");
        case 0x0106C405: return named_input_binding("avk-select"); // Right thumbstick launches/selects in Qualcomm UI.
        case 0x0106C40B: return named_input_binding("avk-gp1");    // Zeebo printed button 1.
        case 0x0106C40C: return named_input_binding("avk-gp2");    // Zeebo printed button 2.
        case 0x0106C40D: return named_input_binding("avk-gp3");    // Zeebo printed button 3.
        case 0x0106C40A: return named_input_binding("avk-gp4");    // Zeebo printed button 4.
        case 0x0106C406: return named_input_binding("avk-gp5");
        case 0x0106C408: return named_input_binding("avk-gp6");
        case 0x0106C407: return named_input_binding("avk-gp-sl");
        case 0x0106C409: return named_input_binding("avk-gp-sr");
        default:
            return nullptr;
    }
}

SDL_GamepadButton xbox_layout_joystick_button(int button) {
    switch (button) {
        case 0: return SDL_GAMEPAD_BUTTON_SOUTH;
        case 1: return SDL_GAMEPAD_BUTTON_EAST;
        case 2: return SDL_GAMEPAD_BUTTON_WEST;
        case 3: return SDL_GAMEPAD_BUTTON_NORTH;
        case 4: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case 5: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case 6: return SDL_GAMEPAD_BUTTON_BACK;
        case 7: return SDL_GAMEPAD_BUTTON_START;
        case 8: return SDL_GAMEPAD_BUTTON_GUIDE;
        case 9: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
        case 10: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        default: return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

SDL_GamepadButton dpad_button_for_hat(uint8_t mask) {
    switch (mask) {
        case SDL_HAT_UP: return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case SDL_HAT_DOWN: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case SDL_HAT_LEFT: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case SDL_HAT_RIGHT: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        default: return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

uint32_t gamepad_axis_to_hid_value(Sint16 value) {
    constexpr Sint16 kDeadzone = 8000;
    if (value > -kDeadzone && value < kDeadzone) {
        return 0x8000u;
    }
    const int converted = static_cast<int>(value) + 32768;
    return static_cast<uint32_t>(std::clamp(converted, 0, 65535));
}

const InputBinding* gamepad_trigger_binding(SDL_GamepadAxis axis) {
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return named_input_binding("q");
    if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return named_input_binding("e");
    return nullptr;
}

bool parse_u64_strict(const std::string& text, uint64_t& value) {
    const std::string trimmed = lower_trimmed(text);
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtoull(trimmed.c_str(), &end, 0);
    return end != trimmed.c_str() && end && *end == '\0';
}

uint64_t env_u64_or_default(const char* name, uint64_t fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return fallback;
    }
    uint64_t parsed = 0;
    return parse_u64_strict(raw, parsed) ? parsed : fallback;
}

std::vector<ScriptedInputEvent> parse_scripted_input(const char* spec) {
    std::vector<ScriptedInputEvent> events;
    if (!spec || !*spec) {
        return events;
    }

    std::string text(spec);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of(",;", start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string token = text.substr(start, end - start);
        start = end + 1;

        const size_t first_colon = token.find(':');
        if (first_colon == std::string::npos) {
            std::cerr << "Ignoring input token without time separator: " << token << std::endl;
            continue;
        }
        uint64_t at_ms = 0;
        if (!parse_u64_strict(token.substr(0, first_colon), at_ms)) {
            std::cerr << "Ignoring input token with invalid time: " << token << std::endl;
            continue;
        }

        std::string action = token.substr(first_colon + 1);
        uint32_t device_index = 0;
        std::string action_trimmed = lower_trimmed(action);
        if (action_trimmed.rfind("p1:", 0) == 0 || action_trimmed.rfind("dev1:", 0) == 0 || action_trimmed.rfind("pad1:", 0) == 0) {
            const size_t colon = action.find(':');
            action = colon == std::string::npos ? std::string{} : action.substr(colon + 1);
            device_index = 0;
        } else if (action_trimmed.rfind("p2:", 0) == 0 || action_trimmed.rfind("dev2:", 0) == 0 || action_trimmed.rfind("pad2:", 0) == 0) {
            const size_t colon = action.find(':');
            action = colon == std::string::npos ? std::string{} : action.substr(colon + 1);
            device_index = 1;
        }
        std::string hold_text;
        const size_t second_colon = action.find(':');
        if (second_colon != std::string::npos) {
            hold_text = action.substr(second_colon + 1);
            action = action.substr(0, second_colon);
        }

        bool explicit_down_up = false;
        bool down = true;
        std::string name = lower_trimmed(action);
        if (!name.empty() && (name[0] == '+' || name[0] == '-')) {
            explicit_down_up = true;
            down = name[0] == '+';
            name.erase(name.begin());
        }
        if (name.size() > 5 && name.compare(name.size() - 5, 5, "-down") == 0) {
            explicit_down_up = true;
            down = true;
            name.resize(name.size() - 5);
        } else if (name.size() > 3 && name.compare(name.size() - 3, 3, "-up") == 0) {
            explicit_down_up = true;
            down = false;
            name.resize(name.size() - 3);
        }

        const InputBinding* binding = input_binding_for_name(name);
        if (!binding) {
            std::cerr << "Ignoring input token with unknown button: " << token << std::endl;
            continue;
        }

        if (explicit_down_up) {
            events.push_back({at_ms, binding, down, device_index});
            continue;
        }

        uint64_t hold_ms = 80;
        if (!hold_text.empty() && !parse_u64_strict(hold_text, hold_ms)) {
            std::cerr << "Ignoring input token with invalid hold time: " << token << std::endl;
            continue;
        }
        events.push_back({at_ms, binding, true, device_index});
        events.push_back({at_ms + hold_ms, binding, false, device_index});
    }

    std::stable_sort(events.begin(), events.end(), [](const ScriptedInputEvent& a, const ScriptedInputEvent& b) {
        return a.at_ms < b.at_ms;
    });
    return events;
}

void fill_placeholder_frame(std::vector<uint8_t>& frame, int width, int height, uint32_t tick) {
    auto* pixels = reinterpret_cast<uint16_t*>(frame.data());
    const uint16_t bar_colors[8] = {
        0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x001F, 0x051F, 0x780F, 0xFFFF
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint16_t color = 0x18C3;
            if (y < 40) {
                color = bar_colors[(x / 80) & 7];
            } else {
                bool checker = (((x >> 4) ^ (y >> 4) ^ static_cast<int>((tick >> 2) & 1)) & 1) != 0;
                color = checker ? 0x39E7 : 0x2104;
                if ((x >= width / 2 - 48 && x < width / 2 + 48) &&
                    (y >= height / 2 - 48 && y < height / 2 + 48)) {
                    color = 0x7BEF;
                }
                if (x == width / 2 || y == height / 2) {
                    color = 0xFFFF;
                }
            }
            pixels[y * width + x] = color;
        }
    }
}

void apply_sdl_gamepad_env_hints() {
    if (const char* mapping = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING"); mapping && *mapping) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, mapping);
    }
    if (const char* mapping_file = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING_FILE"); mapping_file && *mapping_file) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, mapping_file);
    }
    if (std::getenv("ZEEMU_INPUT_BACKGROUND")) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
}

} // namespace

int run_emulator(const std::string& mod_path, uint32_t target_clsid) {
    apply_sdl_gamepad_env_hints();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    const GuestDisplayProfile display_profile = display_profile_for_target(target_clsid);
    auto backend_kind = zeemu::gfx::parse_render_backend(std::getenv("ZEEMU_RENDER_BACKEND"));
    auto presenter = zeemu::gfx::create_frame_presenter(backend_kind);
    std::cout << "Render backend: " << zeemu::gfx::render_backend_name(backend_kind) << std::endl;
    std::cout << "Display profile: " << display_profile.name << " "
              << display_profile.width << "x" << display_profile.height << std::endl;
    if (!presenter || !presenter->initialize("Zeemu", display_profile.width, display_profile.height)) {
        SDL_Quit();
        return 1;
    }

    VirtualFileSystem vfs = VirtualFileSystem::create_default(".");
    VirtualMemory vmem;
    EndianMemory emem(&vmem, LittleEndian);

    // BrewShell creates HLE objects and framebuffers in guest heap space during
    // construction, so reserve heap/hooks before any HLE allocation happens.
    vmem.alloc_protect(0x50000000, 0x4000000, Memory::Read | Memory::Write);
    vmem.alloc_protect(0xFF000000, 0x100000, Memory::Read | Memory::Write | Memory::Execute);

    // CPU contains ~4.5 MB of std::function arrays (thumb/arm dispatch tables) — must be heap-allocated
    auto cpu_up = std::make_unique<CPU>(emem);
    CPU& cpu = *cpu_up;
    BrewShell shell(emem, vfs, display_profile.width, display_profile.height);
    shell.set_presenter(presenter.get());
    shell.set_virtual_memory(&vmem);
    if (std::getenv("ZEEMU_QUIET")) shell.set_suppress_dbgprintf(true);
    BrewLoader loader(emem, &vmem);

    std::cout << "Target Applet CLSID: 0x" << std::hex << target_clsid << std::endl;

    // Make the launched CLSID known to the shell up front so generic applet
    // self-creation (ISHELL_CreateInstance of the running app's own class) can
    // be resolved without a hardcoded per-game CLSID list.
    shell.set_current_applet_cls(target_clsid);

    // Resolve app directory and setup app-specific mounts
    std::filesystem::path mod_abs = std::filesystem::absolute(mod_path);
    std::filesystem::path app_mount_dir = mod_abs.parent_path();
    const std::filesystem::path nested_app_dir = app_mount_dir / mod_abs.stem();
    if (std::filesystem::is_directory(nested_app_dir)) {
        // Some local SDK/homebrew packages keep the .mod one level above the
        // runtime app directory. BREW relative paths still resolve from the
        // module directory that owns the app data.
        app_mount_dir = nested_app_dir;
    }
    vfs.setup_app_mounts(target_clsid, app_mount_dir);
    shell.set_current_directory("fs:/~/");

    uint32_t ppMod = shell.malloc(4);
    emem.write_value(ppMod, 0);

    BrewModule* mod = loader.load_module(mod_path, 0x10000000);
    if (!mod) {
        std::cerr << "Failed to load module: " << mod_path << std::endl;
        presenter.reset();
        SDL_Quit();
        return 1;
    }

    // Allocate module memory. Code references can span well beyond file size
    // (e.g. Quake references data at link-time address 0x64C800, far past the
    // 0x6B440-byte file). Use a generous allocation to cover all possible
    // ROPI-relative data accesses.
    uint32_t mod_alloc_size = std::max(mod->image_size, 0x800000u);
    vmem.alloc_protect(mod->base_address, mod_alloc_size, Memory::Read | Memory::Write | Memory::Execute);
    printf("  Module allocation: VA=0x%08X size=0x%X (%u KB)\n", mod->base_address, mod_alloc_size, mod_alloc_size / 1024);

    // Set ROPI translation: link-time addresses [0, alloc_size) → [base_address, base_address+alloc_size)
    cpu.set_ropi_params(mod->base_address, mod_alloc_size);

    // Allocate stack (16MB below 0x20000000).
    // Keep it executable because some BREW binaries materialize dynamic thunks on the stack.
    vmem.alloc_protect(0x1F000000, 0x1000000, Memory::Read | Memory::Write | Memory::Execute);
    
    // Inject IShell pointer at ImageBase - 4, matching the Zeebo ROPI helper convention.
    vmem.alloc_protect(mod->base_address - 0x1000, 0x1000, Memory::Read | Memory::Write);
    uint32_t shell_ptr = shell.get_shell_ptr();
    uint32_t object_ptr = emem.read_value(shell_ptr); // Real object.

    std::cout << "HLE IShell: shell_ptr=0x" << std::hex << shell_ptr << " obj=0x" << object_ptr << std::endl;

    emem.write_value(mod->base_address - 4, object_ptr);
    emem.write_value(mod->base_address - 8, object_ptr);

    // BREW dynamic modules use GET_HELPER() == *((AEEHelperFuncs**)AEEMod_Load - 1).
    // Provide the minimal stdlib/helper table needed by AEEModGen/AEEAppGen.
    uint32_t helper_table = build_aee_helper_table(shell, emem);
    uint32_t entry_low = mod->entry_point - mod->base_address;
    auto patch_helper_slots = [&]() {
        emem.write_value(mod->base_address - 4, helper_table);
        emem.write_value(mod->base_address - 8, 0u);
        bool entry_helper_slot_is_safe = mod->is_raw || entry_low <= 0x40;
        if (entry_helper_slot_is_safe && entry_low >= 8) {
            emem.write_value(mod->entry_point - 4, helper_table);
            emem.write_value(mod->entry_point - 8, 0u);
            emem.write_value(entry_low - 4, helper_table);
            emem.write_value(entry_low - 8, 0u);
        }

        // The low mirror is linked at image base 0, so its bootstrap copies
        // helper/version from [0 - 4] and [0 - 8] into the ROPI data header.
        emem.write_value(0xFFFFFFFCu, helper_table);
        emem.write_value(0xFFFFFFF8u, 0u);

        // Some ROPI thunks materialize a PC-relative address near 0x200, then
        // read a word just before it. Only fill real padding slots: several
        // raw BREW modules have live code at 0x1F0..0x1FC.
        for (uint32_t slot = 0x1F0; slot <= 0x1FC; slot += 4) {
            if (emem.read_value(slot) == 0) {
                emem.write_value(slot, helper_table);
            }
            const uint32_t high_slot = mod->base_address + slot;
            if (emem.read_value(high_slot) == 0) {
                emem.write_value(high_slot, helper_table);
            }
        }
    };
    patch_helper_slots();
    std::cout << "AEEHelperFuncs table: 0x" << std::hex << helper_table
              << " patched for module bootstrap; entry_low=0x" << entry_low
              << " header_size=0x" << mod->header_size
              << ", helper slot 0x1FC" << std::endl;
    std::cout << "  helper slot check: [0x1FC]=0x" << std::hex << emem.read_value(0x1FC)
              << " [helper+0x68]=0x" << emem.read_value(helper_table + 0x68) << std::endl;

    // Allocate a page for the magic return; SVC #0xDEAD stops execution.
    uint32_t magic_ret = 0xDEADBEE0;
    vmem.alloc_protect(magic_ret, 0x1000, Memory::Read | Memory::Write | Memory::Execute);
    emem.write_value(magic_ret, 0xEF00DEADu);  // SVC #0xDEAD

    uint32_t hle_return_override = 0;
    const char* active_guest_call = nullptr;
    uint32_t active_guest_hle_count = 0;

    // Hook handler: magic SVC stops CPU, BREW hook stubs return through LR,
    // and guest inline SVCs can request normal inline-SVC return.
    std::string guest_svc_line;
    cpu.set_hle_handler([&](uint32_t hook_id, uint32_t inline_return, uint32_t lr_return, uint32_t svc_pc) -> uint32_t {
        if (hook_id == 0x00DEAD) {
            cpu.stop();
            return lr_return;
        }
        if (hook_id == 0x0000AB && cpu.get_reg(REG_R0) == 3) {
            const uint32_t pch = cpu.get_reg(REG_R1);
            char ch = 0;
            if (pch != 0 && pch < 0xFF000000u) {
                ch = static_cast<char>(emem.read_value(pch, EndianMemory::Byte));
            }
            if (ch == '\n' || ch == '\r') {
                if (!guest_svc_line.empty()) {
                    std::cout << "[guest svc 0xab] " << guest_svc_line << std::endl;
                    guest_svc_line.clear();
                }
            } else if (ch != 0) {
                guest_svc_line.push_back(ch);
                if (guest_svc_line.size() >= 160) {
                    std::cout << "[guest svc 0xab] " << guest_svc_line << std::endl;
                    guest_svc_line.clear();
                }
            }
            cpu.set_reg(REG_R0, 0);
            return inline_return;
        }
        if (hook_id == 0x123456) {
            static int semihost_trap_logs = 0;
            if (semihost_trap_logs < 4) {
                std::cout << "[guest semihost 0x123456] op=0x" << std::hex << cpu.get_reg(REG_R0)
                          << " arg=0x" << cpu.get_reg(REG_R1)
                          << " lr=0x" << cpu.get_reg(REG_LR) << std::endl;
            } else if (semihost_trap_logs == 4) {
                std::cout << "[guest semihost 0x123456] suppressing repeated traps" << std::endl;
            }
            ++semihost_trap_logs;
            cpu.set_reg(REG_R0, 0);
            return inline_return;
        }
        const bool brew_hook_stub = svc_pc >= 0xFF000000u && svc_pc < 0xFF100000u;
        if (!brew_hook_stub) {
            if (std::getenv("ZEEMU_TRACE_INLINE_SVC") != nullptr) {
                static int guest_inline_svc_logs = 0;
                if (guest_inline_svc_logs < 8) {
                    std::cout << "[guest inline svc] imm=0x" << std::hex << hook_id
                              << " svc_pc=0x" << svc_pc
                              << " inline_return=0x" << inline_return
                              << " lr=0x" << lr_return << std::endl;
                } else if (guest_inline_svc_logs == 8) {
                    std::cout << "[guest inline svc] suppressing repeated inline SVC logs" << std::endl;
                }
                ++guest_inline_svc_logs;
            }
            cpu.set_reg(REG_R0, 0);
            return inline_return;
        }
        if (active_guest_call != nullptr) {
            ++active_guest_hle_count;
        }
        uint32_t hook_return = lr_return;
        if (hle_return_override != 0) {
            hook_return = hle_return_override;
            cpu.set_reg(REG_LR, hook_return);
            hle_return_override = 0;
        }
        // BREW hooks are entered like normal ARM function calls from guest
        // code. Model the ABI boundary: HLE implementations may set return
        // registers, but must not leak host-side scratch state into guest
        // callee-saved registers. Bio4's cleanup path keeps the helper-table
        // base live in R9 across AEEHelper_free; clobbering it turns the next
        // FREE call into BLX 0 through the low module mirror.
        std::array<uint32_t, 9> callee_saved{};
        for (int reg = REG_R4; reg <= REG_R11; ++reg) {
            callee_saved[static_cast<size_t>(reg - REG_R4)] = cpu.get_reg(static_cast<CPUReg>(reg));
        }
        callee_saved[8] = cpu.get_reg(REG_SP);
        shell.handle_hook(hook_id, cpu);
        for (int reg = REG_R4; reg <= REG_R11; ++reg) {
            cpu.set_reg(static_cast<CPUReg>(reg), callee_saved[static_cast<size_t>(reg - REG_R4)]);
        }
        cpu.set_reg(REG_SP, callee_saved[8]);
        return hook_return;
    });
    
    std::cout << "Starting app execution at 0x" << std::hex << mod->entry_point << std::endl;
    
    uint32_t real_entry = mod->entry_point;

    uint32_t sig_buf = shell.malloc(4096);
    emem.write_value(sig_buf, 0);

    cpu.reset(real_entry);
    cpu.set_reg(REG_R0, object_ptr);
    cpu.set_reg(REG_R1, sig_buf);
    cpu.set_reg(REG_R2, ppMod);
    cpu.set_reg(REG_R3, ppMod);
    cpu.set_reg(REG_R4, object_ptr);
    cpu.set_reg(REG_R5, sig_buf);
    cpu.set_reg(REG_R6, ppMod);
    cpu.set_reg(REG_R7, ppMod);
    for (int i = 8; i <= 12; ++i) cpu.set_reg((CPUReg)i, ppMod);
    cpu.set_reg(REG_LR, magic_ret);

    std::cout << "Args: R0=IShell=0x" << std::hex << object_ptr << " R2=R3=ppMod=0x" << ppMod << std::endl;

    cpu.step_loop();

    uint32_t result = cpu.get_reg(REG_R0);
    std::cout << "AEEMod_Load result: 0x" << std::hex << result << std::endl;
    std::cout << "Helper slots after AEEMod_Load:"
              << " [0x1F0]=0x" << std::hex << emem.read_value(0x1F0)
              << " [0x1F4]=0x" << emem.read_value(0x1F4)
              << " [0x1F8]=0x" << emem.read_value(0x1F8)
              << " [0x1FC]=0x" << emem.read_value(0x1FC)
              << std::endl;
    
    std::cout << "AEEMod_Load return registers:" << std::endl;
    for (int i = 0; i <= 15; ++i) {
        std::cout << "  R" << std::dec << i << "=0x" << std::hex << cpu.get_reg((CPUReg)i) << " ";
        if (i % 4 == 3) std::cout << std::endl;
    }

    uint32_t module_obj = cpu.mem().read_value(ppMod);
    std::cout << "IModule object pointer: 0x" << std::hex << module_obj << std::endl;

    auto read_guest32 = [&](uint32_t addr) -> uint32_t {
        return cpu.mem().read_value(addr);
    };
    auto module_exec_extent = [&]() -> uint32_t {
        uint32_t section_extent = 0x40 + mod->code_size + mod->data_size + mod->bss_size;
        return mod->image_size > section_extent ? mod->image_size : section_extent;
    };

    if (module_obj != 0) {
        uint32_t module_vtable = read_guest32(module_obj);
        std::cout << "IModule vtable pointer: 0x" << std::hex << module_vtable << std::endl;

        auto is_module_code_ptr = [&](uint32_t ptr) -> bool {
            uint32_t module_extent = module_exec_extent();
            return (ptr < module_extent) || (ptr >= mod->base_address && ptr < mod->base_address + module_extent);
        };
        uint32_t pfn_mod_create = read_guest32(module_obj + 12);
        // Apply ROPI translation to function pointers
        pfn_mod_create = cpu.translate_ropi(pfn_mod_create);
        if (pfn_mod_create != 0 && !is_module_code_ptr(pfn_mod_create)) {
            std::cout << "Sanitizing invalid AEEMod.pfnModCrInst=0x" << std::hex << pfn_mod_create
                      << " -> dynamic AEEClsCreateInstance path" << std::endl;
            emem.write_value(module_obj + 12, 0u);
            pfn_mod_create = 0;
        }

        std::cout << "IModule object words:";
        for (int i = 0; i < 8; ++i) {
            std::cout << " +" << std::dec << (i * 4) << "=0x" << std::hex << read_guest32(module_obj + i * 4);
        }
        std::cout << std::endl;
        std::cout << "IModule vtable words:";
        for (int i = 0; i < 6; ++i) {
            std::cout << " +" << std::dec << (i * 4) << "=0x" << std::hex << read_guest32(module_vtable + i * 4);
        }
        std::cout << std::endl;

        uint32_t create_inst_ptr = read_guest32(module_vtable + 8); // Offset 8 = CreateInstance
        create_inst_ptr = cpu.translate_ropi(create_inst_ptr);
        std::cout << "IModule_CreateInstance function: 0x" << std::hex << create_inst_ptr << std::endl;

        if (create_inst_ptr != 0 && create_inst_ptr < 0xFF000000) {
            std::cout << "Attempting to create Applet instance..." << std::endl;

            uint32_t ppApplet = shell.malloc(4);
            emem.write_value(ppApplet, 0);
            shell.set_pending_applet_output_ptr(ppApplet);

            if (target_clsid != 0) {
                char buf[32];
                std::sprintf(buf, "fs:/~0x%08X", target_clsid);
                shell.set_current_directory(buf);
                std::cout << "Set app current directory to: " << buf << std::endl;
            }

            cpu.reset(create_inst_ptr);
            cpu.set_reg(REG_R0, module_obj);
            cpu.set_reg(REG_R1, object_ptr); // IShell* = object_ptr (game reads vtable from here)
            cpu.set_reg(REG_R2, target_clsid);
            cpu.set_reg(REG_R3, ppApplet);
            cpu.set_reg(REG_LR, magic_ret);
            patch_helper_slots();
            const uint64_t create_instance_max_steps =
                env_u64_or_default("ZEEMU_CREATEINSTANCE_MAX_STEPS", 500000000ull);
            const uint64_t pp_applet_guard_steps =
                env_u64_or_default("ZEEMU_CREATEINSTANCE_PPAPPLET_GUARD_STEPS", 0ull);
            const bool allow_partial_applet =
                std::getenv("ZEEMU_CREATEINSTANCE_ALLOW_PARTIAL_APPLET") != nullptr;
            uint32_t recent_pc[16] = {};
            uint32_t recent_op[16] = {};
            int recent_pos = 0;
            bool trace_branches = std::getenv("ZEEMU_TRACE_BRANCHES") != nullptr;
            auto record_step = [&]() {
                uint32_t pc = cpu.get_reg(REG_PC);
                uint32_t op = cpu.mem().read_value(pc);
                if ((op & 0x0FFFFFF0u) == 0x012FFF30u) {
                    uint32_t rm = op & 0x0Fu;
                    uint32_t target = cpu.get_reg((CPUReg)rm);
                    if (trace_branches) {
                        std::cout << "  [branch] PC=0x" << std::hex << pc
                                  << " op=0x" << op
                                  << " R" << std::dec << rm
                                  << "=0x" << std::hex << target
                                  << " LR=0x" << cpu.get_reg(REG_LR)
                                  << " CPSR.T=" << ((cpu.get_reg(REG_CPSR) >> 5) & 1)
                                  << std::endl;
                    }
                    const bool link = (op & 0x20u) != 0;
                    if (link && target >= 0xFF000000u &&
                        arm_condition_passed(cpu.get_reg(REG_CPSR), op >> 28)) {
                        hle_return_override = pc + 4;
                    }
                }
                recent_pc[recent_pos & 15] = pc;
                recent_op[recent_pos & 15] = op;
                ++recent_pos;
                cpu.step_once();
            };

            bool left_module = false;
            const uint64_t guard_limit =
                create_instance_max_steps == 0 ? UINT64_MAX : ((create_instance_max_steps + 999ull) / 1000ull);
            for (uint64_t guard = 0; guard < guard_limit; ++guard) {
                for (int _s = 0; _s < 1000 && !cpu.is_stopped() && cpu.get_reg(REG_PC) != magic_ret; ++_s) {
                    record_step();
                    uint32_t stepped_pc = cpu.get_reg(REG_PC);
                    uint32_t eff_pc = cpu.translate_ropi(stepped_pc);
                    uint32_t module_extent = module_exec_extent();
                    bool stepped_low = eff_pc < module_extent;
                    bool stepped_loaded = eff_pc >= mod->base_address && eff_pc < (mod->base_address + module_extent);
                    if (stepped_pc != magic_ret && !stepped_low && !stepped_loaded && stepped_pc < 0xFF000000) {
                        left_module = true;
                        break;
                    }
                }
                uint32_t pc = cpu.get_reg(REG_PC);
                if ((guard % 1000) == 999) {
                    uint32_t cur_applet = read_guest32(ppApplet);
                    std::cout << "  [CreateInstance] guard=" << std::dec << guard + 1
                              << " PC=0x" << std::hex << pc
                              << " LR=0x" << cpu.get_reg(REG_LR)
                              << " R0=0x" << cpu.get_reg(REG_R0)
                              << " ppApplet=0x" << cur_applet << std::endl;
                }
                // Detect stuck-in-data: if ppApplet is set but CreateInstance keeps
                // running (some titles fill ppApplet before component/resource init
                // finishes), give it a generous, game-agnostic grace period before
                // dispatching EVT_APP_START.
                uint32_t cur_applet_check = read_guest32(ppApplet);
                const uint64_t steps_so_far = static_cast<uint64_t>(guard + 1) * 1000ull;
                if (cur_applet_check != 0 && pp_applet_guard_steps != 0 && steps_so_far > pp_applet_guard_steps) {
                    std::cout << "  [CreateInstance] ppApplet set after " << std::dec << (guard + 1) * 1000
                              << " steps at PC=0x" << std::hex << pc
                              << " — partial applet guard reached applet=0x" << cur_applet_check << std::endl;
                    break;
                }
                uint32_t module_extent = module_exec_extent();
                uint32_t eff_pc_outer = cpu.translate_ropi(pc);
                bool in_low_module = eff_pc_outer < module_extent;
                bool in_loaded_module = eff_pc_outer >= mod->base_address && eff_pc_outer < (mod->base_address + module_extent);
                if (left_module || (pc != magic_ret && !in_low_module && !in_loaded_module && pc < 0xFF000000)) {
                    std::cout << "  [CreateInstance] PC left mapped module: PC=0x" << std::hex << pc
                              << " LR=0x" << cpu.get_reg(REG_LR)
                              << " R0=0x" << cpu.get_reg(REG_R0)
                              << " R1=0x" << cpu.get_reg(REG_R1)
                              << " R2=0x" << cpu.get_reg(REG_R2)
                              << " R3=0x" << cpu.get_reg(REG_R3)
                              << " R4=0x" << cpu.get_reg(REG_R4)
                              << " R5=0x" << cpu.get_reg(REG_R5)
                              << " R6=0x" << cpu.get_reg(REG_R6)
                              << " R7=0x" << cpu.get_reg(REG_R7)
                              << " R12=0x" << cpu.get_reg(REG_R12) << std::endl;
                    std::cout << "  [CreateInstance] recent PCs:" << std::endl;
                    int count = recent_pos < 16 ? recent_pos : 16;
                    for (int i = count; i > 0; --i) {
                        int idx = (recent_pos - i) & 15;
                        std::cout << "    PC=0x" << std::hex << recent_pc[idx]
                                  << " op=0x" << recent_op[idx] << std::endl;
                    }
                    break;
                }
                if (cpu.get_reg(REG_PC) == magic_ret || cpu.is_stopped()) break;
            }
            const bool create_instance_returned = (cpu.get_reg(REG_PC) == magic_ret || cpu.is_stopped());
            if (create_instance_returned) {
                std::cout << "IModule_CreateInstance returned!" << std::endl;
            } else {
                std::cout << "IModule_CreateInstance timed out at PC=0x" << std::hex << cpu.get_reg(REG_PC)
                          << " LR=0x" << cpu.get_reg(REG_LR) << std::endl;
            }

            uint32_t ci_result = cpu.get_reg(REG_R0);
            std::cout << "CreateInstance result: 0x" << std::hex << ci_result << std::endl;

            uint32_t applet_obj = read_guest32(ppApplet);
            shell.set_pending_applet_output_ptr(0);
            std::cout << "Applet object pointer: 0x" << std::hex << applet_obj << std::endl;

            if (applet_obj != 0 && !create_instance_returned && !allow_partial_applet) {
                std::cout << "CreateInstance did not return; not dispatching EVT_APP_START for partial applet."
                          << " Set ZEEMU_CREATEINSTANCE_ALLOW_PARTIAL_APPLET=1 only for diagnostics." << std::endl;
                applet_obj = 0;
            }

            if (applet_obj != 0) {
                shell.set_applet_object_ptr(applet_obj);
                shell.set_current_applet_cls(target_clsid);

                uint32_t applet_vtable = read_guest32(applet_obj);
                std::cout << "Applet vtable pointer: 0x" << std::hex << applet_vtable << std::endl;

                uint32_t handle_evt_ptr = read_guest32(applet_vtable + 8); // Offset 8 = HandleEvent
                std::cout << "Applet_HandleEvent function: 0x" << std::hex << handle_evt_ptr << std::endl;
                std::cout << "  applet+0x18 (m_pAppHandleEvent): 0x" << std::hex << read_guest32(applet_obj + 0x18) << std::endl;
                std::cout << "  applet+0x0c (m_pIShell):         0x" << std::hex << read_guest32(applet_obj + 0x0c) << std::endl;
                std::cout << "  applet+0x10 (m_pIModule):        0x" << std::hex << read_guest32(applet_obj + 0x10) << std::endl;
                std::cout << "  applet+0x14 (m_pIDisplay):       0x" << std::hex << read_guest32(applet_obj + 0x14) << std::endl;
                auto dump_peteca_state = [&](const char* stage) {
                    if (target_clsid != 0x0108FF18 || std::getenv("ZEEMU_TRACE_PETECA") == nullptr) {
                        return;
                    }
                    std::cout << "  peteca state [" << stage << "] applet words:";
                    for (uint32_t off = 0; off < 0x78; off += 4) {
                        std::cout << " +" << std::dec << off << "=0x" << std::hex << read_guest32(applet_obj + off);
                    }
                    std::cout << std::endl;
                    uint32_t loop_obj = read_guest32(applet_obj + 0x60);
                    uint32_t loop_vtable = loop_obj ? read_guest32(loop_obj) : 0;
                    std::cout << "  peteca state [" << stage << "] loop object=0x" << std::hex << loop_obj
                              << " vtable=0x" << loop_vtable
                              << " tick=0x" << (loop_vtable ? read_guest32(loop_vtable + 8) : 0)
                              << " draw=0x" << (loop_vtable ? read_guest32(loop_vtable + 12) : 0)
                              << std::endl;
                    std::cout << "  peteca heap globals:";
                    for (uint32_t off = 0; off < 0x18; off += 4) {
                        std::cout << " [0x" << std::hex << (0x1004D9F8u + off) << "]=0x"
                                  << read_guest32(0x1004D9F8u + off);
                    }
                    std::cout << std::endl;
                    std::cout << "  peteca small block sizes:";
                    for (uint32_t off = 0; off < 0x20; off += 4) {
                        std::cout << " [0x" << std::hex << (0x1004AF88u + off) << "]=0x"
                                  << read_guest32(0x1004AF88u + off);
                    }
                    std::cout << std::endl;
                    std::cout << "  peteca stack descriptors:";
                    for (uint32_t i = 0; i < 4; ++i) {
                        const uint32_t desc = 0x1FEFEED8u + i * 0x88u;
                        std::cout << " desc" << std::dec << i
                                  << "(+40=0x" << std::hex << read_guest32(desc + 0x40)
                                  << " +44=0x" << read_guest32(desc + 0x44)
                                  << " +48=0x" << read_guest32(desc + 0x48)
                                  << " +84=0x" << read_guest32(desc + 0x84) << ")";
                    }
                    std::cout << std::endl;
                };
                dump_peteca_state("post-create");

                if (handle_evt_ptr != 0) {
                    // Raw ROPI modules with entry == image base keep using
                    // [image_base - 4] as the BREW GET_HELPER() anchor after
                    // AEEMod_Load. Do not replace it with an IShell vtable:
                    // SDK helper veneers dispatch through this table during
                    // runtime as well as during bootstrap.
                    if (mod->entry_point == mod->base_address) {
                        patch_helper_slots();
                    }
                    std::cout << "Sending EVT_APP_START to Applet..." << std::endl;

                    GuestCallRunner guest_call(cpu, emem, magic_ret, hle_return_override, active_guest_call, active_guest_hle_count);

                    BrewThreadScheduler thread_scheduler(cpu, shell, magic_ret, hle_return_override,
                                                         active_guest_call, active_guest_hle_count);

                    auto drain_app_events = [&](uint32_t max_batches = 4) {
                        for (uint32_t batch = 0; batch < max_batches; ++batch) {
                            bool did_work = false;
                            auto pending = shell.pop_pending_app_events();
                            for (const auto& e : pending) {
                                guest_call.call(e.label.c_str(), handle_evt_ptr, applet_obj, e.event, e.wParam, e.dwParam);
                                did_work = true;
                            }
                            auto signals = shell.pop_pending_signal_callbacks();
                            for (const auto& s : signals) {
                                guest_call.call(s.label.c_str(), s.callback, s.pUser, s.arg1, s.arg2, 0);
                                did_work = true;
                            }
                            thread_scheduler.collect_pending();
                            if (!did_work) {
                                break;
                            }
                        }
                    };

                    guest_call.call("EVT_APP_START", handle_evt_ptr, applet_obj, 0, 0, 0); // EVT_APP_START = 0 for Zeebo/BREW v1.1
                    drain_app_events(16);
                    (void)thread_scheduler.run_slices(20000);
                    dump_peteca_state("post-start");
                    std::cout << "EVT_APP_START handled." << std::endl;

                    // Optional debug-only initial key press. Titles can treat
                    // synthetic input as a real state transition, so keep this
                    // behind an explicit environment variable.
                    // Zeebo/BREW v1.1: EVT_KEY = 0x100, EVT_KEY_PRESS = 0x101.
                    // This debug helper injects the Zeebo default AVK_SELECT
                    // source (right thumbstick press) through HID and BREW.
                    const bool send_initial_select = std::getenv("ZEEMU_INITIAL_SELECT") != nullptr;
                    if (send_initial_select) {
                        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                            printf("[INPUT_TRACE] synthetic initial select down/up uid=0x0106c405\n");
                        }
                        shell.push_hid_event(0x0106C405, true); // Right thumbstick press -> AVK_SELECT on Zeebo.
                        const uint32_t handled = guest_call.call("EVT_KEY_PRESS", handle_evt_ptr, applet_obj, 0x101, 0xE035, 0);
                        if (handled == 0) {
                            guest_call.call("EVT_KEY", handle_evt_ptr, applet_obj, 0x100, 0xE035, 0);
                        }
                        drain_app_events();
                        shell.push_hid_event(0x0106C405, false);
                        guest_call.call("EVT_KEY_RELEASE", handle_evt_ptr, applet_obj, 0x102, 0xE035, 0);
                        drain_app_events();
                        (void)thread_scheduler.run_slices(20000);
                    }

                    auto queue_pointer_event = [&](const char* label, uint32_t evt, float x, float y, uint32_t buttons) {
                        char pointer[128] = {};
                        std::snprintf(pointer, sizeof(pointer),
                                      "x=%08X,y=%08X,time=00000000,clkcnt=1,modifiers=%016llX,",
                                      static_cast<uint32_t>(static_cast<int32_t>(x)),
                                      static_cast<uint32_t>(static_cast<int32_t>(y)),
                                      static_cast<unsigned long long>(buttons));
                        addr_t ptr = shell.malloc(static_cast<uint32_t>(std::strlen(pointer) + 1));
                        emem.write(ptr, std::string(pointer, std::strlen(pointer) + 1));
                        guest_call.call(label, handle_evt_ptr, applet_obj, evt, static_cast<uint32_t>(std::strlen(pointer) + 1), ptr);
                        drain_app_events();
                    };

                    bool running = true;
                    bool trace_loop = std::getenv("ZEEMU_TRACE_LOOP") != nullptr;
                    const bool trace_timers = std::getenv("ZEEMU_TRACE_TIMERS") != nullptr;
                    const bool trace_input = std::getenv("ZEEMU_TRACE_INPUT") != nullptr;
                    const bool enable_mouse_pointer = std::getenv("ZEEMU_ENABLE_MOUSE_POINTER") != nullptr;
                    uint32_t loop_iter = 0;

                    auto dispatch_key_event = [&](bool down, uint16_t avk) {
                        const uint16_t evt = down ? 0x101 : 0x102;
                        if (trace_input) {
                            printf("[INPUT_TRACE] dispatch %s evt=0x%03x avk=0x%04x\n",
                                   down ? "EVT_KEY_PRESS" : "EVT_KEY_RELEASE",
                                   evt,
                                   avk);
                        }
                        const uint32_t handled = guest_call.call(down ? "EVT_KEY_PRESS" : "EVT_KEY_RELEASE",
                                                                  handle_evt_ptr,
                                                                  applet_obj,
                                                                  evt,
                                                                  avk,
                                                                  0);
                        drain_app_events();

                        // BREW samples commonly route EVT_KEY_PRESS to EVT_KEY
                        // themselves, but some titles only handle the legacy
                        // EVT_KEY code. Dispatch it only as a false-return
                        // fallback to avoid double-advancing handlers that
                        // already consumed EVT_KEY_PRESS.
                        if (down && handled == 0) {
                            if (trace_input) {
                                printf("[INPUT_TRACE] fallback dispatch EVT_KEY evt=0x100 avk=0x%04x after unhandled press\n",
                                       avk);
                            }
                            guest_call.call("EVT_KEY", handle_evt_ptr, applet_obj, 0x100, avk, 0);
                            drain_app_events();
                        }
                    };

                    auto apply_input_binding = [&](const InputBinding& binding, bool down, uint32_t device_index) {
                        switch (binding.kind) {
                            case InputBindingKind::HidButton:
                                shell.push_hid_event(binding.code, down, device_index);
                                if (device_index == 0) {
                                    if (const uint32_t keyboard_id = keyboard_id_for_binding(binding)) {
                                        shell.push_hid_keyboard_event(keyboard_id, down);
                                    }
                                }
                                if (device_index == 0 && shell.hid_default_key_events_enabled(device_index)) {
                                    if (const InputBinding* fallback = brew_key_fallback_for_hid_binding(binding)) {
                                        if (trace_input) {
                                            printf("[INPUT_TRACE] hid-button %s name=%s code=0x%08x -> avk code=0x%08x\n",
                                                   down ? "down" : "up",
                                                   binding.name,
                                                   binding.code,
                                                   fallback->code);
                                        }
                                        dispatch_key_event(down, static_cast<uint16_t>(fallback->code));
                                    }
                                } else if (trace_input && device_index == 0) {
                                    printf("[INPUT_TRACE] hid-button %s name=%s code=0x%08x -> avk suppressed exclusive-level\n",
                                           down ? "down" : "up",
                                           binding.name,
                                           binding.code);
                                }
                                break;
                            case InputBindingKind::HidAxis:
                                shell.set_hid_axis(binding.code, down ? binding.down_value : 0x8000u, device_index);
                                if (device_index == 0) {
                                    if (const uint32_t keyboard_id = keyboard_id_for_binding(binding)) {
                                        shell.push_hid_keyboard_event(keyboard_id, down);
                                    }
                                    const InputBinding* dpad_binding = nullptr;
                                    if (binding.code == 0x0106C4D0) {
                                        dpad_binding = named_input_binding(binding.down_value == 0 ? "left" : "right");
                                    } else if (binding.code == 0x0106C4D1) {
                                        dpad_binding = named_input_binding(binding.down_value == 0 ? "up" : "down");
                                    }
                                    if (dpad_binding) {
                                        if (trace_input) {
                                            printf("[INPUT_TRACE] left-stick-axis %s name=%s -> dpad name=%s\n",
                                                   down ? "down" : "up",
                                                   binding.name,
                                                   dpad_binding->name);
                                        }
                                        shell.push_hid_event(dpad_binding->code, down, device_index);
                                        if (const uint32_t dpad_keyboard_id = keyboard_id_for_binding(*dpad_binding)) {
                                            shell.push_hid_keyboard_event(dpad_keyboard_id, down);
                                        }
                                        if (shell.hid_default_key_events_enabled(device_index)) {
                                            if (const InputBinding* fallback = brew_key_fallback_for_hid_binding(*dpad_binding)) {
                                                dispatch_key_event(down, static_cast<uint16_t>(fallback->code));
                                            }
                                        }
                                    }
                                }
                                break;
                            case InputBindingKind::BrewKey:
                                if (device_index == 0) {
                                    if (const uint32_t keyboard_id = keyboard_id_for_binding(binding)) {
                                        shell.push_hid_keyboard_event(keyboard_id, down);
                                    }
                                }
                                if (device_index == 0) {
                                    dispatch_key_event(down, static_cast<uint16_t>(binding.code));
                                }
                                break;
                        }
                    };

                    std::vector<ScriptedInputEvent> scripted_inputs = parse_scripted_input(std::getenv("ZEEMU_INPUT_SCRIPT"));
                    size_t next_scripted_input = 0;
                    if (!scripted_inputs.empty()) {
                        std::cout << "Scripted input events: " << std::dec << scripted_inputs.size() << std::endl;
                    }
                    const KeyboardProfile keyboard_profile = keyboard_profile_from_env();
                    std::cout << "Keyboard input profile: " << keyboard_profile_name(keyboard_profile) << std::endl;
                    const GamepadProfile gamepad_profile = gamepad_profile_from_env();
                    std::cout << "Gamepad input profile: " << gamepad_profile_name(gamepad_profile) << std::endl;

                    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> open_gamepads;
                    std::unordered_map<SDL_JoystickID, GamepadRuntimeState> gamepad_states;
                    std::unordered_map<SDL_JoystickID, SDL_Joystick*> open_joysticks;
                    std::unordered_map<SDL_JoystickID, GamepadRuntimeState> joystick_states;
                    auto open_gamepad = [&](SDL_JoystickID id) {
                        if (gamepad_profile == GamepadProfile::Off || open_gamepads.find(id) != open_gamepads.end()) {
                            return;
                        }
                        SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
                        if (!gamepad) {
                            if (trace_input) {
                                printf("[INPUT_TRACE] gamepad open failed id=%d error=%s\n",
                                       static_cast<int>(id),
                                       SDL_GetError());
                            }
                            return;
                        }
                        open_gamepads[id] = gamepad;
                        gamepad_states.try_emplace(id);
                        std::cout << "Opened gamepad " << static_cast<int>(id) << ": "
                                  << (SDL_GetGamepadName(gamepad) ? SDL_GetGamepadName(gamepad) : "unknown")
                                  << std::endl;
                    };

                    auto open_joystick_fallback = [&](SDL_JoystickID id) {
                        if (gamepad_profile == GamepadProfile::Off ||
                            open_joysticks.find(id) != open_joysticks.end() ||
                            open_gamepads.find(id) != open_gamepads.end() ||
                            SDL_IsGamepad(id)) {
                            return;
                        }
                        SDL_Joystick* joystick = SDL_OpenJoystick(id);
                        if (!joystick) {
                            if (trace_input) {
                                printf("[INPUT_TRACE] joystick open failed id=%d error=%s\n",
                                       static_cast<int>(id),
                                       SDL_GetError());
                            }
                            return;
                        }
                        open_joysticks[id] = joystick;
                        GamepadRuntimeState& state = joystick_states[id];
                        state.axis_count = SDL_GetNumJoystickAxes(joystick);
                        state.button_count = SDL_GetNumJoystickButtons(joystick);
                        state.hat_count = SDL_GetNumJoystickHats(joystick);
                        std::cout << "Opened joystick fallback " << static_cast<int>(id) << ": "
                                  << (SDL_GetJoystickName(joystick) ? SDL_GetJoystickName(joystick) : "unknown")
                                  << " axes=" << state.axis_count
                                  << " buttons=" << state.button_count
                                  << " hats=" << state.hat_count
                                  << std::endl;
                    };

                    auto close_gamepad = [&](SDL_JoystickID id) {
                        auto it = open_gamepads.find(id);
                        if (it != open_gamepads.end()) {
                            SDL_CloseGamepad(it->second);
                            open_gamepads.erase(it);
                        }
                        gamepad_states.erase(id);
                    };

                    auto close_joystick_fallback = [&](SDL_JoystickID id) {
                        auto it = open_joysticks.find(id);
                        if (it != open_joysticks.end()) {
                            SDL_CloseJoystick(it->second);
                            open_joysticks.erase(it);
                        }
                        joystick_states.erase(id);
                    };

                    shell.set_hid_rumble_callback([&](uint32_t device_index, uint32_t left, uint32_t right) {
                        if (gamepad_profile == GamepadProfile::Off || (open_gamepads.empty() && open_joysticks.empty())) {
                            return;
                        }
                        const Uint32 duration_ms = (left != 0 || right != 0) ? 60000u : 0u;
                        bool ok = false;
                        const char* api = "SDL_RumbleGamepad";
                        if (!open_gamepads.empty()) {
                            auto it = open_gamepads.begin();
                            std::advance(it, std::min<size_t>(device_index, open_gamepads.size() - 1));
                            ok = SDL_RumbleGamepad(it->second,
                                                   static_cast<Uint16>(left),
                                                   static_cast<Uint16>(right),
                                                   duration_ms);
                        } else {
                            auto it = open_joysticks.begin();
                            std::advance(it, std::min<size_t>(device_index, open_joysticks.size() - 1));
                            ok = SDL_RumbleJoystick(it->second,
                                                    static_cast<Uint16>(left),
                                                    static_cast<Uint16>(right),
                                                    duration_ms);
                            api = "SDL_RumbleJoystick";
                        }
                        if (trace_input) {
                            printf("[INPUT_TRACE] %s dev=%u left=%u right=%u duration=%u ok=%u error=%s\n",
                                   api,
                                   device_index,
                                   left,
                                   right,
                                   duration_ms,
                                   ok ? 1u : 0u,
                                   ok ? "" : SDL_GetError());
                        }
                    });

                    if (gamepad_profile != GamepadProfile::Off) {
                        SDL_SetGamepadEventsEnabled(true);
                        SDL_SetJoystickEventsEnabled(true);
                        if (const char* mapping = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING"); mapping && *mapping) {
                            const int result = SDL_AddGamepadMapping(mapping);
                            if (trace_input) {
                                printf("[INPUT_TRACE] SDL_AddGamepadMapping result=%d error=%s\n",
                                       result,
                                       result < 0 ? SDL_GetError() : "");
                            }
                        }
                        if (const char* mapping_file = std::getenv("ZEEMU_SDL_GAMEPAD_MAPPING_FILE"); mapping_file && *mapping_file) {
                            const int result = SDL_AddGamepadMappingsFromFile(mapping_file);
                            if (trace_input) {
                                printf("[INPUT_TRACE] SDL_AddGamepadMappingsFromFile path=%s result=%d error=%s\n",
                                       mapping_file,
                                       result,
                                       result < 0 ? SDL_GetError() : "");
                            }
                        }
                        int gamepad_count = 0;
                        SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&gamepad_count);
                        if (gamepad_ids) {
                            for (int i = 0; i < gamepad_count; ++i) {
                                open_gamepad(gamepad_ids[i]);
                            }
                            SDL_free(gamepad_ids);
                        }
                        int joystick_count = 0;
                        SDL_JoystickID* joystick_ids = SDL_GetJoysticks(&joystick_count);
                        if (joystick_ids) {
                            for (int i = 0; i < joystick_count; ++i) {
                                open_joystick_fallback(joystick_ids[i]);
                            }
                            SDL_free(joystick_ids);
                        }
                    }

                    auto apply_mapped_gamepad_button = [&](SDL_JoystickID id, SDL_GamepadButton button, bool down, const char* source) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (button == SDL_GAMEPAD_BUTTON_INVALID) {
                            if (trace_input) {
                                printf("[INPUT_TRACE] %s button ignored id=%d invalid mapping\n",
                                       source,
                                       static_cast<int>(id));
                            }
                            return;
                        }
                        const auto bindings = input_bindings_for_gamepad_button(button, gamepad_profile);
                        if (trace_input) {
                            printf("[INPUT_TRACE] %s button %s id=%d button=%d profile=%s bindings=%zu\n",
                                   source,
                                   down ? "down" : "up",
                                   static_cast<int>(id),
                                   static_cast<int>(button),
                                   gamepad_profile_name(gamepad_profile),
                                   bindings.size());
                        }
                        for (const InputBinding* binding : bindings) {
                            if (trace_input) {
                                printf("[INPUT_TRACE]   -> kind=%s code=0x%08x\n",
                                       input_binding_kind_name(binding->kind),
                                       binding->code);
                            }
                            apply_input_binding(*binding, down, 0);
                        }
                    };

                    auto apply_gamepad_button = [&](SDL_JoystickID id, SDL_GamepadButton button, bool down) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (open_gamepads.find(id) == open_gamepads.end()) {
                            open_gamepad(id);
                        }
                        if (auto it = gamepad_states.find(id); it != gamepad_states.end() &&
                            button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT) {
                            if (it->second.gamepad_buttons[static_cast<size_t>(button)] == down) {
                                return;
                            }
                            it->second.gamepad_buttons[static_cast<size_t>(button)] = down;
                        }
                        apply_mapped_gamepad_button(id, button, down, "gamepad");
                    };

                    auto apply_stick_dpad_binding = [&](bool& current,
                                                        bool next,
                                                        const char* binding_name,
                                                        const char* source,
                                                        SDL_JoystickID id) {
                        if (current == next) {
                            return;
                        }
                        current = next;
                        if (trace_input) {
                            printf("[INPUT_TRACE] %s left-stick-as-dpad id=%d %s %s\n",
                                   source,
                                   static_cast<int>(id),
                                   binding_name,
                                   next ? "down" : "up");
                        }
                        if (const InputBinding* binding = named_input_binding(binding_name)) {
                            apply_input_binding(*binding, next, 0);
                        }
                    };

                    auto update_left_stick_dpad = [&](SDL_JoystickID id,
                                                       SDL_GamepadAxis axis,
                                                       Sint16 value,
                                                       GamepadRuntimeState& state,
                                                       const char* source) {
                        constexpr Sint16 kDigitalThreshold = 12000;
                        if (axis == SDL_GAMEPAD_AXIS_LEFTX) {
                            const bool left = value < -kDigitalThreshold;
                            const bool right = value > kDigitalThreshold;
                            apply_stick_dpad_binding(state.left_stick_right, right, "right", source, id);
                            apply_stick_dpad_binding(state.left_stick_left, left, "left", source, id);
                        } else if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
                            const bool up = value < -kDigitalThreshold;
                            const bool down = value > kDigitalThreshold;
                            apply_stick_dpad_binding(state.left_stick_down, down, "down", source, id);
                            apply_stick_dpad_binding(state.left_stick_up, up, "up", source, id);
                        }
                    };

                    auto apply_mapped_gamepad_axis = [&](SDL_JoystickID id,
                                                         SDL_GamepadAxis axis,
                                                         Sint16 value,
                                                         GamepadRuntimeState& state,
                                                         const char* source) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }

                        if (gamepad_profile == GamepadProfile::Standard) {
                            if (trace_input) {
                                printf("[INPUT_TRACE] %s axis id=%d axis=%d value=%d\n",
                                       source,
                                       static_cast<int>(id),
                                       static_cast<int>(axis),
                                       static_cast<int>(value));
                            }
                            switch (axis) {
                                case SDL_GAMEPAD_AXIS_LEFTX:
                                    shell.set_hid_axis(0x0106C4D0, gamepad_axis_to_hid_value(value), 0);
                                    update_left_stick_dpad(id, axis, value, state, source);
                                    break;
                                case SDL_GAMEPAD_AXIS_LEFTY:
                                    shell.set_hid_axis(0x0106C4D1, gamepad_axis_to_hid_value(value), 0);
                                    update_left_stick_dpad(id, axis, value, state, source);
                                    break;
                                case SDL_GAMEPAD_AXIS_RIGHTX:
                                    shell.set_hid_axis(0x0106C4CE, gamepad_axis_to_hid_value(value), 0);
                                    break;
                                case SDL_GAMEPAD_AXIS_RIGHTY:
                                    shell.set_hid_axis(0x0106C4CF, gamepad_axis_to_hid_value(value), 0);
                                    break;
                                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
                                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
                                    bool& pressed = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                        ? state.left_trigger_down
                                        : state.right_trigger_down;
                                    const bool now_pressed = value > 12000;
                                    if (pressed != now_pressed) {
                                        pressed = now_pressed;
                                        if (const InputBinding* binding = gamepad_trigger_binding(axis)) {
                                            apply_input_binding(*binding, now_pressed, 0);
                                        }
                                    }
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                    };

                    auto apply_gamepad_axis = [&](SDL_JoystickID id, SDL_GamepadAxis axis, Sint16 value) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (open_gamepads.find(id) == open_gamepads.end()) {
                            open_gamepad(id);
                        }
                        if (auto it = gamepad_states.find(id); it != gamepad_states.end() &&
                            axis >= 0 && axis < SDL_GAMEPAD_AXIS_COUNT) {
                            if (it->second.gamepad_axes[static_cast<size_t>(axis)] == value) {
                                return;
                            }
                            it->second.gamepad_axes[static_cast<size_t>(axis)] = value;
                        }
                        apply_mapped_gamepad_axis(id, axis, value, gamepad_states[id], "gamepad");
                    };

                    auto apply_joystick_combined_triggers = [&](Sint16 value, GamepadRuntimeState& state) {
                        constexpr Sint16 kTriggerThreshold = 12000;
                        const bool left_pressed = value < -kTriggerThreshold;
                        const bool right_pressed = value > kTriggerThreshold;
                        if (state.left_trigger_down != left_pressed) {
                            state.left_trigger_down = left_pressed;
                            if (const InputBinding* binding = gamepad_trigger_binding(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
                                apply_input_binding(*binding, left_pressed, 0);
                            }
                        }
                        if (state.right_trigger_down != right_pressed) {
                            state.right_trigger_down = right_pressed;
                            if (const InputBinding* binding = gamepad_trigger_binding(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
                                apply_input_binding(*binding, right_pressed, 0);
                            }
                        }
                    };

                    auto apply_joystick_axis = [&](SDL_JoystickID id, int raw_axis, Sint16 value) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (open_joysticks.find(id) == open_joysticks.end()) {
                            open_joystick_fallback(id);
                            if (open_joysticks.find(id) == open_joysticks.end()) {
                                return;
                            }
                        }
                        GamepadRuntimeState& state = joystick_states[id];
                        if (raw_axis >= 0 && raw_axis < static_cast<int>(state.joystick_axes.size())) {
                            if (state.joystick_axes[static_cast<size_t>(raw_axis)] == value) {
                                return;
                            }
                            state.joystick_axes[static_cast<size_t>(raw_axis)] = value;
                        }
                        if (trace_input) {
                            printf("[INPUT_TRACE] joystick axis id=%d raw_axis=%d value=%d axes=%d\n",
                                   static_cast<int>(id),
                                   raw_axis,
                                   static_cast<int>(value),
                                   state.axis_count);
                        }
                        switch (raw_axis) {
                            case 0:
                                apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_LEFTX, value, state, "joystick");
                                break;
                            case 1:
                                apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_LEFTY, value, state, "joystick");
                                break;
                            case 2:
                                if (state.axis_count >= 6) {
                                    apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, value, state, "joystick");
                                } else {
                                    apply_joystick_combined_triggers(value, state);
                                }
                                break;
                            case 3:
                                apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_RIGHTX, value, state, "joystick");
                                break;
                            case 4:
                                apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_RIGHTY, value, state, "joystick");
                                break;
                            case 5:
                                apply_mapped_gamepad_axis(id, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, value, state, "joystick");
                                break;
                            default:
                                break;
                        }
                    };

                    auto apply_joystick_hat = [&](SDL_JoystickID id, uint8_t value) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (open_joysticks.find(id) == open_joysticks.end()) {
                            open_joystick_fallback(id);
                            if (open_joysticks.find(id) == open_joysticks.end()) {
                                return;
                            }
                        }
                        GamepadRuntimeState& state = joystick_states[id];
                        const uint8_t previous = state.hat;
                        state.hat = value;
                        constexpr uint8_t kDirections[] = { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT };
                        for (uint8_t dir : kDirections) {
                            const bool was_down = (previous & dir) != 0;
                            const bool is_down = (value & dir) != 0;
                            if (was_down != is_down) {
                                apply_mapped_gamepad_button(id, dpad_button_for_hat(dir), is_down, "joystick-hat");
                            }
                        }
                    };

                    auto apply_joystick_button = [&](SDL_JoystickID id, int raw_button, bool down) {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        if (open_joysticks.find(id) == open_joysticks.end()) {
                            open_joystick_fallback(id);
                            if (open_joysticks.find(id) == open_joysticks.end()) {
                                return;
                            }
                        }
                        if (raw_button >= 0 && raw_button < static_cast<int>(joystick_states[id].joystick_buttons.size())) {
                            if (joystick_states[id].joystick_buttons[static_cast<size_t>(raw_button)] == down) {
                                return;
                            }
                            joystick_states[id].joystick_buttons[static_cast<size_t>(raw_button)] = down;
                        }
                        apply_mapped_gamepad_button(id, xbox_layout_joystick_button(raw_button), down, "joystick");
                    };

                    auto poll_open_gamepads = [&]() {
                        if (gamepad_profile == GamepadProfile::Off) {
                            return;
                        }
                        SDL_UpdateGamepads();
                        for (auto& entry : open_gamepads) {
                            SDL_JoystickID id = entry.first;
                            SDL_Gamepad* gamepad = entry.second;
                            GamepadRuntimeState& state = gamepad_states[id];
                            for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
                                const bool down = SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(i));
                                if (state.gamepad_buttons[static_cast<size_t>(i)] != down) {
                                    state.gamepad_buttons[static_cast<size_t>(i)] = down;
                                    apply_mapped_gamepad_button(id, static_cast<SDL_GamepadButton>(i), down, "gamepad-poll");
                                }
                            }
                            for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
                                const Sint16 value = SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(i));
                                if (state.gamepad_axes[static_cast<size_t>(i)] != value) {
                                    state.gamepad_axes[static_cast<size_t>(i)] = value;
                                    apply_mapped_gamepad_axis(id, static_cast<SDL_GamepadAxis>(i), value, state, "gamepad-poll");
                                }
                            }
                        }
                        for (auto& entry : open_joysticks) {
                            SDL_JoystickID id = entry.first;
                            SDL_Joystick* joystick = entry.second;
                            GamepadRuntimeState& state = joystick_states[id];
                            const int button_limit = std::min<int>(state.button_count, state.joystick_buttons.size());
                            for (int i = 0; i < button_limit; ++i) {
                                const bool down = SDL_GetJoystickButton(joystick, i);
                                if (state.joystick_buttons[static_cast<size_t>(i)] != down) {
                                    state.joystick_buttons[static_cast<size_t>(i)] = down;
                                    apply_mapped_gamepad_button(id, xbox_layout_joystick_button(i), down, "joystick-poll");
                                }
                            }
                            const int axis_limit = std::min<int>(state.axis_count, state.joystick_axes.size());
                            for (int i = 0; i < axis_limit; ++i) {
                                const Sint16 value = SDL_GetJoystickAxis(joystick, i);
                                if (state.joystick_axes[static_cast<size_t>(i)] != value) {
                                    apply_joystick_axis(id, i, value);
                                }
                            }
                            if (state.hat_count > 0) {
                                const uint8_t value = SDL_GetJoystickHat(joystick, 0);
                                if (state.hat != value) {
                                    apply_joystick_hat(id, value);
                                }
                            }
                        }
                    };

                    std::vector<uint8_t> frame_staging(
                        static_cast<size_t>(display_profile.width) *
                        static_cast<size_t>(display_profile.height) * 2u);
                    if (trace_loop) {
                        std::cout << "Entering app loop." << std::endl;
                    }
                    while (running) {
                        if (trace_loop) {
                            std::cout << "Loop " << std::dec << loop_iter << ": poll" << std::endl;
                        }
                        drain_app_events(1);

                        const uint64_t input_now_ms = shell.uptime_ms();
                        while (next_scripted_input < scripted_inputs.size() &&
                               scripted_inputs[next_scripted_input].at_ms <= input_now_ms) {
                            const ScriptedInputEvent& input = scripted_inputs[next_scripted_input++];
                            if (input.binding) {
                                if (trace_input) {
                                    printf("[INPUT_TRACE] scripted %s dev=%u t=%llu name=%s kind=%s code=0x%08x value=0x%08x now=%llu\n",
                                           input.down ? "down" : "up",
                                           input.device_index,
                                           static_cast<unsigned long long>(input.at_ms),
                                           input.binding->name,
                                           input_binding_kind_name(input.binding->kind),
                                           input.binding->code,
                                           input.down ? input.binding->down_value : 0x8000u,
                                           static_cast<unsigned long long>(input_now_ms));
                                }
                                apply_input_binding(*input.binding, input.down, input.device_index);
                            }
                        }

                        SDL_Event event;
                        while (SDL_PollEvent(&event)) {
                            if (event.type == SDL_EVENT_QUIT) {
                                running = false;
                            } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                                bool down = (event.type == SDL_EVENT_KEY_DOWN);
                                const auto bindings = input_bindings_for_sdl_event(event.key.key, event.key.mod, keyboard_profile);
                                if (trace_input) {
                                    printf("[INPUT_TRACE] SDL key %s key=0x%08x profile=%s bindings=%zu\n",
                                           down ? "down" : "up",
                                           static_cast<uint32_t>(event.key.key),
                                           keyboard_profile_name(keyboard_profile),
                                           bindings.size());
                                }
                                for (const InputBinding* binding : bindings) {
                                    if (trace_input) {
                                        printf("[INPUT_TRACE]   -> kind=%s code=0x%08x\n",
                                               input_binding_kind_name(binding->kind),
                                               binding->code);
                                    }
                                    apply_input_binding(*binding, down, 0);
                                }
                            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                                open_gamepad(event.gdevice.which);
                            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                                close_gamepad(event.gdevice.which);
                            } else if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
                                if (SDL_IsGamepad(event.jdevice.which)) {
                                    open_gamepad(event.jdevice.which);
                                } else {
                                    open_joystick_fallback(event.jdevice.which);
                                }
                            } else if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
                                close_joystick_fallback(event.jdevice.which);
                            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                                apply_gamepad_button(event.gbutton.which,
                                                     static_cast<SDL_GamepadButton>(event.gbutton.button),
                                                     event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                                apply_gamepad_axis(event.gaxis.which,
                                                   static_cast<SDL_GamepadAxis>(event.gaxis.axis),
                                                   event.gaxis.value);
                            } else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
                                apply_joystick_button(event.jbutton.which,
                                                      event.jbutton.button,
                                                      event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN);
                            } else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
                                apply_joystick_axis(event.jaxis.which,
                                                    event.jaxis.axis,
                                                    event.jaxis.value);
                            } else if (event.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                                apply_joystick_hat(event.jhat.which, event.jhat.value);
                            } else if (enable_mouse_pointer && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
                                bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                                const uint32_t buttons = event.button.button == SDL_BUTTON_LEFT ? 1u : 0u;
                                queue_pointer_event(down ? "EVT_POINTER_DOWN" : "EVT_POINTER_UP",
                                                    down ? 0x240u : 0x242u,
                                                    event.button.x, event.button.y, buttons);
                                if (trace_input) {
                                    printf("[INPUT_TRACE] mouse button %s -> synthetic select evt=0x%03x avk=0xe035\n",
                                           down ? "down" : "up",
                                           down ? 0x101u : 0x102u);
                                }
                                dispatch_key_event(down, 0xE035u);
                            } else if (enable_mouse_pointer && event.type == SDL_EVENT_MOUSE_MOTION) {
                                queue_pointer_event("EVT_POINTER_MOVE", 0x241u, event.motion.x, event.motion.y, event.motion.state);
                            }
                        }
                        poll_open_gamepads();
                        drain_app_events(16);

                        // Fire expired timers (PFNNOTIFY: R0=pUser, no other args).
                        // GETUPTIMEMS is a real millisecond uptime source; do
                        // not advance it by frame count. QXEngine and BREW
                        // game loops compute elapsed seconds from this value.
                        // Still drain a bounded batch so zero-delay callback
                        // chains progress promptly, but keep one fixed poll
                        // cutoff. Timers scheduled by a callback should not
                        // become reentrantly expired just because emulating
                        // the previous callback consumed host milliseconds.
                        const auto timer_poll_ms = shell.uptime_ms();
                        uint32_t fired_timers = 0;
                        bool did_thread_work = false;
                        while (fired_timers < 64) {
                            if (trace_loop && fired_timers == 0) {
                                std::cout << "Loop " << std::dec << loop_iter << ": timers now=0x"
                                          << std::hex << timer_poll_ms << std::endl;
                            }
                            auto timers = shell.pop_expired_timers(timer_poll_ms);
                            if (timers.empty()) {
                                break;
                            }
                            for (auto& t : timers) {
                                if (trace_loop || trace_timers) {
                                    printf("Firing timer pfn=0x%08x pUser=0x%08x\n", t.pfn, t.pUser);
                                    fflush(stdout);
                                }
                                guest_call.call("timer", t.pfn, t.pUser, 0, 0, 0);
                                drain_app_events();
                                did_thread_work = thread_scheduler.run_slices(20000) || did_thread_work;
                                ++fired_timers;
                                if (fired_timers >= 64) {
                                    break;
                                }
                            }
                        }

                        did_thread_work = thread_scheduler.run_slices(50000) || did_thread_work;
                        const uint32_t idle_delay_ms = (fired_timers != 0 || did_thread_work) ? 0u : 16u;

                        // Guest GL paths present through eglSwapBuffers. If a
                        // GL frame was presented during this loop, do not
                        // immediately overwrite it with the BREW device bitmap.
                        if (presenter->consume_guest_gl_presented()) {
                            ++loop_iter;
                            SDL_Delay(idle_delay_ms);
                            continue;
                        }
                        if (presenter->has_guest_gl_frame()) {
                            if (trace_loop) {
                                std::cout << "Loop " << std::dec << loop_iter
                                          << ": preserve guest GL frame" << std::endl;
                            }
                            ++loop_iter;
                            SDL_Delay(idle_delay_ms);
                            continue;
                        }

                        // Render guest framebuffer.
                        if (trace_loop) {
                            std::cout << "Loop " << std::dec << loop_iter << ": begin_frame" << std::endl;
                        }
                        presenter->begin_frame();
                        bool presented_guest_frame = false;
                        BrewDisplay* display = shell.get_display();
                        if (display) {
                            BrewBitmap* bmp = display->get_device_bitmap();
                            if (bmp) {
                                if (trace_loop) {
                                    std::cout << "Loop " << std::dec << loop_iter
                                              << ": framebuffer guest=0x" << std::hex
                                              << bmp->get_buffer_ptr() << std::endl;
                                }
                                void* host_ptr = vmem.get_host_address(bmp->get_buffer_ptr());
                                if (host_ptr) {
                                    if (trace_loop) {
                                        std::cout << "Loop " << std::dec << loop_iter
                                                  << ": present host=" << host_ptr << std::endl;
                                    }
                                    presenter->present_rgb565(host_ptr, bmp->get_pitch(),
                                                              bmp->get_width(), bmp->get_height());
                                    presented_guest_frame = true;
                                }
                            }
                        }
                        if (!presented_guest_frame) {
                            fill_placeholder_frame(frame_staging, display_profile.width, display_profile.height, loop_iter);
                            presenter->present_rgb565(frame_staging.data(), display_profile.width * 2);
                        }
                        if (trace_loop) {
                            std::cout << "Loop " << std::dec << loop_iter << ": end_frame" << std::endl;
                        }
                        presenter->end_frame();
                        ++loop_iter;
                        SDL_Delay(idle_delay_ms);
                    }
                    for (auto& entry : open_gamepads) {
                        SDL_CloseGamepad(entry.second);
                    }
                    open_gamepads.clear();
                    for (auto& entry : open_joysticks) {
                        SDL_CloseJoystick(entry.second);
                    }
                    open_joysticks.clear();
                }
            }
        }
    }
    
    presenter.reset();
    SDL_Quit();
    return 0;
}
