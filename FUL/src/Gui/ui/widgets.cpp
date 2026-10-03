#include "ui/widgets.h"

namespace ui {

struct Sparkle {
    float x, y, size, alpha, phase;
};

static const Sparkle kSparkles[] = {
    { 0.10f, 0.32f, 4.6f, 0.92f, 0.0f },
    { 0.34f, 0.68f, 3.2f, 0.62f, 1.8f },
    { 0.55f, 0.26f, 5.2f, 1.00f, 0.9f },
    { 0.78f, 0.64f, 3.6f, 0.70f, 2.6f },
    { 0.94f, 0.34f, 4.2f, 0.84f, 1.3f },
};

static const Sparkle kDust[] = {
    { 0.04f, 0.66f, 1.0f, 0.42f, 0.4f }, { 0.17f, 0.22f, 0.9f, 0.34f, 2.1f },
    { 0.24f, 0.74f, 1.1f, 0.46f, 1.2f }, { 0.42f, 0.20f, 0.9f, 0.32f, 3.0f },
    { 0.47f, 0.76f, 1.0f, 0.40f, 0.7f }, { 0.63f, 0.72f, 1.1f, 0.44f, 2.4f },
    { 0.68f, 0.22f, 0.9f, 0.30f, 1.6f }, { 0.86f, 0.76f, 1.0f, 0.38f, 0.2f },
    { 0.90f, 0.18f, 0.9f, 0.34f, 2.8f },
};

void titleBar(const Rect& panel, float barHeight, const char* title, float time, gfx::Image* icon, float alpha) {
    Theme& t = theme();
    Rect bar(panel.x, panel.y, panel.w, barHeight);

    // Animated chroma sweep: a slow rainbow drifts across the top bar,
    // widened to ~two full spectrums and kept dim so it doesn't glare.
    dl().chroma(bar, 2.0f, 0.05f, t.radius, 0.50f * alpha);
    // Slim dark seam so the bar reads as part of the panel.
    dl().rect(Rect(bar.x, bar.b() - 1.f, bar.w, 2.f), Col::hex(0x0A0A0A, 0.80f * alpha), t.radius);

    dl().pushClip(bar);

    // Image title mode: when a banner image is supplied it IS the brand title,
    // drawn aspect-fitted to the bar, and the text title is skipped entirely.
    const float iconPad = 13.f;
    const bool hasIcon = icon && icon->valid();
    const float iconH = hasIcon ? bar.h * 0.78f : 0.f;
    const float iconW = hasIcon ? iconH * icon->aspect() : 0.f;
    if (hasIcon)
        dl().image(icon->srv, Rect(bar.x + iconPad, bar.y + (bar.h - iconH) * 0.5f, iconW, iconH),
                   Col(1.f, 1.f, 1.f, 0.95f * alpha), 0.f);
    const float textX = bar.x + iconPad + iconW + (iconW > 0.f ? 8.f : 0.f);

    const float bandX0 = textX + fonts().title.measure(title) + 16.f;
    const float bandX1 = bar.r() - 34.f;
    const float bandW = (std::max)(bandX1 - bandX0, 1.f);
    auto bandX = [&](float u) { return bandX0 + bandW * core::clamp01(u); };

    const float arcY = bar.y + bar.h * 0.50f;
    const int dots = 26;
    for (int i = 0; i < dots; ++i) {
        const float u = (float)i / (float)(dots - 1);
        const float curve = std::sin(u * core::kPi) * bar.h * 0.30f;
        const float fade = std::sin(u * core::kPi);
        dl().circle(Vec2(bandX(u), arcY - curve), 0.75f, Col::hex(0xE6E6E6, 0.30f * fade * alpha));
    }

    for (const Sparkle& s : kDust) {
        const float twinkle = 0.55f + 0.45f * std::sin(time * 1.4f + s.phase * 2.f);
        dl().circle(Vec2(bandX(s.x), bar.y + bar.h * s.y), s.size, Col::hex(0xF2F2F2, s.alpha * twinkle * alpha));
    }

    for (const Sparkle& s : kSparkles) {
        const float twinkle = 0.74f + 0.26f * std::sin(time * 1.6f + s.phase);
        const Vec2 c(bandX(s.x), bar.y + bar.h * s.y);
        const float r = s.size * twinkle;
        dl().shadow(Rect(c.x - r * 0.5f, c.y - r * 0.5f, r, r), Col::hex(0xD0D0D0, 0.30f * twinkle * alpha), r * 1.9f,
                    r * 0.5f, Vec2());
        dl().star(c, r, 0.85f, Col::hex(0xFFFFFF, s.alpha * twinkle * alpha));
    }

    dl().popClip();

    if (!hasIcon) {
        Rect titleBox(textX, bar.y, 160.f, bar.h);
        ui::textShadow(fonts().title, titleBox, title, t.text, AlignH::Left, AlignV::Middle, 0.2f, 0.55f * alpha, 1.f);
    }
}

static const Sparkle kSkyDust[] = {
    { 0.06f, 0.14f, 1.1f, 0.44f, 0.3f }, { 0.14f, 0.42f, 0.9f, 0.34f, 2.2f },
    { 0.09f, 0.72f, 1.0f, 0.38f, 1.1f }, { 0.21f, 0.22f, 0.9f, 0.30f, 3.1f },
    { 0.26f, 0.62f, 1.1f, 0.42f, 0.8f }, { 0.33f, 0.10f, 1.0f, 0.36f, 2.6f },
    { 0.38f, 0.84f, 0.9f, 0.32f, 1.5f }, { 0.46f, 0.30f, 1.1f, 0.40f, 0.5f },
    { 0.54f, 0.72f, 0.9f, 0.30f, 2.9f }, { 0.61f, 0.16f, 1.0f, 0.38f, 1.8f },
    { 0.67f, 0.52f, 0.9f, 0.32f, 0.9f }, { 0.74f, 0.80f, 1.1f, 0.42f, 2.4f },
    { 0.79f, 0.24f, 0.9f, 0.34f, 1.2f }, { 0.86f, 0.60f, 1.0f, 0.38f, 3.3f },
    { 0.92f, 0.18f, 1.1f, 0.40f, 0.6f }, { 0.95f, 0.78f, 0.9f, 0.32f, 2.0f },
};

static const Sparkle kSkyStars[] = {
    { 0.11f, 0.26f, 4.2f, 0.80f, 0.4f }, { 0.29f, 0.76f, 3.4f, 0.62f, 2.0f },
    { 0.44f, 0.14f, 5.0f, 0.90f, 1.1f }, { 0.58f, 0.86f, 3.0f, 0.55f, 3.0f },
    { 0.72f, 0.34f, 4.6f, 0.84f, 0.7f }, { 0.90f, 0.68f, 3.6f, 0.66f, 2.5f },
};

void skyBackground(const Rect& area, const Rect& shape, float radius, float time, float alpha, float starScale) {
    Theme& t = theme();

    dl().gradientHMasked(area, t.barLeft.alpha(alpha), t.barRight.alpha(alpha), shape, radius);
    dl().gradientVMasked(area, t.barGlow.alpha(0.16f * alpha), Col::hex(0x0A0A0A, 0.55f * alpha), shape, radius);

    dl().pushClip(area);
    for (const Sparkle& s : kSkyDust) {
        const float twinkle = 0.55f + 0.45f * std::sin(time * 1.3f + s.phase * 2.f);
        dl().circle(Vec2(area.x + area.w * s.x, area.y + area.h * s.y), s.size * starScale,
                    Col::hex(0xF2F2F2, s.alpha * twinkle * alpha));
    }
    for (const Sparkle& s : kSkyStars) {
        const float twinkle = 0.72f + 0.28f * std::sin(time * 1.5f + s.phase);
        const Vec2 c(area.x + area.w * s.x, area.y + area.h * s.y);
        const float r = s.size * twinkle * starScale;
        dl().shadow(Rect(c.x - r * 0.5f, c.y - r * 0.5f, r, r), Col::hex(0xD0D0D0, 0.28f * twinkle * alpha),
                    r * 1.9f, r * 0.5f, Vec2());
        dl().star(c, r, 0.85f, Col::hex(0xFFFFFF, s.alpha * twinkle * alpha));
    }
    dl().popClip();
}

bool closeButton(const char* name, const Rect& r) {
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);
    const float hoverT = anim(wid, 0, over ? 1.f : 0.f, 16.f);

    if (hoverT > 0.01f)
        dl().circle(r.center(), r.w * 0.5f, Col::hex(0xFFFFFF, 0.16f * hoverT));

    const Vec2 c = r.center();
    const float s = 4.4f;
    const Col line = Col::hex(0xFFFFFF, 0.88f + 0.12f * hoverT);
    dl().line(Vec2(c.x - s, c.y - s), Vec2(c.x + s, c.y + s), 1.7f, line);
    dl().line(Vec2(c.x + s, c.y - s), Vec2(c.x - s, c.y + s), 1.7f, line);
    return result;
}

bool button(const char* name, const Rect& r, const char* label) {
    Theme& t = theme();
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);

    const float hoverT = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 15.f));
    const float pressT = anim(wid, 1, (over && g().input.down) ? 1.f : 0.f, 24.f);
    const float radius = 8.f;

    Rect box = r.offset(0.f, -1.5f * hoverT + 1.5f * pressT);

    // Ambient breathing glow even when idle.
    const float breathe = 0.5f + 0.5f * std::sin(g().time * 2.2f);
    dl().shadow(box.shrink(2.f), t.focus.alpha((0.14f + 0.10f * breathe) * (0.5f + 0.5f * hoverT)),
                16.f, radius, Vec2(0.f, 4.f));

    dl().gradientHMasked(box, t.barGlow.alpha(0.22f + 0.38f * hoverT), t.focus.alpha(0.34f + 0.40f * hoverT),
                         box, radius);
    dl().border(box.shrink(0.5f),
                t.focus.alpha(0.24f + 0.40f * hoverT + (1.f - hoverT) * breathe * 0.10f), 1.f, radius - 0.5f);

    // Shine sweep that travels across the face while hovered.
    if (hoverT > 0.004f) {
        const float x = std::fmod(g().time * 0.45f, 1.6f) - 0.3f;
        const float sw = box.w * 0.5f;
        Rect sweep(box.x + (box.w - sw) * core::clamp01(x), box.y - 6.f, sw, box.h + 12.f);
        const float pulse = 0.08f + 0.06f * (0.5f + 0.5f * std::sin(g().time * 4.f));
        dl().pushClip(box.shrink(1.f));
        dl().gradientHMasked(sweep, Col::hex(0xFFFFFF, 0.f), Col::hex(0xFFFFFF, pulse * hoverT), sweep, radius);
        dl().popClip();
    }

    ui::textShadow(fonts().body, box, label, t.text.alpha(0.88f + 0.12f * hoverT), AlignH::Center, AlignV::Middle,
                   0.15f, 0.55f, 1.f);
    return result;
}

// Pops one full UTF-8 code point from the end of the string.
void backspaceUtf8(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (s[i] & 0xC0) == 0x80) --i;
    s.erase(i);
}

bool textField(const char* name, const Rect& r, std::string& buffer, const char* placeholder, bool secret) {
    Theme& t = theme();
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);

    if (over && g().input.pressed) g().field = wid;
    const bool focused = g().field == wid;
    if (focused) {
        // Text focus lives in g().field, never in g().active, so a focused
        // field does not capture the mouse and other widgets stay interactive.
        g().fieldRect = r;
        g().fieldSet = wid;

        const Input& in = g().input;
        if (in.escape) {
            g().field = 0;
        } else if (in.enter) {
            g().field = 0;
            return true;
        } else if (in.backspace) {
            backspaceUtf8(buffer);
        } else if (!in.text.empty()) {
            buffer.append(in.text);
            if (buffer.size() > 128) buffer.resize(128);
        }
    }

    const float focusT = core::easeOutCubic(anim(wid, 0, focused ? 1.f : 0.f, 13.f));
    const float radius = 7.f;

    dl().rect(r, Col::hex(0x141414, 0.72f), radius);
    dl().border(r.shrink(0.5f), t.focus.alpha(0.10f + 0.55f * focusT + (1.f - focusT) * (over ? 0.22f : 0.f)),
                1.f, radius - 0.5f);

    const float textX = r.x + 12.f;
    const Col textCol = t.text.alpha(0.86f + 0.14f * focusT);
    const Col dim = Col::hex(0xFFFFFF, 0.30f + 0.12f * focusT);

    if (buffer.empty()) {
        ui::text(fonts().body, Rect(textX, r.y, r.w - 20.f, r.h), placeholder, dim, AlignH::Left, AlignV::Middle);
    } else {
        const std::string display = secret ? std::string(buffer.size(), '*') : buffer;
        ui::text(fonts().body, Rect(textX, r.y, r.w - 20.f, r.h), display.c_str(), textCol, AlignH::Left, AlignV::Middle);
    }

    if (focused) {
        const std::string prefix = buffer.empty() ? std::string() : (secret ? std::string(buffer.size(), '*') : buffer);
        const float caretX = textX + fonts().body.measure(prefix.c_str()) + 1.f;
        const bool visible = std::sin(g().time * 5.5f) > -0.15f;
        if (visible) {
            dl().line(Vec2(std::floor(caretX) + 0.5f, r.y + 5.f), Vec2(std::floor(caretX) + 0.5f, r.b() - 5.f),
                      1.5f, t.text.alpha(0.95f));
        }
    }

    return false;
}

void chevron(const Vec2& center, float height, const Col& c) {
    const float halfH = height * 0.5f;
    const float halfW = height * 0.552f * 0.5f;
    const float thickness = height * 0.149f;

    dl().chevronShape(Vec2(center.x - halfW, center.y - halfH), Vec2(center.x + halfW, center.y), thickness, c);
}

bool gameRow(const char* name, const Rect& r, gfx::Image* icon, const char* title, bool selected) {
    Theme& t = theme();
    const uint32_t wid = id(name);
    const bool over = hovered(wid, r);
    const bool result = clicked(wid, r);

    const float hoverT = core::easeOutCubic(anim(wid, 0, over ? 1.f : 0.f, 15.f));
    const float pressT = anim(wid, 1, (over && g().input.down) ? 1.f : 0.f, 24.f);
    const float selT = core::easeOutCubic(anim(wid, 2, selected ? 1.f : 0.f, 18.f));
    const float radius = 8.f;

    const float glow = (std::max)(hoverT, selT);
    Rect box = r.offset(0.f, -1.2f * glow + 1.2f * pressT);

    if (glow > 0.004f)
        dl().shadow(box.shrink(2.f), t.focus.alpha(0.16f * glow + 0.10f * selT), 16.f, radius, Vec2(0.f, 3.f));

    dl().rect(box, t.row, radius);
    if (glow > 0.004f)
        skyBackground(box, box, radius, g().time, glow, 0.62f);
    dl().border(box.shrink(0.5f), t.focus.alpha(0.30f * selT + 0.55f * hoverT), 1.f, radius - 0.5f);

    float textX = box.x + 13.f;
    if (icon && icon->valid()) {
        const float s = 20.f + glow * 1.6f;
        Rect iconRect(box.x + 11.f, box.center().y - s * 0.5f, s, s);
        dl().image(icon->srv, iconRect, Col(1.f, 1.f, 1.f, 1.f), 0.f);
        textX = box.x + 11.f + 20.f + 10.f;
    }

    ui::text(fonts().body, Rect(textX, box.y, box.w * 0.7f, box.h), title, t.text, AlignH::Left, AlignV::Middle);

    // Chevron pill: sits at its resting position when the row is the active
    // selection (filled), otherwise it slides in on hover as the launch cue.
    const float pillClamp = (std::max)(hoverT, selT);
    if (pillClamp > 0.004f) {
        const float breathe = 0.5f + 0.5f * std::sin(g().time * 2.4f);
        const float slide = selT > 0.5f ? 0.f : (1.f - pillClamp) * 7.f;
        const float pillR = 9.5f + selT * 2.f;
        const Vec2 pillC(std::floor(box.r() - 7.f - pillR + slide) + 0.5f, std::floor(box.center().y) + 0.5f);

        dl().shadow(Rect(pillC.x - pillR, pillC.y - pillR, pillR * 2.f, pillR * 2.f),
                    t.focus.alpha((0.20f + breathe * 0.22f) * pillClamp + 0.16f * selT), 13.f + breathe * 5.f,
                    pillR, Vec2());
        if (selT > 0.004f)
            dl().circle(pillC, pillR, t.focus.alpha(0.72f * selT));
        if (hoverT > 0.004f && selT < 1.f)
            dl().circle(pillC, pillR, Col::hex(0xFFFFFF, 0.10f * hoverT * (1.f - selT)));
        dl().border(Rect(pillC.x - pillR + 0.5f, pillC.y - pillR + 0.5f, pillR * 2.f - 1.f, pillR * 2.f - 1.f),
                    t.focus.alpha((0.35f + breathe * 0.25f) * hoverT + 0.30f * selT), 1.f, pillR - 0.5f);

        const float chevH = 8.5f;
        const float nudge = breathe * 0.7f;
        chevron(Vec2(pillC.x + nudge, pillC.y + 1.f), chevH, Col::hex(0x000000, 0.35f * pillClamp));
        chevron(Vec2(pillC.x + nudge, pillC.y), chevH,
                selT >= 0.5f ? Col::hex(0xFFFFFF, 0.92f * selT) : t.text.alpha(hoverT));
    }

    return result;
}

void loadingLine(const Rect& r, float phase, float time, float alpha) {
    Theme& t = theme();
    const float radius = r.h * 0.5f;

    dl().rect(r, Col::hex(0x141414, 0.85f * alpha), radius);
    dl().border(r.shrink(0.5f), Col::hex(0xFFFFFF, 0.07f * alpha), 1.f, radius - 0.5f);

    const float segW = r.w * 0.42f;
    float pp = phase * 2.f;
    if (pp > 1.f) pp = 2.f - pp;
    const float travel = core::easeInOutCubic(pp);
    Rect seg(r.x + (r.w - segW) * travel, r.y, segW, r.h);

    dl().shadow(seg.shrink(0.5f), t.focus.alpha(0.34f * alpha), 10.f, radius, Vec2());
    dl().gradientHMasked(seg, t.barGlow.alpha(alpha), t.barRight.alpha(alpha), seg, radius);
    dl().gradientHMasked(Rect(seg.x, seg.y, seg.w * 0.5f, seg.h), t.barLeft.alpha(0.75f * alpha),
                         t.barLeft.alpha(0.f), seg, radius);

    dl().pushClip(seg);
    for (int i = 0; i < 3; ++i) {
        const float u = 0.24f + i * 0.26f;
        const float twinkle = 0.45f + 0.55f * std::sin(time * 3.2f + i * 1.7f);
        dl().circle(Vec2(seg.x + seg.w * u, seg.center().y - r.h * 0.08f), 0.8f,
                    Col::hex(0xFFFFFF, 0.85f * twinkle * alpha));
    }
    dl().popClip();
}

void logoMark(gfx::Image* logo, const Rect& box, float alpha) {
    if (!logo || !logo->valid()) return;
    dl().image(logo->srv, box.offset(1.5f, 3.f), Col(0.f, 0.f, 0.f, 0.34f * alpha));
    dl().image(logo->srv, box, Col(1.f, 1.f, 1.f, alpha));
}

void spinnerRing(const Vec2& center, float radius, float thickness, const Col& from, const Col& to, float alpha) {
    const float t = g().time;

    static const float kOrbit[5][3] = {
        { -0.55f, 1.62f, 3.6f }, { 0.95f, 1.48f, 2.6f }, { 2.35f, 1.72f, 4.2f },
        { 3.75f, 1.55f, 2.9f },  { 5.15f, 1.68f, 3.3f },
    };
    for (const auto& s : kOrbit) {
        const float twinkle = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 2.4f + s[0] * 2.f));
        const float drift = s[0] + t * 0.35f;
        const Vec2 p(center.x + std::cos(drift) * radius * s[1], center.y + std::sin(drift) * radius * s[1]);
        dl().star(p, s[2] * (0.7f + 0.3f * twinkle), 0.85f, Col::hex(0xFFFFFF, 0.60f * twinkle * alpha));
    }

    dl().arc(center, radius, thickness, 0.f, core::kPi * 2.f, Col::hex(0xFFFFFF, 0.10f * alpha));

    const float rot = t * 3.2f;
    const float sweep = 0.42f * core::kPi + (0.5f + 0.5f * std::sin(t * 2.4f)) * 0.85f * core::kPi;
    dl().arcGradient(center, radius, thickness, rot, rot + sweep, from.alpha(alpha), to.alpha(alpha));

    const Vec2 head(center.x + std::cos(rot + sweep) * radius, center.y + std::sin(rot + sweep) * radius);
    dl().circle(head, thickness * 0.55f, Col::hex(0xFFFFFF, 0.85f * alpha));
}

}
