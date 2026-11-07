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
    interactionClips_.clear();
    containerStack_.clear();
    horizontalLayout_ = false;
    sameLine_ = false;
    hasDockPreview_ = false;
    dockLayoutDone_ = false;
    dockSplitters_.clear();
    windowInputEnabled_ = true;
    for (auto& [id, window] : windows_) {
        (void)id;
        window.hasDrawLayer = false;
    }
}

const DrawList& Context::EndFrame() {
    if (const auto focused = windows_.find(focusedWindow_);
        focused != windows_.end() && focused->second.hasDrawLayer) {
        draw_.BringLayerToFront(focused->second.drawLayer);
    }
    const ComputedStyle splitterStyle = Resolve("dock-splitter", 0);
    for (const Rect& splitter : dockSplitters_) {
        const bool hovered = splitter.Expanded(3.f).Contains(in_.mousePos);
        const Color color = hovered || activeDockSplitter_ != 0
            ? splitterStyle.accent : splitterStyle.background;
        draw_.AddRectFilled(splitter, Fade(color, splitterStyle.opacity),
                            std::min(splitter.w, splitter.h) * 0.5f);
    }
    if (hasDockPreview_) {
        const ComputedStyle previewStyle = Resolve("dock-preview", 0);
        draw_.AddRectFilled(dockPreview_, Fade(previewStyle.accent, 0.24f), previewStyle.rounding);
        draw_.AddBox(dockPreview_, Fade(previewStyle.background, 0.08f),
                     Fade(previewStyle.accent, 0.95f), 2.f, previewStyle.rounding);
        DrawTextCentered(dockPreview_, "Dock preview", previewStyle);
    }
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

    const bool insideClip = std::all_of(interactionClips_.begin(), interactionClips_.end(),
        [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
    const bool windowAllowsInput = windowInputEnabled_ || active_ == id;
    res.hovered = windowAllowsInput && insideClip && r.Contains(in_.mousePos) &&
                  (active_ == 0 || active_ == id);
    if (res.hovered && justPressed && active_ == 0) active_ = id;

    if (active_ == id) {
        if (down) {
            res.held = true;
        } else {
            res.pressed = insideClip && r.Contains(in_.mousePos);
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

void Context::SetWindowOpen(std::string_view title, bool open) {
    WindowState& window = windows_[HashStr(title)];
    window.open = open;
    if (open) {
        window.zOrder = ++nextWindowZ_;
        focusedWindow_ = HashStr(title);
        if (window.dock != DockSide::None) {
            auto& dockGroup = dockedWindows_[static_cast<size_t>(window.dock)];
            if (std::find(dockGroup.begin(), dockGroup.end(), HashStr(title)) == dockGroup.end())
                dockGroup.push_back(HashStr(title));
        }
    }
    dockLayoutDone_ = false;
}

std::array<Rect, 5> Context::DockRegions() const {
    const bool left = !dockedWindows_[static_cast<size_t>(DockSide::Left)].empty();
    const bool right = !dockedWindows_[static_cast<size_t>(DockSide::Right)].empty();
    const bool top = !dockedWindows_[static_cast<size_t>(DockSide::Top)].empty();
    const bool bottom = !dockedWindows_[static_cast<size_t>(DockSide::Bottom)].empty();
    const bool vertical = left || right;
    const bool horizontal = top || bottom;
    const float leftWidth = left ? in_.displaySize.x * (horizontal ? 0.3f : 0.5f) : 0.f;
    const float rightWidth = right ? in_.displaySize.x * (horizontal ? 0.3f : 0.5f) : 0.f;
    const float centerWidth = std::max(0.f, in_.displaySize.x - leftWidth - rightWidth);
    const float topHeight = top ? in_.displaySize.y * (vertical ? 0.3f : 0.5f) : 0.f;
    const float bottomHeight = bottom ? in_.displaySize.y * (vertical ? 0.3f : 0.5f) : 0.f;
    std::array<Rect, 5> regions{};
    regions[static_cast<size_t>(DockSide::Left)] = {0.f, 0.f, leftWidth, in_.displaySize.y};
    regions[static_cast<size_t>(DockSide::Right)] =
        {in_.displaySize.x - rightWidth, 0.f, rightWidth, in_.displaySize.y};
    regions[static_cast<size_t>(DockSide::Top)] = {leftWidth, 0.f, centerWidth, topHeight};
    regions[static_cast<size_t>(DockSide::Bottom)] =
        {leftWidth, in_.displaySize.y - bottomHeight, centerWidth, bottomHeight};
    return regions;
}

void Context::LayoutDockedWindows() {
    if (dockLayoutDone_) return;
    dockLayoutDone_ = true;

    for (size_t side = 1; side < dockedWindows_.size(); ++side) {
        auto& ids = dockedWindows_[side];
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](WidgetId id) {
            const auto it = windows_.find(id);
            return it == windows_.end() || !it->second.open ||
                   static_cast<size_t>(it->second.dock) != side;
        }), ids.end());
    }

    const std::array<Rect, 5> regions = DockRegions();
    for (size_t side = 1; side < dockedWindows_.size(); ++side) {
        auto& ids = dockedWindows_[side];
        if (ids.empty()) continue;
        const Rect region = regions[side];

        float totalSpan = 0.f;
        for (WidgetId id : ids) totalSpan += std::max(0.01f, windows_[id].dockSpan);
        if (totalSpan <= 0.f) totalSpan = static_cast<float>(ids.size());

        float offset = 0.f;
        const bool verticalSplit = side == static_cast<size_t>(DockSide::Left) ||
                                   side == static_cast<size_t>(DockSide::Right);
        const float axisSize = verticalSplit ? region.h : region.w;
        for (size_t i = 0; i + 1 < ids.size(); ++i) {
            WindowState& first = windows_[ids[i]];
            WindowState& second = windows_[ids[i + 1]];
            const float span = std::max(0.01f, first.dockSpan);
            const float normalizedOffset = offset + span;
            const float coordinate = (verticalSplit ? region.y : region.x) +
                                     axisSize * normalizedOffset / totalSpan;
            Rect splitter = verticalSplit
                ? Rect{region.x,
                       coordinate - 3.f,
                       region.w, 6.f}
                : Rect{coordinate - 3.f,
                       region.y,
                       6.f, region.h};
            const WidgetId splitterId = HashStr("##dock-splitter",
                static_cast<WidgetId>((side << 24) ^ (i + 1)));
            const Interaction interaction = Behavior(splitterId, splitter.Expanded(4.f));
            const bool verticalAxis = verticalSplit;
            const float currentMouse = verticalAxis ? in_.mousePos.y : in_.mousePos.x;
            if (interaction.held) {
                if (activeDockSplitter_ != splitterId) {
                    activeDockSplitter_ = splitterId;
                    dockSplitterMouseStart_ = currentMouse;
                    dockSplitterSpanStart_ = first.dockSpan;
                    dockSplitterPairSpan_ = first.dockSpan + second.dockSpan;
                }
                const float delta = (currentMouse - dockSplitterMouseStart_) /
                                    std::max(1.f, axisSize) * totalSpan;
                const float minSpan = std::min(totalSpan * 0.08f, dockSplitterPairSpan_ * 0.49f);
                first.dockSpan = std::clamp(dockSplitterSpanStart_ + delta,
                                            minSpan, dockSplitterPairSpan_ - minSpan);
                second.dockSpan = dockSplitterPairSpan_ - first.dockSpan;
                totalSpan = 0.f;
                for (WidgetId id : ids) totalSpan += std::max(0.01f, windows_[id].dockSpan);
            } else if (activeDockSplitter_ == splitterId && !in_.mouseDown[0]) {
                activeDockSplitter_ = 0;
            }
            dockSplitters_.push_back(splitter);
            offset += first.dockSpan;
        }

        totalSpan = 0.f;
        for (WidgetId id : ids) totalSpan += std::max(0.01f, windows_[id].dockSpan);
        offset = 0.f;
        for (WidgetId id : ids) {
            WindowState& window = windows_[id];
            const float span = std::max(0.01f, window.dockSpan);
            if (verticalSplit) {
                const float panelH = region.h * span / totalSpan;
                window.pos = {region.x, region.y + offset};
                window.size = {region.w, panelH};
            } else {
                const float panelW = region.w * span / totalSpan;
                window.pos = {region.x + offset, region.y};
                window.size = {panelW, region.h};
            }
            offset += verticalSplit ? window.size.y : window.size.x;
        }
    }
}

bool Context::BeginWindow(std::string_view title, Rect initial) {
    LayoutDockedWindows();
    const WidgetId id = GetID(title);
    WindowState& w = windows_[id];
    if (!w.init) {
        w.init = true;
        w.pos = {initial.x, initial.y};
        w.size = {initial.w, initial.h};
        w.zOrder = ++nextWindowZ_;
    }
    if (!w.open) return false;

    WidgetId topWindowAtMouse = 0;
    uint64_t topWindowZ = 0;
    if (in_.mousePos.x >= 0.f && in_.mousePos.y >= 0.f &&
        in_.mousePos.x < in_.displaySize.x && in_.mousePos.y < in_.displaySize.y) {
        for (const auto& [windowId, candidate] : windows_) {
            if (!candidate.open || !candidate.init) continue;
            const Rect candidateFrame{candidate.pos.x, candidate.pos.y,
                                      candidate.size.x, candidate.size.y};
            if (candidateFrame.Contains(in_.mousePos) && candidate.zOrder >= topWindowZ) {
                topWindowAtMouse = windowId;
                topWindowZ = candidate.zOrder;
            }
        }
    }
    windowInputEnabled_ = topWindowAtMouse == id;
    const bool justPressed = in_.mouseDown[0] && !prevMouseDown_;
    if (justPressed && windowInputEnabled_) {
        w.zOrder = ++nextWindowZ_;
        focusedWindow_ = id;
    }

    const ComputedStyle ws = Resolve("window", 0);
    const ComputedStyle ts = Resolve("window-title", 0);
    const std::string_view text = DisplayLabel(title);
    const float titleH = ts.fontSize * 1.2f + ts.padding.Vertical();
    constexpr float controlWidth = 25.f;
    constexpr float resizeSize = 18.f;

    Rect frame{w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect closeRect{frame.x + frame.w - controlWidth - ts.padding.right, frame.y,
                         controlWidth, titleH};
    const Rect dragRect{frame.x, frame.y, std::max(0.f, closeRect.x - frame.x), titleH};
    const WidgetId dragId = HashStr("##titlebar", id);
    const WidgetId resizeId = HashStr("##resize", id);
    const bool wasDragging = w.dragging;
    const Interaction drag = Behavior(dragId, dragRect);
    if (drag.held) {
        if (!wasDragging) {
            w.dragging = true;
            for (auto& sideWindows : dockedWindows_)
                sideWindows.erase(std::remove(sideWindows.begin(), sideWindows.end(), id), sideWindows.end());
            w.dock = DockSide::None;
            dockLayoutDone_ = false;
            LayoutDockedWindows();
            w.dragOff = in_.mousePos - w.pos;
        }
        w.pos = in_.mousePos - w.dragOff;
    } else if (wasDragging && !in_.mouseDown[0]) {
        if (dockingEnabled_) {
            constexpr float dockThreshold = 32.f;
            if (in_.mousePos.x <= dockThreshold) w.dock = DockSide::Left;
            else if (in_.mousePos.x >= in_.displaySize.x - dockThreshold) w.dock = DockSide::Right;
            else if (in_.mousePos.y <= dockThreshold) w.dock = DockSide::Top;
            else if (in_.mousePos.y >= in_.displaySize.y - dockThreshold) w.dock = DockSide::Bottom;
            if (w.dock != DockSide::None) {
                auto& dockGroup = dockedWindows_[static_cast<size_t>(w.dock)];
                if (std::find(dockGroup.begin(), dockGroup.end(), id) == dockGroup.end()) {
                    float existingSpan = 0.f;
                    for (WidgetId dockedId : dockGroup)
                        existingSpan += std::max(0.01f, windows_[dockedId].dockSpan);
                    w.dockSpan = dockGroup.empty()
                        ? 1.f : existingSpan / static_cast<float>(dockGroup.size());
                    dockGroup.push_back(id);
                }
                dockLayoutDone_ = false;
                LayoutDockedWindows();
            }
        }
        w.dragging = false;
    }

    frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
    Rect resizeRect{frame.x + frame.w - resizeSize, frame.y + frame.h - resizeSize,
                    resizeSize, resizeSize};
    const Interaction resize = w.dock == DockSide::None
        ? Behavior(resizeId, resizeRect) : Interaction{};
    if (resize.held) {
        if (!w.resizing) {
            w.resizing = true;
            for (auto& sideWindows : dockedWindows_)
                sideWindows.erase(std::remove(sideWindows.begin(), sideWindows.end(), id), sideWindows.end());
            w.dock = DockSide::None;
            w.resizeStart = in_.mousePos;
            w.startSize = w.size;
        }
        w.size.x = std::max(220.f, w.startSize.x + in_.mousePos.x - w.resizeStart.x);
        w.size.y = std::max(titleH + 100.f, w.startSize.y + in_.mousePos.y - w.resizeStart.y);
    } else if (w.resizing && !in_.mouseDown[0]) {
        w.resizing = false;
    }
    frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect bar{frame.x, frame.y, frame.w, titleH};
    const Rect finalClose{frame.x + frame.w - controlWidth - ts.padding.right, frame.y,
                          controlWidth, titleH};

    const Interaction close = Behavior(HashStr("##close", id), finalClose);
    if (close.pressed) {
        w.open = false;
        if (focusedWindow_ == id) focusedWindow_ = 0;
        for (auto& sideWindows : dockedWindows_)
            sideWindows.erase(std::remove(sideWindows.begin(), sideWindows.end(), id), sideWindows.end());
        dockLayoutDone_ = false;
        windowInputEnabled_ = true;
        return false;
    }

    w.drawLayer = draw_.BeginLayer();
    w.hasDrawLayer = true;
    DrawBox(frame, ws);
    draw_.AddRectFilled(bar, Fade(ts.background, ts.opacity), ws.rounding);
    const Rect titleRect{bar.x + ts.padding.left, bar.y,
                         std::max(0.f, bar.w - ts.padding.Horizontal()), bar.h};
    DrawTextCentered(titleRect, text, ts);

    const WidgetId closeId = HashStr("##close", id);
    ComputedStyle closeStyle = Resolve("window-close", 0);
    closeStyle = Animate(closeId, "window-close", State_None,
                         windowInputEnabled_ && finalClose.Contains(in_.mousePos),
                         active_ == closeId && in_.mouseDown[0]);
    draw_.AddRectFilled(finalClose, Fade(closeStyle.background, closeStyle.opacity),
                        closeStyle.rounding);
    DrawTextCentered(finalClose, "x", closeStyle);

    if (w.dragging && dockingEnabled_) {
        constexpr float dockThreshold = 32.f;
        DockSide previewSide = DockSide::None;
        if (in_.mousePos.x <= dockThreshold) previewSide = DockSide::Left;
        else if (in_.mousePos.x >= in_.displaySize.x - dockThreshold) previewSide = DockSide::Right;
        else if (in_.mousePos.y <= dockThreshold) previewSide = DockSide::Top;
        else if (in_.mousePos.y >= in_.displaySize.y - dockThreshold) previewSide = DockSide::Bottom;
        if (previewSide != DockSide::None) {
            const bool verticalEdges = previewSide == DockSide::Left || previewSide == DockSide::Right ||
                !dockedWindows_[static_cast<size_t>(DockSide::Left)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::Right)].empty();
            const bool horizontalEdges = previewSide == DockSide::Top || previewSide == DockSide::Bottom ||
                !dockedWindows_[static_cast<size_t>(DockSide::Top)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::Bottom)].empty();
            const bool hasLeft = previewSide == DockSide::Left ||
                !dockedWindows_[static_cast<size_t>(DockSide::Left)].empty();
            const bool hasRight = previewSide == DockSide::Right ||
                !dockedWindows_[static_cast<size_t>(DockSide::Right)].empty();
            const bool hasTop = previewSide == DockSide::Top ||
                !dockedWindows_[static_cast<size_t>(DockSide::Top)].empty();
            const bool hasBottom = previewSide == DockSide::Bottom ||
                !dockedWindows_[static_cast<size_t>(DockSide::Bottom)].empty();
            const float leftWidth = hasLeft ? in_.displaySize.x * (horizontalEdges ? 0.3f : 0.5f) : 0.f;
            const float rightWidth = hasRight ? in_.displaySize.x * (horizontalEdges ? 0.3f : 0.5f) : 0.f;
            const float centerWidth = std::max(0.f, in_.displaySize.x - leftWidth - rightWidth);
            const float topHeight = hasTop ? in_.displaySize.y * (verticalEdges ? 0.3f : 0.5f) : 0.f;
            const float bottomHeight = hasBottom ? in_.displaySize.y * (verticalEdges ? 0.3f : 0.5f) : 0.f;
            Rect preview;
            if (previewSide == DockSide::Left)
                preview = {0.f, 0.f, leftWidth, in_.displaySize.y};
            else if (previewSide == DockSide::Right)
                preview = {in_.displaySize.x - rightWidth, 0.f, rightWidth, in_.displaySize.y};
            else if (previewSide == DockSide::Top)
                preview = {leftWidth, 0.f, centerWidth, topHeight};
            else
                preview = {leftWidth, in_.displaySize.y - bottomHeight, centerWidth, bottomHeight};
            dockPreview_ = preview;
            hasDockPreview_ = true;
        }
    }

    if (w.dock == DockSide::None) {
        const ComputedStyle gripStyle = Resolve("window-resize", 0);
        for (int i = 0; i < 3; ++i) {
            const float offset = static_cast<float>(i * 5 + 4);
            draw_.AddRectFilled({frame.x + frame.w - offset - 2.f,
                                 frame.y + frame.h - 4.f, 2.f,
                                 std::min(offset - 1.f, 8.f)},
                                Fade(gripStyle.accent, gripStyle.opacity), 1.f);
        }
    }

    draw_.PushClipRect(frame);
    interactionClips_.push_back(frame);
    idStack_.push_back(id);
    currentWindowId_ = id;

    const float scrollBarWidth = 12.f;
    content_ = {frame.x + ws.padding.left,
                frame.y + titleH + ws.padding.top,
                frame.w - ws.padding.Horizontal() - scrollBarWidth,
                frame.h - titleH - ws.padding.Vertical()};
    windowViewport_ = content_;
    if (in_.mouseWheel != 0.f && content_.Contains(in_.mousePos))
        w.scrollY = std::max(0.f, w.scrollY - in_.mouseWheel * ts.fontSize * 3.f);
    const float maxScroll = std::max(0.f, w.contentHeight - windowViewport_.h);
    w.scrollY = std::clamp(w.scrollY, 0.f, maxScroll);
    content_.y -= w.scrollY;
    cursorY_ = content_.y;
    cursorX_ = content_.x;
    lineTop_ = content_.y;
    lineBottom_ = content_.y;
    lastRight_ = content_.x;
    sameLine_ = false;
    horizontalLayout_ = false;
    draw_.PushClipRect(windowViewport_);
    interactionClips_.push_back(windowViewport_);
    return true;
}

void Context::EndWindow() {
    while (!containerStack_.empty()) EndContainer();
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    if (currentWindowId_ != 0) {
        WindowState& window = windows_[currentWindowId_];
        const float extent = std::max(lineBottom_, cursorY_ - Resolve("text", 0).spacing);
        window.contentHeight = std::max(0.f, extent - (windowViewport_.y - window.scrollY));
        const float maxScroll = std::max(0.f, window.contentHeight - windowViewport_.h);
        window.scrollY = std::clamp(window.scrollY, 0.f, maxScroll);
        if (maxScroll > 0.f) {
            const ComputedStyle trackStyle = Resolve("window-scrollbar", 0);
            const float trackWidth = 7.f;
            const Rect track{windowViewport_.x + windowViewport_.w + 2.f, windowViewport_.y,
                             trackWidth, windowViewport_.h};
            draw_.AddRectFilled(track, Fade(trackStyle.background, trackStyle.opacity),
                                trackWidth * 0.5f);
            const float thumbH = std::max(28.f, track.h * track.h / window.contentHeight);
            float thumbY = track.y + (track.h - thumbH) *
                (maxScroll > 0.f ? window.scrollY / maxScroll : 0.f);
            const Rect thumb{track.x, thumbY, track.w, thumbH};
            const WidgetId scrollId = HashStr("##scrollbar", currentWindowId_);
            const Interaction scroll = Behavior(scrollId, thumb.Expanded(3.f));
            if (scroll.held) {
                if (!window.scrolling) {
                    window.scrolling = true;
                    window.scrollDragOffset = in_.mousePos.y - thumb.y;
                }
                const float travel = std::max(1.f, track.h - thumbH);
                const float ratio = std::clamp((in_.mousePos.y - window.scrollDragOffset - track.y) /
                                               travel, 0.f, 1.f);
                window.scrollY = ratio * maxScroll;
                thumbY = track.y + travel * ratio;
            } else if (window.scrolling && !in_.mouseDown[0]) {
                window.scrolling = false;
            }
            draw_.AddRectFilled({thumb.x, thumbY, thumb.w, thumb.h},
                                Fade(trackStyle.accent, trackStyle.opacity), trackWidth * 0.5f);
        }
        if (window.scrolling && !in_.mouseDown[0]) window.scrolling = false;
    }
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    if (!idStack_.empty()) idStack_.pop_back();
    if (currentWindowId_ != 0) {
        WindowState& window = windows_[currentWindowId_];
        draw_.EndLayer(window.drawLayer);
    }
    currentWindowId_ = 0;
    windowInputEnabled_ = true;
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
    interactionClips_.push_back(frame);
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
    if (!interactionClips_.empty()) interactionClips_.pop_back();
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

bool Context::ImageButton(std::string_view label, GPUImageId imageId, Vec2 imageSize) {
    const WidgetId id = GetID(label);
    const ComputedStyle base = Resolve("image-button", 0);
    const Rect r = Place({imageSize.x + base.padding.Horizontal(), imageSize.y + base.padding.Vertical()},
                         base.spacing);
    const Interaction b = Behavior(id, r);
    const ComputedStyle style = Animate(id, "image-button", State_None, b.hovered, b.held);
    DrawBox(r, style);
    draw_.AddQuad({r.x + style.padding.left, r.y + style.padding.top, imageSize.x, imageSize.y},
                  {0.f, 0.f, 1.f, 1.f}, Fade(Color{}, style.opacity), imageId);
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

void Context::Image(GPUImageId imageId, Vec2 size) {
    const Rect r = Place(size, Resolve("image", 0).spacing);
    draw_.AddQuad(r, {0.f, 0.f, 1.f, 1.f}, Color{}, imageId);
}

void Context::ImageLabel(std::string_view label, GPUImageId imageId, Vec2 imageSize) {
    const ComputedStyle style = Resolve("image-label", 0);
    const Vec2 textSize = text_->Measure(label, style.fontSize);
    const Rect r = Place({imageSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(imageSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    draw_.AddQuad({r.x + style.padding.left, r.y + (r.h - imageSize.y) * 0.5f,
                   imageSize.x, imageSize.y}, {0.f, 0.f, 1.f, 1.f}, Color{}, imageId);
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