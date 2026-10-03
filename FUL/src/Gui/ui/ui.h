#pragma once
#include <string>
#include <unordered_map>
#include "core/types.h"
#include "gfx/drawlist.h"
#include "gfx/font.h"
#include "gfx/image.h"

namespace ui {

using core::Col;
using core::Rect;
using core::Vec2;

enum class AlignH { Left, Center, Right };
enum class AlignV { Top, Middle, Bottom };

struct Input {
    Vec2 mouse;
    bool down = false;
    bool pressed = false;
    bool released = false;
    // Per-frame keyboard events (WM_CHAR converted to UTF-8, plus paste).
    std::string text;
    bool backspace = false;
    bool enter = false;
    bool escape = false;
};

struct Theme {
    Col body = Col::hex(0x1A1A1A, 1.f);     // Synapse content
    Col row = Col::hex(0x292929, 1.f);      // Synapse panel
    Col barLeft = Col::hex(0x1A1A1A, 1.f);  // Synapse content
    Col barRight = Col::hex(0x292929, 1.f); // Synapse panel
    Col barGlow = Col::hex(0x5A5A5A, 1.f);  // Synapse focus
    Col text = Col::hex(0xFFFFFF, 1.f);     // Synapse white accent
    Col focus = Col::hex(0x5A5A5A, 1.f);    // Synapse focus
    float radius = 10.f;
};

struct Fonts {
    gfx::Font title;
    gfx::Font body;
};

struct AnimState {
    float value = 0.f;
    float velocity = 0.f;
    bool initialized = false;
};

struct Context {
    gfx::DrawList draw;
    Input input;
    Theme theme;
    Fonts fonts;
    float dt = 0.f;
    float time = 0.f;
    float width = 0.f;
    float height = 0.f;
    uint32_t hot = 0;
    uint32_t active = 0;
    uint32_t hotNext = 0;
    uint32_t field = 0; // id of the focused text field (0 = none)
    Rect fieldRect;     // bounds of the focused field (hit-test for blur)
    uint32_t fieldSet = 0; // field id painted focused this frame (0 = none)
    bool keepActive = false;
    std::unordered_map<uint64_t, AnimState> anims;
};

struct FontData {
    const void* semiBold = nullptr;
    size_t semiBoldSize = 0;
    const void* bold = nullptr;
    size_t boldSize = 0;
};

Context& g();
bool init(ID3D11Device* dev, const FontData& fonts);
void shutdown();
void newFrame(const Input& in, float dt, float width, float height);
void endFrame();

inline gfx::DrawList& dl() { return g().draw; }
inline Theme& theme() { return g().theme; }
inline Fonts& fonts() { return g().fonts; }

uint32_t id(const char* str);

float anim(uint32_t widget, int slot, float target, float speed);
bool hovered(uint32_t widget, const Rect& r);
bool clicked(uint32_t widget, const Rect& r);

// True while a text field holds focus. Used by the host to forward escape to
// the field instead of closing the window.
bool textFieldFocused();

void text(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h = AlignH::Left,
          AlignV v = AlignV::Middle, float tracking = 0.f);
void textShadow(gfx::Font& font, const Rect& box, const char* str, const Col& col, AlignH h = AlignH::Left,
                AlignV v = AlignV::Middle, float tracking = 0.f, float shadowAlpha = 0.55f,
                float offsetY = 1.f);

}
