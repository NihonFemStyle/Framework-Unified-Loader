#pragma once
#include "ui/ui.h"

namespace ui {

void titleBar(const Rect& panel, float barHeight, const char* title, float time, gfx::Image* icon = nullptr, float alpha = 1.f);
void skyBackground(const Rect& area, const Rect& shape, float radius, float time, float alpha,
                   float starScale = 1.f);
bool closeButton(const char* name, const Rect& r);
bool button(const char* name, const Rect& r, const char* label);
bool textField(const char* name, const Rect& r, std::string& buffer, const char* placeholder,
               bool secret = false);
bool gameRow(const char* name, const Rect& r, gfx::Image* icon, const char* title, bool selected = false);
void chevron(const Vec2& center, float height, const Col& c);
void logoMark(gfx::Image* logo, const Rect& box, float alpha);
void spinnerRing(const Vec2& center, float radius, float thickness, const Col& from, const Col& to, float alpha);
void loadingLine(const Rect& r, float phase, float time, float alpha);

}
