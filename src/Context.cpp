#include "FluidUI/Context.h"

#include <algorithm>
#include <cstdio>

namespace fluid {

namespace {

std::string_view DisplayLabel(std::string_view s) {
    const size_t p = s.find("##");
    return p == std::string_view::npos ? s : s.substr(0, p);
}

Color Fade(Color c, float opacity) { c.a *= opacity; return c; }

} // namespace

Context::Context() : text_(&defaultText_) {}
Context::~Context() = default;

void Context::SetTextBackend(ITextBackend* backend) { text_ = backend ? backend : &defaultText_; }

// ---------------------------------------------------------------- frame

void Context::NewFrame(const InputState& input) {
    in_ = input;
    draw_.Clear();
    draw_.PushClipRect({0.f, 0.f, in_.displaySize.x, in_.displaySize.y});
    idStack_.clear();
    classStack_.clear();
    containerStack_.clear();
    horizontalLayout_ = false;
    sameLine_ = false;
}

const DrawList& Context::EndFrame() {
    draw_.PopClipRect();
    prevMouseDown_ = in_.mouseDown[0];
    if (!in_.mouseDown[0]) active_ = 0; // never leave a stuck active widget
    return draw_;
}

// ---------------------------------------------------------------- helpers

WidgetId Context::GetID(std::string_view label) const {
    return HashStr(label, idStack_.empty() ? kHashSeed : idStack_.back());
}

ComputedStyle Context::Resolve(std::string_view widget, uint8_t state) const {
    return style_.Resolve(widget, classStack_, state);
}

ComputedStyle Context::Animate(WidgetId id, std::string_view widget, uint8_t extra,
                               bool hovered, bool held) {
    const ComputedStyle n = Resolve(widget, extra);
    const ComputedStyle h = Resolve(widget, extra | State_Hovered);
    const ComputedStyle a = Resolve(widget, extra | State_Hovered | State_Active);

    Anim& an = anims_[id];
    const float k = std::min(1.f, in_.deltaTime * n.transitionSpeed);
    an.hover  += ((hovered || held ? 1.f : 0.f) - an.hover) * k;
    an.active += ((held ? 1.f : 0.f) - an.active) * k;

    return Lerp(Lerp(n, h, an.hover), a, an.active);
}

Context::Interaction Context::Behavior(WidgetId id, Rect r) {
    Interaction res;
    const bool down = in_.mouseDown[0];
    const bool justPressed = down && !prevMouseDown_;

    res.hovered = r.Contains(in_.mousePos) && (active_ == 0 || active_ == id);
    if (res.hovered && justPressed && active_ == 0) active_ = id;

    if (active_ == id) {
        if (down) {
            res.held = true;
        } else {
            res.pressed = r.Contains(in_.mousePos);
            active_ = 0;
        }
    }
    return res;
}

Rect Context::Place(Vec2 size, float spacing) {
    Vec2 pos;
    if (horizontalLayout_) {
        pos = {cursorX_, content_.y};
        cursorX_ = pos.x + size.x + spacing;
    } else if (sameLine_) {
        pos = {lastRight_ + spacing, lineTop_};
        lineBottom_ = std::max(lineBottom_, pos.y + size.y);
    } else {
        pos = {content_.x, cursorY_};
        lineTop_ = pos.y;
        lineBottom_ = pos.y + size.y;
    }
    lastRight_ = pos.x + size.x;
    cursorY_ = lineBottom_ + spacing;
    sameLine_ = false;
    return {pos.x, pos.y, size.x, size.y};
}

void Context::DrawBox(Rect r, const ComputedStyle& s) {
    draw_.AddShadow(r, Fade(s.shadowColor, s.opacity), s.shadowBlur, s.rounding);
    draw_.AddBox(r, Fade(s.background, s.opacity), Fade(s.borderColor, s.opacity),
                 s.borderWidth, s.rounding);
}

// ---------------------------------------------------------------- ids / classes / layout

void Context::PushClass(std::string_view cls) { classStack_.emplace_back(cls); }
void Context::PopClass() { if (!classStack_.empty()) classStack_.pop_back(); }
void Context::PushID(std::string_view id) { idStack_.push_back(GetID(id)); }
void Context::PopID() { if (!idStack_.empty()) idStack_.pop_back(); }
void Context::SameLine() { sameLine_ = true; }
void Context::Spacing(float px) { cursorY_ += px; }

// ---------------------------------------------------------------- window

bool Context::BeginWindow(std::string_view title, Rect initial) {
    const WidgetId id = GetID(title);
    WindowState& w = windows_[id];
    if (!w.init) {
        w.init = true;
        w.pos = {initial.x, initial.y};
        w.size = {initial.w, initial.h};
    }
    if (w.dock != DockSide::None && !w.dragging) {
        const float halfW = in_.displaySize.x * 0.5f;
        const float halfH = in_.displaySize.y * 0.5f;
        switch (w.dock) {
            case DockSide::Left:   w.pos = {0.f, 0.f}; w.size = {halfW, in_.displaySize.y}; break;
            case DockSide::Right:  w.pos = {halfW, 0.f}; w.size = {halfW, in_.displaySize.y}; break;
            case DockSide::Top:    w.pos = {0.f, 0.f}; w.size = {in_.displaySize.x, halfH}; break;
            case DockSide::Bottom: w.pos = {0.f, halfH}; w.size = {in_.displaySize.x, halfH}; break;
            case DockSide::None: break;
        }
    }

    const ComputedStyle ws = Resolve("window", 0);
    const ComputedStyle ts = Resolve("window-title", 0);
    const std::string_view text = DisplayLabel(title);
    const float titleH = ts.fontSize * 1.2f + ts.padding.Vertical();

    Rect frame{w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect hitBar{frame.x, frame.y, frame.w, titleH};

    // Drag by title bar
    const WidgetId dragId = HashStr("##titlebar", id);
    const bool wasDragging = w.dragging;
    const Interaction drag = Behavior(dragId, hitBar);
    if (drag.held) {
        if (!wasDragging) { w.dragging = true; w.dock = DockSide::None; w.dragOff = in_.mousePos - w.pos; }
        w.pos = in_.mousePos - w.dragOff;
    } else if (wasDragging && !in_.mouseDown[0]) {
        if (dockingEnabled_) {
            constexpr float dockThreshold = 24.f;
            if (in_.mousePos.x <= dockThreshold) w.dock = DockSide::Left;
            else if (in_.mousePos.x >= in_.displaySize.x - dockThreshold) w.dock = DockSide::Right;
            else if (in_.mousePos.y <= dockThreshold) w.dock = DockSide::Top;
            else if (in_.mousePos.y >= in_.displaySize.y - dockThreshold) w.dock = DockSide::Bottom;
        }
        w.dragging = false;
    }
    if (w.dock != DockSide::None) {
        const float halfW = in_.displaySize.x * 0.5f;
        const float halfH = in_.displaySize.y * 0.5f;
        switch (w.dock) {
            case DockSide::Left:   w.pos = {0.f, 0.f}; w.size = {halfW, in_.displaySize.y}; break;
            case DockSide::Right:  w.pos = {halfW, 0.f}; w.size = {halfW, in_.displaySize.y}; break;
            case DockSide::Top:    w.pos = {0.f, 0.f}; w.size = {in_.displaySize.x, halfH}; break;
            case DockSide::Bottom: w.pos = {0.f, halfH}; w.size = {in_.displaySize.x, halfH}; break;
            case DockSide::None: break;
        }
    }
    frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect bar{frame.x, frame.y, frame.w, titleH};

    DrawBox(frame, ws);
    draw_.AddRectFilled(bar, Fade(ts.background, ts.opacity), ws.rounding);
    text_->Emit(draw_, {bar.x + ts.padding.left, bar.y + ts.padding.top}, text,
                ts.fontSize, Fade(ts.foreground, ts.opacity));

    draw_.PushClipRect(frame);
    idStack_.push_back(id);

    content_ = {frame.x + ws.padding.left,
                frame.y + titleH + ws.padding.top,
                frame.w - ws.padding.Horizontal(),
                frame.h - titleH - ws.padding.Vertical()};
    cursorY_ = content_.y;
    cursorX_ = content_.x;
    sameLine_ = false;
    horizontalLayout_ = false;
    return true;
}

void Context::EndWindow() {
    while (!containerStack_.empty()) EndContainer();
    draw_.PopClipRect();
    if (!idStack_.empty()) idStack_.pop_back();
}

bool Context::BeginContainerImpl(Vec2 size, bool horizontal,
                                std::string_view widget) {
    const ComputedStyle style = Resolve(widget, 0);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) size.y = style.fontSize * 6.f + style.padding.Vertical();
    const Rect frame = Place(size, style.spacing);

    LayoutState outer{content_, cursorY_, lineTop_, lineBottom_, lastRight_, cursorX_,
                      sameLine_, horizontalLayout_};
    containerStack_.push_back({outer});
    DrawBox(frame, style);
    draw_.PushClipRect(frame);
    content_ = {frame.x + style.padding.left, frame.y + style.padding.top,
                std::max(0.f, frame.w - style.padding.Horizontal()),
                std::max(0.f, frame.h - style.padding.Vertical())};
    cursorY_ = content_.y;
    cursorX_ = content_.x;
    lineTop_ = content_.y;
    lineBottom_ = content_.y;
    lastRight_ = content_.x;
    sameLine_ = false;
    horizontalLayout_ = horizontal;
    return true;
}

bool Context::BeginContainer(std::string_view label, Vec2 size) {
    PushID(label);
    const bool result = BeginContainerImpl(size, false);
    return result;
}

bool Context::BeginList(std::string_view label, Vec2 size, bool horizontal) {
    PushID(label);
    return BeginContainerImpl(size, horizontal,
                              horizontal ? "horizontal-list" : "vertical-list");
}

void Context::EndContainer() {
    if (containerStack_.empty()) return;
    draw_.PopClipRect();
    const LayoutState outer = containerStack_.back().outer;
    containerStack_.pop_back();
    content_ = outer.content;
    cursorY_ = outer.cursorY;
    lineTop_ = outer.lineTop;
    lineBottom_ = outer.lineBottom;
    lastRight_ = outer.lastRight;
    cursorX_ = outer.cursorX;
    sameLine_ = outer.sameLine;
    horizontalLayout_ = outer.horizontal;
    PopID();
}

void Context::DrawTextCentered(Rect r, std::string_view text, const ComputedStyle& style) {
    const Vec2 measured = text_->Measure(text, style.fontSize);
    text_->Emit(draw_, {r.x + (r.w - measured.x) * 0.5f, r.y + (r.h - measured.y) * 0.5f},
                text, style.fontSize, Fade(style.foreground, style.opacity));
}

bool Context::BeginTabContainer(std::string_view label, Vec2 size,
                                const std::vector<TabItem>& tabs, int* selected,
                                TabPosition position) {
    if (!selected || tabs.empty()) return false;
    PushID(label);
    if (!BeginContainerImpl(size, false)) { PopID(); return false; }

    const ComputedStyle style = Resolve("tab", 0);
    const float barSize = style.fontSize * 1.2f + style.padding.Vertical();
    float tabWidth = barSize;
    if (position == TabPosition::Left) {
        for (const TabItem& tab : tabs) {
            const std::string text = tab.icon.empty() || tab.showText
                ? (tab.icon.empty() ? std::string(tab.label)
                                    : std::string(tab.icon) + " " + std::string(tab.label))
                : std::string(tab.icon);
            const ComputedStyle tabStyle = Resolve("tab", 0);
            tabWidth = std::max(tabWidth, text_->Measure(text, tabStyle.fontSize).x +
                                           tabStyle.padding.Horizontal());
        }
    }
    const Rect originalContent = content_;
    if (position == TabPosition::Bottom) {
        content_.y += std::max(0.f, content_.h - barSize);
        content_.h = barSize;
    } else if (position == TabPosition::Left) {
        content_.w = tabWidth;
    } else {
        content_.h = barSize;
    }
    horizontalLayout_ = position != TabPosition::Left;
    cursorY_ = content_.y;
    cursorX_ = content_.x;
    for (size_t i = 0; i < tabs.size(); ++i) {
        const TabItem& tab = tabs[i];
        const std::string visibleText = tab.icon.empty() || tab.showText
            ? (tab.icon.empty() ? std::string(tab.label)
                                : std::string(tab.icon) + " " + std::string(tab.label))
            : std::string(tab.icon);
        const std::string_view visible = visibleText;
        const WidgetId tabId = HashStr(tab.label, GetID("##tab"));
        const ComputedStyle base = Resolve("tab", *selected == static_cast<int>(i) ? State_Checked : State_None);
        Vec2 measured = text_->Measure(visible, base.fontSize);
        Vec2 tabSize{measured.x + base.padding.Horizontal(), measured.y + base.padding.Vertical()};
        if (position == TabPosition::Left) {
            tabSize.x = tabWidth;
            tabSize.y = std::max(barSize, measured.y + base.padding.Vertical());
        } else {
            tabSize.y = barSize;
        }
        const Rect tabRect = Place(tabSize, base.spacing);
        const Interaction interaction = Behavior(tabId, tabRect);
        const ComputedStyle tabStyle = Animate(tabId, "tab",
            *selected == static_cast<int>(i) ? State_Checked : State_None,
            interaction.hovered, interaction.held);
        DrawBox(tabRect, tabStyle);
        DrawTextCentered(tabRect, visible, tabStyle);
        if (interaction.pressed) *selected = static_cast<int>(i);
    }
    content_ = originalContent;
    if (position == TabPosition::Left) {
        content_.x += tabWidth;
        content_.w = std::max(0.f, content_.w - tabWidth);
    } else {
        content_.y += position == TabPosition::Bottom ? 0.f : barSize;
        content_.h = std::max(0.f, content_.h - barSize);
    }
    cursorY_ = content_.y;
    cursorX_ = content_.x;
    lineTop_ = cursorY_;
    lineBottom_ = cursorY_;
    lastRight_ = content_.x;
    sameLine_ = false;
    horizontalLayout_ = false;
    return true;
}

// ---------------------------------------------------------------- widgets

void Context::Text(std::string_view text) {
    const ComputedStyle s = Resolve("text", 0);
    const Vec2 ts = text_->Measure(text, s.fontSize);
    const Rect r = Place({ts.x + s.padding.Horizontal(), ts.y + s.padding.Vertical()}, s.spacing);
    text_->Emit(draw_, {r.x + s.padding.left, r.y + s.padding.top}, text,
                s.fontSize, Fade(s.foreground, s.opacity));
}

void Context::TextLabel(std::string_view text) { Text(text); }

void Context::TextLabelLeft(std::string_view text, std::string_view icon) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 iconSize = text_->Measure(icon, style.fontSize);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Rect r = Place({iconSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(iconSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    text_->Emit(draw_, {r.x + style.padding.left, r.y + (r.h - iconSize.y) * 0.5f},
                icon, style.fontSize, Fade(style.foreground, style.opacity));
    text_->Emit(draw_, {r.x + style.padding.left + iconSize.x + style.spacing,
                        r.y + (r.h - textSize.y) * 0.5f},
                text, style.fontSize, Fade(style.foreground, style.opacity));
}

void Context::TextLabelRight(std::string_view text, std::string_view icon) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Vec2 iconSize = text_->Measure(icon, style.fontSize);
    const Rect r = Place({textSize.x + style.spacing + iconSize.x + style.padding.Horizontal(),
                          std::max(textSize.y, iconSize.y) + style.padding.Vertical()}, style.spacing);
    text_->Emit(draw_, {r.x + style.padding.left, r.y + (r.h - textSize.y) * 0.5f},
                text, style.fontSize, Fade(style.foreground, style.opacity));
    text_->Emit(draw_, {r.x + style.padding.left + textSize.x + style.spacing,
                        r.y + (r.h - iconSize.y) * 0.5f},
                icon, style.fontSize, Fade(style.foreground, style.opacity));
}

bool Context::Button(std::string_view label) {
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);

    const ComputedStyle base = Resolve("button", 0);
    const Vec2 ts = text_->Measure(text, base.fontSize);
    const Rect r = Place({ts.x + base.padding.Horizontal(), ts.y + base.padding.Vertical()}, base.spacing);

    const Interaction b = Behavior(id, r);
    const ComputedStyle s = Animate(id, "button", State_None, b.hovered, b.held);

    DrawBox(r, s);
    text_->Emit(draw_, {r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f}, text,
                s.fontSize, Fade(s.foreground, s.opacity));
    return b.pressed;
}

bool Context::ImageButton(std::string_view label, uint64_t texture, Vec2 imageSize) {
    const WidgetId id = GetID(label);
    const ComputedStyle base = Resolve("image-button", 0);
    const Rect r = Place({imageSize.x + base.padding.Horizontal(), imageSize.y + base.padding.Vertical()},
                         base.spacing);
    const Interaction b = Behavior(id, r);
    const ComputedStyle style = Animate(id, "image-button", State_None, b.hovered, b.held);
    DrawBox(r, style);
    draw_.AddQuad({r.x + style.padding.left, r.y + style.padding.top, imageSize.x, imageSize.y},
                  {0.f, 0.f, 1.f, 1.f}, Fade(Color{}, style.opacity), texture);
    return b.pressed;
}

bool Context::Checkbox(std::string_view label, bool* value) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);
    const uint8_t extra = *value ? State_Checked : State_None;

    const ComputedStyle base = Resolve("checkbox", extra);
    const float box = base.fontSize * 1.1f;
    const Vec2 ts = text_->Measure(text, base.fontSize);
    const Rect r = Place({box + base.spacing + ts.x, std::max(box, ts.y)}, base.spacing);

    const Interaction b = Behavior(id, r);
    const ComputedStyle s = Animate(id, "checkbox", extra, b.hovered, b.held);

    const Rect boxR{r.x, r.y + (r.h - box) * 0.5f, box, box};
    DrawBox(boxR, s);
    if (*value)
        draw_.AddRectFilled(boxR.Expanded(-box * 0.25f), Fade(s.accent, s.opacity), s.rounding * 0.5f);
    text_->Emit(draw_, {boxR.x + box + s.spacing, r.y + (r.h - ts.y) * 0.5f}, text,
                s.fontSize, Fade(s.foreground, s.opacity));

    if (b.pressed) *value = !*value;
    return b.pressed;
}

bool Context::Toggle(std::string_view label, bool* value) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);
    const ComputedStyle base = Resolve("toggle", *value ? State_Checked : State_None);
    const float trackW = base.fontSize * 2.2f;
    const float trackH = base.fontSize * 1.15f;
    const Vec2 textSize = text_->Measure(text, base.fontSize);
    const Rect r = Place({trackW + base.spacing + textSize.x, std::max(trackH, textSize.y)}, base.spacing);
    const Interaction interaction = Behavior(id, r);
    const ComputedStyle style = Animate(id, "toggle", *value ? State_Checked : State_None,
                                        interaction.hovered, interaction.held);
    const Rect track{r.x, r.y + (r.h - trackH) * 0.5f, trackW, trackH};
    draw_.AddRectFilled(track, Fade(*value ? style.accent : style.background, style.opacity), trackH * 0.5f);
    const float knob = trackH * 0.78f;
    const float knobX = *value ? track.x + track.w - trackH * 0.89f : track.x + trackH * 0.11f;
    draw_.AddRectFilled({knobX, track.y + (trackH - knob) * 0.5f, knob, knob},
                        Fade(style.foreground, style.opacity), knob * 0.5f);
    text_->Emit(draw_, {track.x + track.w + style.spacing, r.y + (r.h - textSize.y) * 0.5f},
                text, style.fontSize, Fade(style.foreground, style.opacity));
    if (interaction.pressed) *value = !*value;
    return interaction.pressed;
}

bool Context::Radio(std::string_view label, int* value, int option) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);
    const bool checked = *value == option;
    const ComputedStyle base = Resolve("radio", checked ? State_Checked : State_None);
    const float diameter = base.fontSize * 1.1f;
    const Vec2 textSize = text_->Measure(text, base.fontSize);
    const Rect r = Place({diameter + base.spacing + textSize.x, std::max(diameter, textSize.y)}, base.spacing);
    const Interaction interaction = Behavior(id, r);
    const ComputedStyle style = Animate(id, "radio", checked ? State_Checked : State_None,
                                        interaction.hovered, interaction.held);
    const Rect circle{r.x, r.y + (r.h - diameter) * 0.5f, diameter, diameter};
    draw_.AddRectFilled(circle, Fade(style.borderColor.a > 0.f ? style.borderColor : style.background,
                                     style.opacity), diameter * 0.5f);
    if (checked) {
        const float inset = diameter * 0.28f;
        draw_.AddRectFilled(circle.Expanded(-inset), Fade(style.accent, style.opacity),
                            diameter * 0.5f);
    }
    text_->Emit(draw_, {circle.x + diameter + style.spacing, r.y + (r.h - textSize.y) * 0.5f},
                text, style.fontSize, Fade(style.foreground, style.opacity));
    if (interaction.pressed) *value = option;
    return interaction.pressed;
}

bool Context::SliderFloat(std::string_view label, float* value, float lo, float hi) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);

    const ComputedStyle base = Resolve("slider", 0);
    const Rect r = Place({content_.w, base.fontSize * 1.2f + base.padding.Vertical()}, base.spacing);

    const Interaction b = Behavior(id, r);
    bool changed = false;
    if (b.held && hi > lo && r.w > 0.f) {
        const float t = std::clamp((in_.mousePos.x - r.x) / r.w, 0.f, 1.f);
        const float nv = lo + t * (hi - lo);
        if (nv != *value) { *value = nv; changed = true; }
    }

    const ComputedStyle s = Animate(id, "slider", State_None, b.hovered, b.held);
    const float t = hi > lo ? std::clamp((*value - lo) / (hi - lo), 0.f, 1.f) : 0.f;

    DrawBox(r, s);
    if (t > 0.f)
        draw_.AddRectFilled({r.x, r.y, r.w * t, r.h}, Fade(s.accent, s.opacity), s.rounding);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.*s: %.2f", static_cast<int>(text.size()), text.data(), *value);
    const Vec2 ts = text_->Measure(buf, s.fontSize);
    text_->Emit(draw_, {r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f}, buf,
                s.fontSize, Fade(s.foreground, s.opacity));
    return changed;
}

bool Context::ValueSlider(std::string_view label, float* value, float lo, float hi) {
    return SliderFloat(label, value, lo, hi);
}

void Context::Image(uint64_t texture, Vec2 size) {
    const Rect r = Place(size, Resolve("image", 0).spacing);
    draw_.AddQuad(r, {0.f, 0.f, 1.f, 1.f}, Color{}, texture);
}

void Context::ImageLabel(std::string_view label, uint64_t texture, Vec2 imageSize) {
    const ComputedStyle style = Resolve("image-label", 0);
    const Vec2 textSize = text_->Measure(label, style.fontSize);
    const Rect r = Place({imageSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(imageSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    draw_.AddQuad({r.x + style.padding.left, r.y + (r.h - imageSize.y) * 0.5f,
                   imageSize.x, imageSize.y}, {0.f, 0.f, 1.f, 1.f}, Color{}, texture);
    text_->Emit(draw_, {r.x + style.padding.left + imageSize.x + style.spacing,
                        r.y + (r.h - textSize.y) * 0.5f},
                label, style.fontSize, Fade(style.foreground, style.opacity));
}

bool Context::Dropdown(std::string_view label, const std::vector<std::string_view>& items,
                       int* selected, std::string_view icon, bool showText) {
    if (!selected || items.empty()) return false;
    const WidgetId id = GetID(label);
    int current = std::clamp(*selected, 0, static_cast<int>(items.size()) - 1);
    const std::string_view text = icon.empty() ? items[static_cast<size_t>(current)] : icon;
    const ComputedStyle base = Resolve("dropdown", 0);
    const std::string display = icon.empty() || !showText
        ? std::string(icon.empty() ? text : icon)
        : std::string(icon) + " " + std::string(items[static_cast<size_t>(current)]);
    const Vec2 fullSize = text_->Measure(display, base.fontSize);
    const Rect r = Place({fullSize.x + base.padding.Horizontal() + base.fontSize,
                          fullSize.y + base.padding.Vertical()}, base.spacing);
    const Interaction interaction = Behavior(id, r);
    const ComputedStyle style = Animate(id, "dropdown", State_None, interaction.hovered, interaction.held);
    DrawBox(r, style);
    text_->Emit(draw_, {r.x + style.padding.left, r.y + (r.h - fullSize.y) * 0.5f},
                display, style.fontSize, Fade(style.foreground, style.opacity));
    const float arrowX = r.x + r.w - style.padding.right - style.fontSize * 0.35f;
    draw_.AddTriangleFilled({arrowX - 4.f, r.y + r.h * 0.42f},
                            {arrowX + 4.f, r.y + r.h * 0.42f},
                            {arrowX, r.y + r.h * 0.58f}, Fade(style.foreground, style.opacity));
    if (interaction.pressed) dropdowns_[id] = !dropdowns_[id];

    bool changed = false;
    if (dropdowns_[id]) {
        for (size_t i = 0; i < items.size(); ++i) {
            PushID("##dropdown-options");
            PushID(label);
            PushID(items[i]);
            if (Button(items[i])) {
                *selected = static_cast<int>(i);
                dropdowns_[id] = false;
                changed = true;
            }
            PopID(); PopID(); PopID();
        }
    }
    return changed;
}

bool Context::BeginCollapsible(std::string_view label, bool* open, Vec2 size) {
    if (!open) return false;
    const WidgetId id = GetID(label);
    const std::string header = std::string(*open ? "v " : "> ") + std::string(DisplayLabel(label));
    if (Button(header)) *open = !*open;
    if (!*open) return false;
    if (size.x <= 0.f) size.x = content_.w;
    PushID(label);
    return BeginContainerImpl(size, false, "collapsible-container");
}

} // namespace fluid