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
    if (sameLine_) {
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

    const ComputedStyle ws = Resolve("window", 0);
    const ComputedStyle ts = Resolve("window-title", 0);
    const std::string_view text = DisplayLabel(title);
    const float titleH = ts.fontSize * 1.2f + ts.padding.Vertical();

    const Rect frame{w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect bar{frame.x, frame.y, frame.w, titleH};

    // Drag by title bar
    const WidgetId dragId = HashStr("##titlebar", id);
    Behavior(dragId, bar);
    if (active_ == dragId) {
        if (!w.dragging) { w.dragging = true; w.dragOff = in_.mousePos - w.pos; }
        w.pos = in_.mousePos - w.dragOff;
    } else {
        w.dragging = false;
    }

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
    sameLine_ = false;
    return true;
}

void Context::EndWindow() {
    draw_.PopClipRect();
    if (!idStack_.empty()) idStack_.pop_back();
}

// ---------------------------------------------------------------- widgets

void Context::Text(std::string_view text) {
    const ComputedStyle s = Resolve("text", 0);
    const Vec2 ts = text_->Measure(text, s.fontSize);
    const Rect r = Place({ts.x + s.padding.Horizontal(), ts.y + s.padding.Vertical()}, s.spacing);
    text_->Emit(draw_, {r.x + s.padding.left, r.y + s.padding.top}, text,
                s.fontSize, Fade(s.foreground, s.opacity));
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

} // namespace fluid