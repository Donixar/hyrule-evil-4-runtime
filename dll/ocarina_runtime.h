// dll/ocarina_runtime.h
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "ocarina_input.h"
// Pure pieces of the Task 10 wiring (link_runtime.cpp owns hooks, memory and audio):
// the [Ocarina] INI switches and bindings, the clip chain the dispatch continues inside the
// player handler, the load epoch the worker publishes, the sticky focus-loss latch, the
// costume -> mount asset map and the raw input assembled from keys and an XInput pad.
// Nothing here reads game memory or calls the OS.
namespace link_runtime::ocarina::runtime {

// Virtual-key codes. Defaults are Donald's 11-oct table (spec, "Controles").
struct Bindings {
    uint8_t toggle = 0x4F;  // O
    uint8_t stow = 0x44;    // D
    uint8_t note_a = 0x41;  // A
    uint8_t up = 0x26, down = 0x28, left = 0x25, right = 0x27;  // arrows
};

// Every switch is off unless the INI holds exactly "1" (like ChapterIniSwitch). Enabled is the
// master: with it 0 nothing of the ocarina is created, hooked or opened. The other switches
// stage the in-game tests one piece at a time; a piece that is off is a successful no-op for
// the session (Freeze=0 freezes nothing, Pose=0 plays no clip, Notes=0 plays no sound).
struct Config {
    bool enabled = false, item = false, freeze = false, mount = false, pose = false, notes = false, pad = false;
    // OcarinaPadStyle (18-oct, Donald): n64 (default) = C notes on the RIGHT stick, A note on A, B puts it away, L+R
    // (LB+RB) takes it out or away; the left stick plays nothing. legacy = the 11-oct mapping (notes on the left stick).
    bool pad_n64 = true;
    unsigned volume = 35;
    Bindings keys;
};

inline bool ExactOne(const wchar_t* value) { return value && value[0] == L'1' && value[1] == 0; }

// "O", "7", "Up", "Down", "Left", "Right" or "0xNN" (1..0xFE); -1 for anything else.
inline int ParseKey(const wchar_t* v) {
    if (!v || !v[0]) return -1;
    if (!v[1]) {
        const wchar_t c = v[0];
        if (c >= L'A' && c <= L'Z') return int(c);
        if (c >= L'a' && c <= L'z') return int(c - L'a' + L'A');
        if (c >= L'0' && c <= L'9') return int(c);
        return -1;
    }
    struct Named { const wchar_t* name; int vk; };
    static const Named names[] = {{L"Up", 0x26}, {L"Down", 0x28}, {L"Left", 0x25}, {L"Right", 0x27}};
    for (const auto& n : names) {
        const wchar_t* a = n.name; const wchar_t* b = v;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*a && !*b) return n.vk;
    }
    if (v[0] == L'0' && (v[1] == L'x' || v[1] == L'X') && v[2]) {
        int value = 0;
        for (const wchar_t* p = v + 2; *p; ++p) {
            int d = *p >= L'0' && *p <= L'9' ? *p - L'0' : *p >= L'a' && *p <= L'f' ? *p - L'a' + 10 :
                    *p >= L'A' && *p <= L'F' ? *p - L'A' + 10 : -1;
            if (d < 0 || p - v > 4) return -1;
            value = value * 16 + d;
        }
        return value >= 1 && value <= 0xFE ? value : -1;
    }
    return -1;
}

// Get(name, buffer, count) fills buffer with the INI value or "" (GetPrivateProfileStringW shape).
// Invalid or clashing bindings keep every default and count one problem each.
template <class Get>
Config ReadConfig(Get get, unsigned& problems) {
    problems = 0;
    Config c;
    wchar_t value[32] = {};
    auto flag = [&](const wchar_t* name) { value[0] = 0; get(name, value, 32); return ExactOne(value); };
    c.enabled = flag(L"Enabled");
    c.item = flag(L"Item");
    c.freeze = flag(L"Freeze");
    c.mount = flag(L"Mount");
    c.pose = flag(L"Pose");
    c.notes = flag(L"Notes");
    c.pad = flag(L"Pad");
    value[0] = 0; get(L"NoteVolumePercent", value, 32);
    if (value[0]) {
        unsigned v = 0; bool ok = true;
        for (const wchar_t* p = value; *p; ++p) { if (*p < L'0' || *p > L'9' || v > 100) { ok = false; break; } v = v * 10 + unsigned(*p - L'0'); }
        if (ok && v <= 100) c.volume = v; else ++problems;
    }
    Bindings keys;
    struct Slot { const wchar_t* name; uint8_t* field; };
    Slot slots[] = {{L"KeyToggle", &keys.toggle}, {L"KeyStow", &keys.stow}, {L"KeyNoteA", &keys.note_a},
                    {L"KeyUp", &keys.up}, {L"KeyDown", &keys.down}, {L"KeyLeft", &keys.left}, {L"KeyRight", &keys.right}};
    bool keys_ok = true;
    for (auto& s : slots) {
        value[0] = 0; get(s.name, value, 32);
        if (!value[0]) continue;
        const int vk = ParseKey(value);
        if (vk < 0) { keys_ok = false; ++problems; continue; }
        *s.field = uint8_t(vk);
    }
    for (size_t i = 0; i < 7 && keys_ok; ++i)
        for (size_t j = i + 1; j < 7; ++j)
            if (*slots[i].field == *slots[j].field) { keys_ok = false; ++problems; break; }
    if (keys_ok) c.keys = keys;
    value[0] = 0; get(L"OcarinaPadStyle", value, 32);
    if (value[0]) {
        auto same = [&](const wchar_t* w) { const wchar_t* a = value; for (; *a && *w; ++a, ++w) { wchar_t x = *a >= L'A' && *a <= L'Z' ? wchar_t(*a + 32) : *a; if (x != *w) return false; } return !*a && !*w; };
        if (same(L"n64")) c.pad_n64 = true; else if (same(L"legacy")) c.pad_n64 = false; else ++problems;
    }
    // Freeze without Pose would leave Link free to walk into a door or an event while the
    // world is suspended (review I4): refused, counted as one problem.
    if (c.freeze && !c.pose) { c.freeze = false; ++problems; }
    return c;
}

// Which clip the dispatch lease is playing. The entry clip ends into the loop (seamless, no
// return to idle); the loop restarts itself from frame 0; only the exit clip really ends.
enum class Clip : uint8_t { None, Enter, Loop, Exit };
class ClipChain {
    Clip current_ = Clip::None; bool finished_ = false;
public:
    Clip Current() const { return current_; }
    bool Finished() const { return finished_; }
    void Started(Clip c) { current_ = c; finished_ = false; }
    // Inside the handler, when the owned clip reports its end: the clip to continue with
    // (Clip::None lets it end through the native finish path).
    Clip OnEnd() {
        if (current_ == Clip::Enter) { finished_ = true; current_ = Clip::Loop; return Clip::Loop; }
        if (current_ == Clip::Loop) return Clip::Loop;
        return Clip::None;
    }
    // The lease ended: Finished (exit clip done) or lost (cancelled or abandoned).
    void Ended(bool finished_exit) { finished_ = finished_exit && current_ == Clip::Exit; current_ = Clip::None; }
    void Reset() { current_ = Clip::None; finished_ = false; }
};

// Which lifecycle events are the ocarina's (review I1). Arm the token BEFORE Runtime::Start:
// Start publishes Started from inside, so a token set afterwards would route that first event
// to Mortal Draw (its voice and log). Disarm when the start fails or the lease ends.
class LeaseRoute {
    uint64_t token_ = 0;
public:
    void Arm(uint64_t token) { token_ = token; }
    void Disarm() { token_ = 0; }
    uint64_t Token() const { return token_; }
    bool Ocarina(uint64_t token) const { return token && token == token_; }
};

// Knife-hold suppression (review I3). Held from the sheathe request / freeze until Ended; after
// Ended it stays on while the knife button (KEY_KAMAE_KNIFE 0x800 in ON) is still down, so a
// button held across the stow cannot ready the sword by itself, like the parry gate.
class KnifeLatch {
    enum class Mode : uint8_t { Off, Session, UntilRelease } mode_ = Mode::Off;
public:
    static constexpr uint32_t kKnife = 0x800;
    void Hold() { mode_ = Mode::Session; }
    void Ended() { if (mode_ == Mode::Session) mode_ = Mode::UntilRelease; }
    bool Holding() const { return mode_ != Mode::Off; }
    // Once per player-handler frame with the current ON word: true = hide 0x800 this frame.
    bool Suppress(uint32_t on) {
        if (mode_ == Mode::UntilRelease && !(on & kKnife)) mode_ = Mode::Off;
        return mode_ != Mode::Off;
    }
};

// One ocarina frame on either handler path (review I4). native=true: a native event, door or
// cutscene owns the player this frame (dispatch observe). The Host's pending work (FreezeEnd
// retry, freeze Poll, unmount retry) runs first, then the session ticks with no input and the
// Host reports its gates lost, so any open session releases instead of waiting for Normal.
// `step`: this tick in 60 Hz frames of game time (watchdog budgets, ocarina_session.h).
template <class Session, class Host>
void Frame(Session& session, Host& host, const InputEvents& events, bool native, double step = 1.0) {
    host.Service();
    host.SetNativeFrame(native);
    session.Tick(native ? InputEvents{} : events, host, step);
    host.SetNativeFrame(false);
}

// Worker side: one more on every return to MainLoop (Rno0 3) after anything else (room load,
// continue after death, title, option screen). The game thread only reads it.
class LoadEpoch {
    std::atomic<uint32_t> epoch_{1};
    bool known_ = false, in_main_ = false;
public:
    void Observe(bool readable, uint32_t routine) {
        const bool main = readable && (routine & 0xFF) == 3;
        if (known_ && main && !in_main_) epoch_.fetch_add(1, std::memory_order_acq_rel);
        known_ = true; in_main_ = main;
    }
    uint32_t Current() const { return epoch_.load(std::memory_order_acquire); }
};

// The worker publishes every focus loss; Host::Foreground() reports it once (session contract).
class FocusLatch {
    std::atomic<bool> lost_{false};
public:
    void Publish(bool foreground) { if (!foreground) lost_.store(true, std::memory_order_release); }
    bool Take(bool foreground_now) { const bool was = lost_.exchange(false, std::memory_order_acq_rel); return foreground_now && !was; }
};

// Leon costume byte (pG+0x4FC9: 0 Jacket pl00, 1 Normal pl08, 2 Vest pl09, 3 RPD pl10, 4 Mafia
// pl19) -> ocarina TPL of ocarina-model-03's costume_manifest. Mafia is not Link: -1.
inline int CostumeAsset(unsigned costume) { return costume <= 3 ? int(costume) : -1; }

// XInput gamepad state, already read (buttons and left stick, raw shorts).
struct Pad { bool connected = false; uint16_t buttons = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0; };
constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000, kPadLB = 0x0100, kPadRB = 0x0200;
inline float Axis(int16_t v) { return v < 0 ? (v < -32767 ? -1.f : float(v) / 32767.f) : float(v) / 32767.f; }

// Down(vk) -> key held. The stick keeps XInput's sign (ly > 0 up), as NoteInput expects.
template <class Down>
RawInput Assemble(const Bindings& k, Down down, const Pad& pad, bool use_pad, bool n64 = false) {
    RawInput r;
    r.toggle_key = down(k.toggle); r.stow = down(k.stow); r.a = down(k.note_a);
    r.up = down(k.up); r.down = down(k.down); r.left = down(k.left); r.right = down(k.right);
    if (use_pad && pad.connected) {
        r.pad_a = (pad.buttons & kPadA) != 0; r.pad_b = (pad.buttons & kPadB) != 0;
        r.lb = (pad.buttons & kPadLB) != 0; r.rb = (pad.buttons & kPadRB) != 0;
        // n64: the C buttons are the right stick; the left stick is not read at all.
        r.lx = Axis(n64 ? pad.rx : pad.lx); r.ly = Axis(n64 ? pad.ry : pad.ly);
    }
    return r;
}
}  // namespace link_runtime::ocarina::runtime
