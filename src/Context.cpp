#include "FluidUI/Context.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

namespace fluid {

namespace {

std::string_view DisplayLabel(std::string_view s) {
    const size_t p = s.find("##");
    return p == std::string_view::npos ? s : s.substr(0, p);
}

Color Fade(Color c, float opacity) { c.a *= opacity; return c; }

constexpr uint8_t kResizeLeft = 1u << 0;
constexpr uint8_t kResizeRight = 1u << 1;
constexpr uint8_t kResizeTop = 1u << 2;
constexpr uint8_t kResizeBottom = 1u << 3;
constexpr float kDockTargetSize = 28.f;
constexpr float kDockTargetGap = 4.f;
constexpr float kDockCompassSize = 104.f;

uint8_t ResizeEdgesAt(Rect frame, Vec2 position) {
    constexpr float edgeSize = 8.f;
    if (!frame.Contains(position)) return 0;
    uint8_t edges = 0;
    if (position.x - frame.x <= edgeSize) edges |= kResizeLeft;
    else if (frame.x + frame.w - position.x <= edgeSize) edges |= kResizeRight;
    if (position.y - frame.y <= edgeSize) edges |= kResizeTop;
    else if (frame.y + frame.h - position.y <= edgeSize) edges |= kResizeBottom;
    return edges;
}

CursorShape CursorForResize(uint8_t edges) {
    const bool horizontal = (edges & (kResizeLeft | kResizeRight)) != 0;
    const bool vertical = (edges & (kResizeTop | kResizeBottom)) != 0;
    if (horizontal && vertical) {
        const bool sameDiagonal =
            ((edges & kResizeLeft) && (edges & kResizeTop)) ||
            ((edges & kResizeRight) && (edges & kResizeBottom));
        return sameDiagonal ? CursorShape::ResizeDiagonalNWSE
                            : CursorShape::ResizeDiagonalNESW;
    }
    return horizontal ? CursorShape::ResizeHorizontal : CursorShape::ResizeVertical;
}

} // namespace

Context::Context() : text_(&defaultText_) {}
Context::~Context() = default;

void Context::SetTextBackend(ITextBackend* backend) { text_ = backend ? backend : &defaultText_; }

// ---------------------------------------------------------------- frame

void Context::NewFrame(const InputState& input) {
    in_ = input;
    timeSeconds_ += std::max(0.f, input.deltaTime);
    cursorShape_ = CursorShape::Arrow;
    const bool menuClickOutside = in_.mouseDown[0] && !prevMouseDown_ &&
        !openMenus_.empty() && in_.mousePos.y >= menuBarHeight_ &&
        std::none_of(menuPopupRects_.begin(), menuPopupRects_.end(),
            [&](const Rect& popup) { return popup.Contains(in_.mousePos); });
    if (menuClickOutside) openMenus_.clear();
    menuPopupRects_.clear();
    draw_.Clear();
    draw_.PushClipRect({0.f, 0.f, in_.displaySize.x, in_.displaySize.y});
    idStack_.clear();
    classStack_.clear();
    interactionClips_.clear();
    containerStack_.clear();
    menuStack_.clear();
    menuBarActive_ = false;
    menuBarLayerActive_ = false;
    menuBarHeight_ = 0.f;
    horizontalLayout_ = false;
    sameLine_ = false;
    hasDockPreview_ = false;
    dockCompassVisible_ = false;
    dockCompassSelection_ = DockSide::None;
    dockLayoutDone_ = false;
    dockSplitters_.clear();
    tabDropTarget_ = 0;
    tabDropPreview_ = {};
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
    if (menuBarLayerActive_) draw_.BringLayerToFront(menuBarLayer_);
    for (const DockSplitter& dockSplitter : dockSplitters_) {
        const Rect& splitter = dockSplitter.rect;
        const bool vertical = splitter.h >= splitter.w;
        const bool active = activeDockSplitter_ == dockSplitter.id;
        const bool hovered = splitter.Expanded(4.f).Contains(in_.mousePos) &&
                             !IsFloatingWindowAt(in_.mousePos);
        if (hovered || active)
            cursorShape_ = vertical ? CursorShape::ResizeHorizontal
                                    : CursorShape::ResizeVertical;
    }
    if (hasDockPreview_) {
        draw_.PushClipRect(dockPreview_);
        const ComputedStyle dockStyle = Resolve("dock-menu", 0);
        ComputedStyle paneStyle = Resolve("window", 0);
        paneStyle.opacity = 0.78f;
        paneStyle.shadowBlur = 0.f;
        paneStyle.shadowColor = {};
        paneStyle.shadowColorGradient = {};
        paneStyle.borderColor = dockStyle.borderColor;
        paneStyle.borderColorGradient = dockStyle.borderColorGradient;
        paneStyle.borderWidth = dockStyle.borderWidth;
        DrawBox(dockPreview_, paneStyle);
        ComputedStyle titleStyle = Resolve("window-title", 0);
        titleStyle.opacity *= 0.9f;
        const float titleHeight = std::min(dockPreview_.h,
            titleStyle.fontSize * 1.2f + titleStyle.padding.Vertical());
        const Rect title{dockPreview_.x, dockPreview_.y, dockPreview_.w, titleHeight};
        DrawStyledColor(title, titleStyle.background, titleStyle.backgroundGradient,
                        titleStyle.opacity, titleStyle.cornerRadii);
        const ComputedStyle textStyle = Resolve("text", 0);
        const float inset = std::max(8.f, textStyle.padding.left);
        const float lineHeight = std::max(2.f, titleStyle.fontSize * 0.16f);
        const float availableWidth = std::max(0.f, dockPreview_.w - inset * 2.f);
        for (int i = 0; i < 3; ++i) {
            const float lineWidth = availableWidth * (i == 1 ? 0.62f : 0.86f);
            const Rect line{dockPreview_.x + inset,
                            dockPreview_.y + titleHeight + inset + i * (lineHeight + 7.f),
                            lineWidth, lineHeight};
            DrawStyledColor(line, textStyle.foreground, textStyle.textGradient,
                            paneStyle.opacity * 0.3f, lineHeight * 0.5f);
        }
        draw_.PopClipRect();
    }
    if (tabDropTarget_ != 0) {
        const auto target = windows_.find(tabDropTarget_);
        if (target != windows_.end()) {
            const Rect rect{target->second.pos.x, target->second.pos.y,
                            target->second.size.x,
                            std::min(target->second.size.y,
                                     target->second.titleHeight)};
            const ComputedStyle style = Resolve("dock-tab", State_Checked);
            constexpr float border = 3.f;
            DrawStyledColor({rect.x, rect.y, rect.w, border},
                            style.accent, style.accentGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x, rect.y + rect.h - border, rect.w, border},
                            style.accent, style.accentGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x, rect.y + border, border, rect.h - border * 2.f},
                            style.accent, style.accentGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x + rect.w - border, rect.y + border, border,
                             rect.h - border * 2.f},
                            style.accent, style.accentGradient, style.opacity, border * 0.5f);
        }
    }
    if (dockCompassVisible_) {
        std::vector<std::string> dockClasses = classStack_;
        dockClasses.emplace_back("docking-options");
        const ComputedStyle menuStyle = style_.Resolve("dock-menu", dockClasses, 0);
        DrawBox(dockCompassRect_, menuStyle);
        constexpr DockSide sides[] = {
            DockSide::TopLeft, DockSide::Top, DockSide::TopRight,
            DockSide::Left, DockSide::Center, DockSide::Right,
            DockSide::BottomLeft, DockSide::Bottom, DockSide::BottomRight
        };
        for (DockSide side : sides) {
            const Rect target = DockTargetRect(side);
            const bool hovered = target.Contains(in_.mousePos);
            ComputedStyle style = style_.Resolve(
                "dock-target", dockClasses, hovered ? State_Hovered : State_None);
            DrawBox(target, style);
            const Rect icon{target.x + 5.f, target.y + 5.f,
                            target.w - 10.f, target.h - 10.f};
            const float cellGap = 1.f;
            const float cellSize = (icon.w - 2.f * cellGap) / 3.f;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    const DockSide cellSide = sides[row * 3 + column];
                    const bool selectedCell = side == cellSide;
                    const Color cellColor = selectedCell
                        ? style.accent : style.background;
                    draw_.AddRectFilled(
                        {icon.x + column * (cellSize + cellGap),
                         icon.y + row * (cellSize + cellGap), cellSize, cellSize},
                        Fade(cellColor, style.opacity), 1.f);
                }
            }
        }
    }
    draw_.PopClipRect();
    prevMouseDown_ = in_.mouseDown[0];
    if (!in_.mouseDown[0]) {
        active_ = 0;
        activeDockSplitter_ = 0;
    }
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

bool Context::IsFloatingWindowAt(Vec2 position) const {
    for (const auto& [id, window] : windows_) {
        (void)id;
        if (!window.open || !window.init || window.dock != DockSide::None) continue;
        if (Rect{window.pos.x, window.pos.y, window.size.x, window.size.y}.Contains(position))
            return true;
    }
    return false;
}

WidgetId Context::WindowTitleAt(Vec2 position, WidgetId exclude) const {
    WidgetId hit = 0;
    uint64_t topZ = 0;
    const WindowState* topWindow = nullptr;
    const ComputedStyle fallbackTitleStyle = style_.Resolve("window-title", {}, 0);
    const float fallbackTitleHeight =
        fallbackTitleStyle.fontSize * 1.2f + fallbackTitleStyle.padding.Vertical();
    const auto& centerGroup = dockedWindows_[static_cast<size_t>(DockSide::Center)];
    for (const auto& [id, window] : windows_) {
        if (id == exclude || !window.open || !window.init) continue;
        if (window.dock == DockSide::Center && centerGroup.size() > 1 &&
            activeCenterTab_ != id)
            continue;
        if (window.tabGroup != 0) {
            const auto group = tabGroups_.find(window.tabGroup);
            if (group != tabGroups_.end() && group->second.active != id) continue;
        }
        if (Rect{window.pos.x, window.pos.y, window.size.x, window.size.y}
                .Contains(position) && window.zOrder >= topZ) {
            hit = id;
            topZ = window.zOrder;
            topWindow = &window;
        }
    }
    if (topWindow) {
        const float titleHeight = topWindow->titleHeight > 0.f
            ? topWindow->titleHeight : fallbackTitleHeight;
        if (position.y >= topWindow->pos.y + std::min(topWindow->size.y, titleHeight))
            return 0;
    }
    return hit;
}

void Context::DetachTab(WidgetId id) {
    auto window = windows_.find(id);
    if (window == windows_.end()) return;
    for (auto& sideWindows : dockedWindows_)
        sideWindows.erase(std::remove(sideWindows.begin(), sideWindows.end(), id),
                          sideWindows.end());
    if (window->second.dock == DockSide::Center) activeCenterTab_ = 0;
    if (window->second.tabGroup != 0) {
        const WidgetId groupId = window->second.tabGroup;
        auto group = tabGroups_.find(groupId);
        if (group != tabGroups_.end()) {
            auto& members = group->second.windows;
            members.erase(std::remove(members.begin(), members.end(), id), members.end());
            if (group->second.active == id)
                group->second.active = members.empty() ? 0 : members.front();
            if (members.size() < 2) {
                for (WidgetId member : members) {
                    if (auto remaining = windows_.find(member); remaining != windows_.end())
                        remaining->second.tabGroup = 0;
                }
                tabGroups_.erase(group);
            }
        }
        window->second.tabGroup = 0;
    }
    dockLayoutDone_ = false;
}

void Context::JoinTab(WidgetId id, WidgetId target) {
    if (id == target) return;
    auto dragged = windows_.find(id);
    auto targetWindow = windows_.find(target);
    if (dragged == windows_.end() || targetWindow == windows_.end() ||
        !dragged->second.open || !targetWindow->second.open)
        return;

    const DockSide targetDock = targetWindow->second.dock;
    if (targetDock == DockSide::Center) {
        auto& center = dockedWindows_[static_cast<size_t>(DockSide::Center)];
        if (std::find(center.begin(), center.end(), id) != center.end()) return;
        if (dragged->second.dock == DockSide::None)
            dragged->second.dockRestoreSize = dragged->second.size;
        DetachTab(id);
        dragged->second.dock = DockSide::Center;
        dragged->second.tabGroup = 0;
        center.push_back(id);
        activeCenterTab_ = id;
        dockLayoutDone_ = false;
        return;
    }

    if (dragged->second.tabGroup != 0 &&
        dragged->second.tabGroup == targetWindow->second.tabGroup)
        return;

    const DockSide draggedDock = dragged->second.dock;
    const Vec2 restoreSize = dragged->second.size;
    if (draggedDock == DockSide::None) dragged->second.dockRestoreSize = restoreSize;
    DetachTab(id);

    targetWindow = windows_.find(target);
    if (targetWindow == windows_.end()) return;
    WidgetId groupId = targetWindow->second.tabGroup;
    if (groupId == 0) {
        groupId = target;
        TabGroup group;
        group.windows.push_back(target);
        group.active = target;
        tabGroups_.emplace(groupId, std::move(group));
        targetWindow->second.tabGroup = groupId;
    }

    TabGroup& group = tabGroups_[groupId];
    if (std::find(group.windows.begin(), group.windows.end(), id) == group.windows.end())
        group.windows.push_back(id);
    group.active = id;
    dragged->second.tabGroup = groupId;
    dragged->second.dock = targetWindow->second.dock;
    if (dragged->second.dock != DockSide::None) {
        auto& dockGroup = dockedWindows_[static_cast<size_t>(dragged->second.dock)];
        if (std::find(dockGroup.begin(), dockGroup.end(), id) == dockGroup.end())
            dockGroup.push_back(id);
    }
    dragged->second.pos = targetWindow->second.pos;
    dragged->second.size = targetWindow->second.size;
    dragged->second.zOrder = ++nextWindowZ_;
    focusedWindow_ = id;
    dockLayoutDone_ = false;
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

void Context::DrawStyledColor(Rect r, Color color, const LinearGradient& gradient,
                              float opacity, CornerRadii rounding) {
    if (gradient.Empty()) draw_.AddRectFilled(r, Fade(color, opacity), rounding);
    else draw_.AddRectFilledGradient(r, gradient, opacity, rounding);
}

namespace {

CornerRadii ExpandRadii(CornerRadii radii, float amount) {
    return {
        std::max(0.f, radii.topLeft + amount),
        std::max(0.f, radii.topRight + amount),
        std::max(0.f, radii.bottomRight + amount),
        std::max(0.f, radii.bottomLeft + amount)
    };
}

} // namespace

void Context::DrawBox(Rect r, const ComputedStyle& s) {
    if (s.shadowEnabled) {
        if (!s.shadowColorGradient.Empty())
            draw_.AddShadowGradient(r, s.shadowColorGradient, s.opacity,
                                    s.shadowBlur, s.cornerRadii);
        else
            draw_.AddShadow(r, Fade(s.shadowColor, s.opacity), s.shadowBlur, s.cornerRadii);
    }
    draw_.AddRectFilled(r, Fade(s.background, s.opacity), s.cornerRadii);
    draw_.AddRectFilledGradient(r, s.backgroundGradient, s.opacity, s.cornerRadii);
    DrawBackgroundImage(r, s);
    if (!s.borderColorGradient.Empty())
        draw_.AddBorderGradient(r, s.borderColorGradient, s.opacity,
                                s.borderWidth, s.cornerRadii);
    else if (s.borderWidth > 0.f && s.borderColor.a > 0.f)
        draw_.AddBox(r, {0.f, 0.f, 0.f, 0.f}, Fade(s.borderColor, s.opacity),
                     s.borderWidth, s.cornerRadii);
    DrawOutline(r, s);
}

void Context::DrawOutline(Rect r, const ComputedStyle& s) {
    if (!s.outlineEnabled || s.outlineWidth <= 0.f) return;
    const Rect outline = r.Expanded(s.outlineWidth);
    const CornerRadii rounding = ExpandRadii(s.cornerRadii, s.outlineWidth);
    if (!s.outlineColorGradient.Empty())
        draw_.AddBorderGradient(outline, s.outlineColorGradient, s.opacity,
                                s.outlineWidth, rounding);
    else {
        const Color color = s.outlineColorSet ? s.outlineColor : s.foreground;
        if (color.a > 0.f)
            draw_.AddBox(outline, {0.f, 0.f, 0.f, 0.f},
                         Fade(color, s.opacity), s.outlineWidth, rounding);
    }
}

void Context::EmitText(Vec2 pos, std::string_view text, const ComputedStyle& style) {
    if (style.textGradient.Empty()) {
        text_->Emit(draw_, pos, text, style.fontSize, Fade(style.foreground, style.opacity));
        return;
    }

    const Vec2 size = text_->Measure(text, style.fontSize);
    const size_t vertexStart = draw_.Vertices().size();
    text_->Emit(draw_, pos, text, style.fontSize, Color{1.f, 1.f, 1.f, 1.f});
    draw_.ApplyLinearGradient(vertexStart, draw_.Vertices().size(),
                              {pos.x, pos.y, size.x, size.y},
                              style.textGradient, style.opacity);
}

void Context::DrawBackgroundImage(Rect r, const ComputedStyle& s) {
    if (s.backgroundImage == 0 || r.w <= 0.f || r.h <= 0.f) return;

    Rect imageRect = r;
    Rect uv{0.f, 0.f, 1.f, 1.f};
    const float imageW = s.backgroundImageSize.x;
    const float imageH = s.backgroundImageSize.y;
    if (imageW > 0.f && imageH > 0.f) {
        const float imageAspect = imageW / imageH;
        const float regionAspect = r.w / r.h;
        if (s.backgroundFit == BackgroundFit::Cover) {
            if (imageAspect > regionAspect) {
                uv.w = regionAspect / imageAspect;
                uv.x = (1.f - uv.w) * s.backgroundPosition.x;
            } else {
                uv.h = imageAspect / regionAspect;
                uv.y = (1.f - uv.h) * s.backgroundPosition.y;
            }
        } else if (s.backgroundFit == BackgroundFit::Contain) {
            const float scale = std::min(r.w / imageW, r.h / imageH);
            imageRect.w = imageW * scale;
            imageRect.h = imageH * scale;
            imageRect.x += (r.w - imageRect.w) * s.backgroundPosition.x;
            imageRect.y += (r.h - imageRect.h) * s.backgroundPosition.y;
        }
    }

    draw_.PushClipRect(r);
    draw_.AddQuad(imageRect, uv, {1.f, 1.f, 1.f, s.opacity}, s.backgroundImage);
    draw_.PopClipRect();
}

Context::Interaction Context::DrawMenuButton(Rect rect, std::string_view label, WidgetId id,
                                             std::string_view widget, bool selected) {
    const Interaction interaction = Behavior(id, rect);
    const ComputedStyle style = Animate(id, widget,
        selected ? State_Checked : State_None, interaction.hovered, interaction.held);
    DrawBox(rect, style);
    const Rect textRect{rect.x + style.padding.left, rect.y,
                        std::max(0.f, rect.w - style.padding.Horizontal()), rect.h};
    DrawTextCentered(textRect, label, style);
    return interaction;
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
    const WidgetId id = HashStr(title);
    WindowState& window = windows_[id];
    window.open = open;
    if (open) {
        window.zOrder = ++nextWindowZ_;
        focusedWindow_ = id;
        if (window.dock != DockSide::None) {
            auto& dockGroup = dockedWindows_[static_cast<size_t>(window.dock)];
            if (std::find(dockGroup.begin(), dockGroup.end(), id) == dockGroup.end())
                dockGroup.push_back(id);
        }
    }
    dockLayoutDone_ = false;
}

bool Context::SaveLayout(const std::string& path, std::string* error) const {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        if (error) *error = "Could not open layout file for writing: " + path;
        return false;
    }

    size_t windowCount = 0;
    std::vector<std::pair<WidgetId, TabGroup>> savedTabGroups;
    std::unordered_set<WidgetId> savedTabMembers;
    for (const auto& [id, window] : windows_) {
        if (window.init) ++windowCount;
        (void)id;
    }
    for (const auto& [groupId, group] : tabGroups_) {
        TabGroup savedGroup;
        for (WidgetId member : group.windows) {
            const auto window = windows_.find(member);
            if (window != windows_.end() && window->second.init && window->second.open &&
                window->second.tabGroup == groupId)
                savedGroup.windows.push_back(member);
        }
        if (savedGroup.windows.size() < 2) continue;
        savedGroup.active = std::find(savedGroup.windows.begin(), savedGroup.windows.end(),
                                      group.active) != savedGroup.windows.end()
            ? group.active : savedGroup.windows.front();
        for (WidgetId member : savedGroup.windows) savedTabMembers.insert(member);
        savedTabGroups.emplace_back(groupId, std::move(savedGroup));
    }

    file.precision(std::numeric_limits<float>::max_digits10);
    file << "FLUIDUI_LAYOUT 3\n"
         << "ratios " << dockLeftRatio_ << ' ' << dockRightRatio_ << ' '
         << dockTopRatio_ << ' ' << dockBottomRatio_ << '\n'
         << "focus " << focusedWindow_ << '\n'
         << "center-tab " << activeCenterTab_ << '\n'
         << "windows " << windowCount << '\n';

    for (const auto& [id, window] : windows_) {
        if (!window.init) continue;
        file << "window " << id << ' ' << (window.open ? 1 : 0) << ' '
             << window.pos.x << ' ' << window.pos.y << ' '
             << window.size.x << ' ' << window.size.y << ' '
             << static_cast<unsigned int>(window.dock) << ' ' << window.dockSpan << ' '
             << window.dockRestoreSize.x << ' ' << window.dockRestoreSize.y << ' '
             << window.scrollY << ' ' << window.zOrder << ' '
             << (savedTabMembers.contains(id) ? window.tabGroup : 0) << '\n';
    }

    for (size_t side = 1; side < dockedWindows_.size(); ++side) {
        const auto& group = dockedWindows_[side];
        std::vector<WidgetId> savedGroup;
        for (WidgetId id : group) {
            const auto window = windows_.find(id);
            if (window != windows_.end() && window->second.init && window->second.open &&
                static_cast<size_t>(window->second.dock) == side)
                savedGroup.push_back(id);
        }
        file << "order " << side << ' ' << savedGroup.size();
        for (WidgetId id : savedGroup) file << ' ' << id;
        file << '\n';
    }
    file << "tab-groups " << savedTabGroups.size() << '\n';
    for (const auto& [id, group] : savedTabGroups) {
        file << "tab-group " << id << ' ' << group.active << ' ' << group.windows.size();
        for (WidgetId member : group.windows) file << ' ' << member;
        file << '\n';
    }

    file.flush();
    if (!file) {
        if (error) *error = "Could not write layout file: " + path;
        return false;
    }
    if (error) error->clear();
    return true;
}

bool Context::LoadLayout(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    std::ifstream file(path);
    if (!file) return fail("Could not open layout file for reading: " + path);

    std::string token;
    unsigned int version = 0;
    if (!(file >> token >> version) || token != "FLUIDUI_LAYOUT" ||
        (version < 1 || version > 3))
        return fail("Invalid or unsupported FluidUI layout file: " + path);

    float leftRatio = 0.f, rightRatio = 0.f, topRatio = 0.f, bottomRatio = 0.f;
    if (!(file >> token >> leftRatio >> rightRatio >> topRatio >> bottomRatio) ||
        token != "ratios" ||
        !std::isfinite(leftRatio) || !std::isfinite(rightRatio) ||
        !std::isfinite(topRatio) || !std::isfinite(bottomRatio) ||
        leftRatio < 0.12f || leftRatio > 0.75f ||
        rightRatio < 0.12f || rightRatio > 0.75f ||
        topRatio < 0.12f || topRatio > 0.75f ||
        bottomRatio < 0.12f || bottomRatio > 0.75f)
        return fail("Invalid dock ratios in layout file: " + path);

    WidgetId focusedWindow = 0;
    if (!(file >> token >> focusedWindow) || token != "focus")
        return fail("Invalid focus entry in layout file: " + path);
    uint64_t centerTab = 0;
    if (version >= 2 &&
        (!(file >> token >> centerTab) || token != "center-tab" ||
         centerTab > std::numeric_limits<WidgetId>::max()))
        return fail("Invalid center tab in layout file: " + path);

    size_t windowCount = 0;
    constexpr size_t maxWindows = 10000;
    if (!(file >> token >> windowCount) || token != "windows" ||
        windowCount > maxWindows)
        return fail("Invalid window count in layout file: " + path);

    std::unordered_map<WidgetId, WindowState> loadedWindows;
    loadedWindows.reserve(windowCount);
    for (size_t i = 0; i < windowCount; ++i) {
        uint64_t rawId = 0;
        int open = 0;
        unsigned int dock = 0;
        WindowState window;
        if (!(file >> token >> rawId >> open
                    >> window.pos.x >> window.pos.y >> window.size.x >> window.size.y
                    >> dock >> window.dockSpan
                    >> window.dockRestoreSize.x >> window.dockRestoreSize.y
                    >> window.scrollY >> window.zOrder) ||
            (version >= 3 && !(file >> window.tabGroup)) ||
            token != "window" || rawId > std::numeric_limits<WidgetId>::max() ||
            (open != 0 && open != 1) || dock > 9 ||
            !std::isfinite(window.pos.x) || !std::isfinite(window.pos.y) ||
            !std::isfinite(window.size.x) || !std::isfinite(window.size.y) ||
            window.size.x < 0.f || window.size.y < 0.f ||
            !std::isfinite(window.dockSpan) || window.dockSpan <= 0.f ||
            !std::isfinite(window.dockRestoreSize.x) ||
            !std::isfinite(window.dockRestoreSize.y) ||
            window.dockRestoreSize.x < 0.f || window.dockRestoreSize.y < 0.f ||
            !std::isfinite(window.scrollY) || window.scrollY < 0.f)
            return fail("Invalid window entry in layout file: " + path);
        const WidgetId id = static_cast<WidgetId>(rawId);
        window.init = true;
        window.open = open != 0;
        window.dock = static_cast<DockSide>(dock);
        window.contentHeight = window.scrollY + window.size.y;
        if (!loadedWindows.emplace(id, window).second)
            return fail("Duplicate window ID in layout file: " + path);
    }

    std::array<std::vector<WidgetId>, 10> loadedDockGroups;
    std::unordered_set<WidgetId> orderedWindows;
    for (size_t i = 1; i < loadedDockGroups.size(); ++i) {
        size_t side = 0, count = 0;
        if (!(file >> token >> side >> count) || token != "order" ||
            side != i || count > windowCount)
            return fail("Invalid dock order in layout file: " + path);
        auto& group = loadedDockGroups[i];
        group.reserve(count);
        for (size_t j = 0; j < count; ++j) {
            uint64_t rawId = 0;
            if (!(file >> rawId) || rawId > std::numeric_limits<WidgetId>::max())
                return fail("Invalid docked window ID in layout file: " + path);
            const WidgetId id = static_cast<WidgetId>(rawId);
            const auto window = loadedWindows.find(id);
            if (window == loadedWindows.end() || !window->second.open ||
                static_cast<size_t>(window->second.dock) != i ||
                !orderedWindows.insert(id).second)
                return fail("Inconsistent dock order in layout file: " + path);
            group.push_back(id);
        }
    }

    std::unordered_map<WidgetId, TabGroup> loadedTabGroups;
    if (version >= 3) {
        std::unordered_set<WidgetId> tabGroupMembers;
        size_t groupCount = 0;
        if (!(file >> token >> groupCount) || token != "tab-groups" ||
            groupCount > windowCount)
            return fail("Invalid tab group count in layout file: " + path);
        for (size_t i = 0; i < groupCount; ++i) {
            uint64_t rawGroup = 0, rawActive = 0;
            size_t memberCount = 0;
            if (!(file >> token >> rawGroup >> rawActive >> memberCount) ||
                token != "tab-group" || rawGroup > std::numeric_limits<WidgetId>::max() ||
                rawGroup == 0 ||
                rawActive > std::numeric_limits<WidgetId>::max() ||
                memberCount < 2 || memberCount > windowCount)
                return fail("Invalid tab group in layout file: " + path);
            TabGroup group;
            group.active = static_cast<WidgetId>(rawActive);
            group.windows.reserve(memberCount);
            DockSide sharedDock = DockSide::None;
            for (size_t j = 0; j < memberCount; ++j) {
                uint64_t rawMember = 0;
                if (!(file >> rawMember) || rawMember > std::numeric_limits<WidgetId>::max())
                    return fail("Invalid tab group member in layout file: " + path);
                const WidgetId member = static_cast<WidgetId>(rawMember);
                const auto window = loadedWindows.find(member);
                if (window == loadedWindows.end() || !window->second.open ||
                    window->second.tabGroup != static_cast<WidgetId>(rawGroup) ||
                    !tabGroupMembers.insert(member).second)
                    return fail("Inconsistent tab group member in layout file: " + path);
                if (j == 0) sharedDock = window->second.dock;
                else if (window->second.dock != sharedDock)
                    return fail("Tab group windows must share a dock region: " + path);
                group.windows.push_back(member);
            }
            if (std::find(group.windows.begin(), group.windows.end(), group.active) ==
                    group.windows.end() ||
                std::find(group.windows.begin(), group.windows.end(),
                          static_cast<WidgetId>(rawGroup)) == group.windows.end() ||
                !loadedTabGroups.emplace(static_cast<WidgetId>(rawGroup),
                                         std::move(group)).second)
                return fail("Duplicate or invalid tab group in layout file: " + path);
        }
        for (const auto& [id, window] : loadedWindows) {
            if (window.tabGroup != 0 && !tabGroupMembers.contains(id))
                return fail("Window references a missing tab group: " + path);
        }
    }

    for (const auto& [id, window] : loadedWindows) {
        if (window.open && window.dock != DockSide::None &&
            !orderedWindows.contains(id))
            return fail("Docked window is missing from dock order in layout file: " + path);
    }
    const auto& loadedCenterGroup =
        loadedDockGroups[static_cast<size_t>(DockSide::Center)];
    if (version >= 2 && centerTab != 0 &&
        std::find(loadedCenterGroup.begin(), loadedCenterGroup.end(),
                  static_cast<WidgetId>(centerTab)) == loadedCenterGroup.end())
        return fail("Center tab does not belong to the center dock group: " + path);
    file >> std::ws;
    if (!file.eof()) return fail("Unexpected trailing data in layout file: " + path);

    windows_ = std::move(loadedWindows);
    dockedWindows_ = std::move(loadedDockGroups);
    tabGroups_ = std::move(loadedTabGroups);
    dockLeftRatio_ = leftRatio;
    dockRightRatio_ = rightRatio;
    dockTopRatio_ = topRatio;
    dockBottomRatio_ = bottomRatio;
    nextWindowZ_ = 0;
    for (const auto& [id, window] : windows_) {
        (void)id;
        nextWindowZ_ = std::max(nextWindowZ_, window.zOrder);
    }
    const auto focused = windows_.find(focusedWindow);
    focusedWindow_ = focused != windows_.end() && focused->second.open
        ? focusedWindow : 0;
    const auto& centerGroup = dockedWindows_[static_cast<size_t>(DockSide::Center)];
    const WidgetId savedCenterTab = static_cast<WidgetId>(centerTab);
    activeCenterTab_ = version >= 2 &&
            std::find(centerGroup.begin(), centerGroup.end(), savedCenterTab) != centerGroup.end()
        ? savedCenterTab
        : std::find(centerGroup.begin(), centerGroup.end(), focusedWindow_) != centerGroup.end()
            ? focusedWindow_ : (centerGroup.empty() ? 0 : centerGroup.back());
    active_ = 0;
    activeDockSplitter_ = 0;
    dockLayoutDone_ = false;
    if (error) error->clear();
    return true;
}

bool Context::Begin(std::string_view title, bool* open) {
    const WidgetId id = GetID(title);
    if (open) {
        if (!*open) {
            SetWindowOpen(title, false);
            return false;
        }
        const auto it = windows_.find(id);
        if (it != windows_.end() && !it->second.open)
            SetWindowOpen(title, true);
    }

    const float width = std::min(in_.displaySize.x * 0.72f,
                                 std::max(320.f, in_.displaySize.x - 48.f));
    const float height = std::min(in_.displaySize.y * 0.78f,
                                  std::max(240.f, in_.displaySize.y - 72.f));
    const Rect initial{std::max(0.f, (in_.displaySize.x - width) * 0.5f),
                       std::max(menuBarHeight_, (in_.displaySize.y - height) * 0.5f),
                       width, height};
    const bool visible = BeginWindow(title, initial);
    if (!visible && open) *open = false;
    return visible;
}

void Context::End() { EndWindow(); }

bool Context::BeginChild(std::string_view id, Vec2 size) {
    PushID(id);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) {
        const float startY = horizontalLayout_ ? content_.y : (sameLine_ ? lineTop_ : cursorY_);
        size.y = std::max(0.01f, content_.y + content_.h - startY + size.y);
    }
    return BeginContainerImpl(size, false, "container");
}

void Context::EndChild() { EndContainer(); }

bool Context::BeginMainMenuBar() {
    if (menuBarActive_) return false;
    const ComputedStyle style = Resolve("main-menu-bar", 0);
    menuBarHeight_ = std::max(style.fontSize * 1.2f + style.padding.Vertical(), 1.f);
    menuBarCursorX_ = style.padding.left;
    menuBarActive_ = true;
    menuBarLayer_ = draw_.BeginLayer();
    menuBarLayerActive_ = true;
    DrawBox({0.f, 0.f, in_.displaySize.x, menuBarHeight_}, style);
    menuStack_.clear();
    return true;
}

void Context::EndMainMenuBar() {
    if (!menuBarActive_) return;
    while (!menuStack_.empty()) EndMenu();
    draw_.EndLayer(menuBarLayer_);
    menuBarLayerActive_ = true;
    menuBarActive_ = false;
}

Rect Context::BeginMenuPopup(WidgetId id, Rect anchor, size_t depth) {
    const ComputedStyle style = Resolve("menu-popup", 0);
    const ComputedStyle itemStyle = Resolve("menu-item", 0);
    const float rowHeight = itemStyle.fontSize * 1.4f + itemStyle.padding.Vertical();
    const auto size = menuPopupSizes_.find(id);
    const float width = std::clamp(size == menuPopupSizes_.end() ? 160.f : size->second.x,
                                   1.f, std::max(1.f, in_.displaySize.x));
    const float height = std::clamp(size == menuPopupSizes_.end() ? rowHeight : size->second.y,
                                    rowHeight, std::max(rowHeight, in_.displaySize.y));
    float x = anchor.x;
    float y = anchor.y + anchor.h;
    if (depth > 0) {
        if (!menuStack_.empty()) {
            const Rect parentPopup = menuStack_.back().popup;
            draw_.PopClipRect();
            if (!interactionClips_.empty()) interactionClips_.pop_back();
            x = parentPopup.x + parentPopup.w;
            if (x + width > in_.displaySize.x) x = parentPopup.x - width;
        }
        y = anchor.y;
    }
    x = std::clamp(x, 0.f, std::max(0.f, in_.displaySize.x - width));
    Rect popup{x, y, width, height};
    if (y + popup.h > in_.displaySize.y) popup.y = std::max(0.f, in_.displaySize.y - popup.h);
    DrawBox(popup, style);
    draw_.PushClipRect(popup);
    interactionClips_.push_back(popup);
    menuPopupRects_.push_back(popup);
    return popup;
}

bool Context::BeginMenu(std::string_view label) {
    if (!menuBarActive_) return false;

    const size_t depth = menuStack_.size();
    const WidgetId parentId = depth == 0 ? GetID("##main-menu") : menuStack_.back().id;
    const WidgetId id = HashStr(label, parentId);
    const ComputedStyle style = Resolve(depth == 0 ? "menu" : "menu-item", 0);
    const float rowHeight = style.fontSize * 1.4f + style.padding.Vertical();
    Rect row;
    if (depth == 0) {
        const float width = std::max(40.f, text_->Measure(label, style.fontSize).x +
                                     style.padding.Horizontal());
        row = {menuBarCursorX_, 0.f, width, menuBarHeight_};
        menuBarCursorX_ += width;
    } else {
        MenuState& parent = menuStack_.back();
        const ComputedStyle popupStyle = Resolve("menu-popup", 0);
        row = {parent.popup.x + popupStyle.padding.left, parent.cursorY,
               std::max(0.f, parent.popup.w - popupStyle.padding.Horizontal()), rowHeight};
        parent.cursorY += rowHeight;
        const float arrowWidth = Resolve("menu-item", 0).fontSize * 1.2f;
        parent.contentWidth = std::max(parent.contentWidth,
            text_->Measure(label, style.fontSize).x + style.padding.Horizontal() +
            arrowWidth + Resolve("menu-popup", 0).padding.Horizontal());
    }

    const bool selected = depth < openMenus_.size() && openMenus_[depth] == id;
    const Interaction interaction =
        DrawMenuButton(row, label, id, depth == 0 ? "menu" : "menu-item", selected);
    if (depth > 0) {
        const ComputedStyle arrowStyle = Resolve("menu-item", 0);
        const Vec2 arrowSize = text_->Measure(">", arrowStyle.fontSize);
        EmitText({row.x + row.w - arrowSize.x - arrowStyle.padding.right,
                  row.y + (row.h - arrowSize.y) * 0.5f}, ">", arrowStyle);
    }

    if (interaction.pressed) {
        if (selected) {
            openMenus_.resize(depth);
        } else {
            openMenus_.resize(depth);
            openMenus_.push_back(id);
        }
    } else if (interaction.hovered && !in_.mouseDown[0] && !openMenus_.empty() &&
               depth < openMenus_.size()) {
        openMenus_.resize(depth);
        openMenus_.push_back(id);
    }

    if (depth >= openMenus_.size() || openMenus_[depth] != id) return false;
    const Rect parentPopup = depth > 0 && !menuStack_.empty() ? menuStack_.back().popup : Rect{};
    const Rect popup = BeginMenuPopup(id, row, depth);
    const ComputedStyle popupStyle = Resolve("menu-popup", 0);
    menuStack_.push_back({id, popup, parentPopup,
                          popup.y + popupStyle.padding.top,
                          popupStyle.padding.Horizontal(), depth > 0});
    return true;
}

void Context::EndMenu() {
    if (menuStack_.empty()) return;
    const MenuState state = menuStack_.back();
    const ComputedStyle popupStyle = Resolve("menu-popup", 0);
    const float contentHeight = std::max(
        Resolve("menu-item", 0).fontSize * 1.4f + Resolve("menu-item", 0).padding.Vertical(),
        state.cursorY - state.popup.y + popupStyle.padding.bottom);
    const float contentWidth = std::max(state.contentWidth, 1.f);
    menuPopupSizes_[state.id] = {
        std::min(contentWidth, in_.displaySize.x),
        std::min(contentHeight, in_.displaySize.y)
    };
    menuStack_.pop_back();
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    if (state.hasParentPopup) {
        draw_.PushClipRect(state.parentPopup);
        interactionClips_.push_back(state.parentPopup);
    }
}

bool Context::MenuItem(std::string_view label) {
    if (!menuBarActive_) return false;
    const size_t depth = menuStack_.size();
    const ComputedStyle style = Resolve(depth == 0 ? "menu" : "menu-item", 0);
    const float rowHeight = style.fontSize * 1.4f + style.padding.Vertical();
    Rect row;
    if (depth == 0) {
        const float width = std::max(40.f, text_->Measure(label, style.fontSize).x +
                                     style.padding.Horizontal());
        row = {menuBarCursorX_, 0.f, width, menuBarHeight_};
        menuBarCursorX_ += width;
    } else {
        MenuState& parent = menuStack_.back();
        const ComputedStyle popupStyle = Resolve("menu-popup", 0);
        row = {parent.popup.x + popupStyle.padding.left, parent.cursorY,
               std::max(0.f, parent.popup.w - popupStyle.padding.Horizontal()), rowHeight};
        parent.cursorY += rowHeight;
        parent.contentWidth = std::max(parent.contentWidth,
            text_->Measure(label, style.fontSize).x + style.padding.Horizontal() +
            Resolve("menu-popup", 0).padding.Horizontal());
    }
    const WidgetId parentId = depth == 0 ? GetID("##main-menu") : menuStack_.back().id;
    const WidgetId id = HashStr(label, parentId);
    const Interaction interaction =
        DrawMenuButton(row, label, id, depth == 0 ? "menu" : "menu-item");
    if (!interaction.pressed) return false;
    openMenus_.clear();
    return true;
}

Rect Context::DockTargetRect(DockSide side) const {
    const float gridSize = kDockTargetSize * 3.f + kDockTargetGap * 2.f;
    const float originX = in_.displaySize.x * 0.5f - gridSize * 0.5f;
    const float originY = in_.displaySize.y * 0.5f - gridSize * 0.5f;
    int column = 0, row = 0;
    switch (side) {
        case DockSide::TopLeft: row = 0; column = 0; break;
        case DockSide::Top: row = 0; column = 1; break;
        case DockSide::TopRight: row = 0; column = 2; break;
        case DockSide::Left: row = 1; column = 0; break;
        case DockSide::Center: row = 1; column = 1; break;
        case DockSide::Right: row = 1; column = 2; break;
        case DockSide::BottomLeft: row = 2; column = 0; break;
        case DockSide::Bottom: row = 2; column = 1; break;
        case DockSide::BottomRight: row = 2; column = 2; break;
        case DockSide::None: break;
    }
    return {originX + column * (kDockTargetSize + kDockTargetGap),
            originY + row * (kDockTargetSize + kDockTargetGap),
            kDockTargetSize, kDockTargetSize};
}

Context::DockSide Context::DockTargetAt(Vec2 position) const {
    constexpr DockSide sides[] = {
        DockSide::TopLeft, DockSide::Top, DockSide::TopRight,
        DockSide::Left, DockSide::Center, DockSide::Right,
        DockSide::BottomLeft, DockSide::Bottom, DockSide::BottomRight
    };
    for (DockSide side : sides)
        if (DockTargetRect(side).Contains(position)) return side;
    return DockSide::None;
}

void Context::SetDockPreview(DockSide side, WidgetId newWindow) {
    if (side == DockSide::None) {
        hasDockPreview_ = false;
        return;
    }
    const size_t index = static_cast<size_t>(side);
    const Rect region = DockRegions(side)[index];
    const auto& group = dockedWindows_[index];
    if (side == DockSide::Center) {
        dockPreview_ = region;
    } else if (newWindow != 0 && windows_.contains(newWindow)) {
        const WindowState& window = windows_.at(newWindow);
        dockPreview_ = {window.pos.x, window.pos.y, window.size.x, window.size.y};
    } else if (group.empty()) {
        dockPreview_ = region;
    } else {
        float existingSpan = 0.f;
        for (WidgetId id : group) existingSpan += std::max(0.01f, windows_.at(id).dockSpan);
        const float newSpan = group.empty()
            ? 1.f : existingSpan / static_cast<float>(group.size());
        const bool verticalSplit = side == DockSide::Left || side == DockSide::Right ||
            side == DockSide::TopLeft || side == DockSide::TopRight ||
            side == DockSide::BottomLeft || side == DockSide::BottomRight;
        const float axisSize = verticalSplit ? region.h : region.w;
        const float newSize = axisSize * newSpan / (existingSpan + newSpan);
        if (verticalSplit)
            dockPreview_ = {region.x, region.y + region.h - newSize, region.w, newSize};
        else
            dockPreview_ = {region.x + region.w - newSize, region.y, newSize, region.h};
    }
    hasDockPreview_ = true;
}

std::array<Rect, 10> Context::DockRegions(DockSide extraSide) const {
    auto hasWindow = [&](DockSide side) {
        return side == extraSide || !dockedWindows_[static_cast<size_t>(side)].empty();
    };
    const bool left = hasWindow(DockSide::Left) || hasWindow(DockSide::TopLeft) ||
                      hasWindow(DockSide::BottomLeft);
    const bool right = hasWindow(DockSide::Right) || hasWindow(DockSide::TopRight) ||
                       hasWindow(DockSide::BottomRight);
    const bool top = hasWindow(DockSide::Top) || hasWindow(DockSide::TopLeft) ||
                     hasWindow(DockSide::TopRight);
    const bool bottom = hasWindow(DockSide::Bottom) || hasWindow(DockSide::BottomLeft) ||
                        hasWindow(DockSide::BottomRight);
    const bool hasDocked = left || right || top || bottom || hasWindow(DockSide::Center);
    const bool topLeft = hasWindow(DockSide::TopLeft);
    const bool topRight = hasWindow(DockSide::TopRight);
    const bool bottomLeft = hasWindow(DockSide::BottomLeft);
    const bool bottomRight = hasWindow(DockSide::BottomRight);
    const float sideWidthScale = (left && right && dockLeftRatio_ + dockRightRatio_ > 0.8f)
        ? 0.8f / (dockLeftRatio_ + dockRightRatio_) : 1.f;
    const float sideHeightScale = (top && bottom && dockTopRatio_ + dockBottomRatio_ > 0.8f)
        ? 0.8f / (dockTopRatio_ + dockBottomRatio_) : 1.f;
    const float leftWidth = left ? in_.displaySize.x * dockLeftRatio_ * sideWidthScale : 0.f;
    const float rightWidth = right ? in_.displaySize.x * dockRightRatio_ * sideWidthScale : 0.f;
    const float centerWidth = std::max(0.f, in_.displaySize.x - leftWidth - rightWidth);
    const float dockTop = std::min(menuBarHeight_, in_.displaySize.y);
    const float usableHeight = std::max(0.f, in_.displaySize.y - dockTop);
    const float topHeight = top ? usableHeight * dockTopRatio_ * sideHeightScale : 0.f;
    const float bottomHeight = bottom ? usableHeight * dockBottomRatio_ * sideHeightScale : 0.f;
    const float centerHeight = std::max(0.f, usableHeight - topHeight - bottomHeight);
    const float centerX = leftWidth;
    const float centerY = dockTop + topHeight;
    std::array<Rect, 10> regions{};
    regions[static_cast<size_t>(DockSide::Left)] = {
        0.f,
        topLeft ? centerY : dockTop,
        leftWidth,
        centerHeight + (topLeft ? 0.f : topHeight) +
            (bottomLeft ? 0.f : bottomHeight)
    };
    regions[static_cast<size_t>(DockSide::Right)] = {
        leftWidth + centerWidth,
        topRight ? centerY : dockTop,
        rightWidth,
        centerHeight + (topRight ? 0.f : topHeight) +
            (bottomRight ? 0.f : bottomHeight)
    };
    regions[static_cast<size_t>(DockSide::Top)] =
        {centerX, dockTop, centerWidth, topHeight};
    regions[static_cast<size_t>(DockSide::Bottom)] =
        {centerX, centerY + centerHeight, centerWidth, bottomHeight};
    regions[static_cast<size_t>(DockSide::Center)] = hasDocked
        ? Rect{centerX, centerY, centerWidth, centerHeight}
        : Rect{0.f, dockTop, in_.displaySize.x, usableHeight};
    regions[static_cast<size_t>(DockSide::TopLeft)] =
        {0.f, dockTop, leftWidth, topHeight};
    regions[static_cast<size_t>(DockSide::TopRight)] =
        {leftWidth + centerWidth, dockTop, rightWidth, topHeight};
    regions[static_cast<size_t>(DockSide::BottomLeft)] =
        {0.f, centerY + centerHeight, leftWidth, bottomHeight};
    regions[static_cast<size_t>(DockSide::BottomRight)] =
        {leftWidth + centerWidth, centerY + centerHeight, rightWidth, bottomHeight};
    return regions;
}

void Context::LayoutDockedWindows() {
    if (dockLayoutDone_) return;
    dockLayoutDone_ = true;

    for (auto group = tabGroups_.begin(); group != tabGroups_.end();) {
        auto& members = group->second.windows;
        members.erase(std::remove_if(members.begin(), members.end(), [&](WidgetId id) {
            const auto window = windows_.find(id);
            return window == windows_.end() || !window->second.open ||
                   window->second.tabGroup != group->first;
        }), members.end());
        if (group->second.active == 0 ||
            std::find(members.begin(), members.end(), group->second.active) == members.end())
            group->second.active = members.empty() ? 0 : members.front();
        if (members.size() < 2) {
            for (WidgetId id : members) {
                if (auto window = windows_.find(id); window != windows_.end())
                    window->second.tabGroup = 0;
            }
            group = tabGroups_.erase(group);
        } else {
            const DockSide sharedDock = windows_.at(members.front()).dock;
            for (WidgetId id : members) {
                WindowState& window = windows_.at(id);
                if (window.dock != sharedDock) {
                    if (window.dock != DockSide::None) {
                        auto& oldGroup = dockedWindows_[static_cast<size_t>(window.dock)];
                        oldGroup.erase(std::remove(oldGroup.begin(), oldGroup.end(), id),
                                       oldGroup.end());
                    }
                    window.dock = sharedDock;
                }
            }
            ++group;
        }
    }

    for (size_t side = 1; side < dockedWindows_.size(); ++side) {
        auto& ids = dockedWindows_[side];
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](WidgetId id) {
            const auto it = windows_.find(id);
            return it == windows_.end() || !it->second.open ||
                   static_cast<size_t>(it->second.dock) != side;
        }), ids.end());
    }
    auto& centerGroup = dockedWindows_[static_cast<size_t>(DockSide::Center)];
    if (std::find(centerGroup.begin(), centerGroup.end(), activeCenterTab_) == centerGroup.end())
        activeCenterTab_ = centerGroup.empty() ? 0 : centerGroup.back();

    auto addBoundarySplitter = [&](DockSide side, Rect rect, bool horizontalAxis,
                                  float& ratio) {
        const WidgetId id = HashStr("##dock-edge-splitter", static_cast<WidgetId>(side));
        const Interaction interaction = IsFloatingWindowAt(in_.mousePos) && active_ != id
            ? Interaction{} : Behavior(id, rect.Expanded(4.f));
        if (interaction.hovered || interaction.held)
            cursorShape_ = horizontalAxis ? CursorShape::ResizeVertical
                                          : CursorShape::ResizeHorizontal;
        const float mouse = horizontalAxis ? in_.mousePos.x : in_.mousePos.y;
        if (interaction.held) {
            if (activeDockSplitter_ != id) {
                activeDockSplitter_ = id;
                dockSplitterMouseStart_ = mouse;
                dockBoundaryStart_ = ratio;
            }
            const float delta = (mouse - dockSplitterMouseStart_) /
                std::max(1.f, horizontalAxis ? in_.displaySize.x : in_.displaySize.y);
            const bool reverse = side == DockSide::Right || side == DockSide::Bottom;
            float maxRatio = 0.75f;
            const bool hasRight = !dockedWindows_[static_cast<size_t>(DockSide::Right)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::TopRight)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::BottomRight)].empty();
            const bool hasLeft = !dockedWindows_[static_cast<size_t>(DockSide::Left)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::TopLeft)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::BottomLeft)].empty();
            const bool hasTop = !dockedWindows_[static_cast<size_t>(DockSide::Top)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::TopLeft)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::TopRight)].empty();
            const bool hasBottom = !dockedWindows_[static_cast<size_t>(DockSide::Bottom)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::BottomLeft)].empty() ||
                !dockedWindows_[static_cast<size_t>(DockSide::BottomRight)].empty();
            if (side == DockSide::Left && hasRight)
                maxRatio = 0.8f - dockRightRatio_;
            if (side == DockSide::Right && hasLeft)
                maxRatio = 0.8f - dockLeftRatio_;
            if (side == DockSide::Top && hasBottom)
                maxRatio = 0.8f - dockBottomRatio_;
            if (side == DockSide::Bottom && hasTop)
                maxRatio = 0.8f - dockTopRatio_;
            ratio = std::clamp(dockBoundaryStart_ + (reverse ? -delta : delta),
                               0.12f, std::max(0.12f, maxRatio));
        } else if (activeDockSplitter_ == id && !in_.mouseDown[0]) {
            activeDockSplitter_ = 0;
        }
        dockSplitters_.push_back({rect, id});
    };

    auto hasLeft = [&]() {
        return !dockedWindows_[static_cast<size_t>(DockSide::Left)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::TopLeft)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::BottomLeft)].empty();
    };
    auto hasRight = [&]() {
        return !dockedWindows_[static_cast<size_t>(DockSide::Right)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::TopRight)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::BottomRight)].empty();
    };
    auto hasTop = [&]() {
        return !dockedWindows_[static_cast<size_t>(DockSide::Top)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::TopLeft)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::TopRight)].empty();
    };
    auto hasBottom = [&]() {
        return !dockedWindows_[static_cast<size_t>(DockSide::Bottom)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::BottomLeft)].empty() ||
            !dockedWindows_[static_cast<size_t>(DockSide::BottomRight)].empty();
    };
    std::array<Rect, 10> regions = DockRegions();
    if (hasLeft())
        addBoundarySplitter(DockSide::Left,
            {regions[static_cast<size_t>(DockSide::Left)].w - 3.f,
             std::min(menuBarHeight_, in_.displaySize.y), 6.f,
             std::max(0.f, in_.displaySize.y - menuBarHeight_)},
            true, dockLeftRatio_);
    if (hasRight())
        addBoundarySplitter(DockSide::Right,
            {regions[static_cast<size_t>(DockSide::Right)].x - 3.f,
             std::min(menuBarHeight_, in_.displaySize.y), 6.f,
             std::max(0.f, in_.displaySize.y - menuBarHeight_)},
            true, dockRightRatio_);
    if (hasTop())
        addBoundarySplitter(DockSide::Top,
            {0.f,
             regions[static_cast<size_t>(DockSide::Top)].y +
                 regions[static_cast<size_t>(DockSide::Top)].h - 3.f,
             in_.displaySize.x, 6.f},
            false, dockTopRatio_);
    if (hasBottom())
        addBoundarySplitter(DockSide::Bottom,
            {0.f,
             regions[static_cast<size_t>(DockSide::Bottom)].y - 3.f,
             in_.displaySize.x, 6.f},
            false, dockBottomRatio_);
    regions = DockRegions();
    for (size_t side = 1; side < dockedWindows_.size(); ++side) {
        const auto& dockGroup = dockedWindows_[side];
        std::vector<WidgetId> ids;
        ids.reserve(dockGroup.size());
        for (WidgetId id : dockGroup) {
            const WindowState& window = windows_[id];
            const auto group = window.tabGroup == 0
                ? tabGroups_.end() : tabGroups_.find(window.tabGroup);
            if (group == tabGroups_.end() || group->second.windows.empty() ||
                group->second.windows.front() == id)
                ids.push_back(id);
        }
        if (ids.empty()) continue;
        const Rect region = regions[side];
        if (side == static_cast<size_t>(DockSide::Center)) {
            for (WidgetId id : ids) {
                WindowState& window = windows_[id];
                window.pos = {region.x, region.y};
                window.size = {region.w, region.h};
            }
            continue;
        }

        float totalSpan = 0.f;
        for (WidgetId id : ids) totalSpan += std::max(0.01f, windows_[id].dockSpan);
        if (totalSpan <= 0.f) totalSpan = static_cast<float>(ids.size());

        float offset = 0.f;
        const bool verticalSplit = side == static_cast<size_t>(DockSide::Left) ||
            side == static_cast<size_t>(DockSide::Right) ||
            side == static_cast<size_t>(DockSide::TopLeft) ||
            side == static_cast<size_t>(DockSide::TopRight) ||
            side == static_cast<size_t>(DockSide::BottomLeft) ||
            side == static_cast<size_t>(DockSide::BottomRight);
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
            const Interaction interaction = IsFloatingWindowAt(in_.mousePos) &&
                                           active_ != splitterId
                ? Interaction{} : Behavior(splitterId, splitter.Expanded(4.f));
            if (interaction.hovered || interaction.held)
                cursorShape_ = verticalSplit ? CursorShape::ResizeHorizontal
                                             : CursorShape::ResizeVertical;
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
            dockSplitters_.push_back({splitter, splitterId});
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
    for (const auto& [groupId, group] : tabGroups_) {
        (void)groupId;
        if (group.windows.empty()) continue;
        const auto host = windows_.find(group.windows.front());
        if (host == windows_.end()) continue;
        for (size_t i = 1; i < group.windows.size(); ++i) {
            auto member = windows_.find(group.windows[i]);
            if (member == windows_.end()) continue;
            member->second.pos = host->second.pos;
            member->second.size = host->second.size;
            member->second.dockSpan = host->second.dockSpan;
        }
    }
}

bool Context::BeginWindow(std::string_view title, Rect initial) {
    LayoutDockedWindows();
    const WidgetId id = GetID(title);
    WindowState& w = windows_[id];
    w.title = std::string(DisplayLabel(title));
    if (!w.init) {
        w.init = true;
        w.pos = {initial.x, initial.y};
        w.size = {initial.w, initial.h};
        w.zOrder = ++nextWindowZ_;
    }
    if (!w.open) return false;
    const auto& centerGroup = dockedWindows_[static_cast<size_t>(DockSide::Center)];
    if (w.dock == DockSide::Center && centerGroup.size() > 1 &&
        activeCenterTab_ != id)
        return false;
    auto genericTabs = w.tabGroup == 0 ? tabGroups_.end() : tabGroups_.find(w.tabGroup);
    if (genericTabs != tabGroups_.end() && genericTabs->second.windows.size() > 1) {
        if (genericTabs->second.active != id) return false;
        const auto host = windows_.find(genericTabs->second.windows.front());
        if (host != windows_.end() && host->first != id) {
            w.pos = host->second.pos;
            w.size = host->second.size;
        }
    }

    WidgetId topWindowAtMouse = 0;
    uint64_t topWindowZ = 0;
    if (in_.mousePos.x >= 0.f && in_.mousePos.y >= 0.f &&
        in_.mousePos.x < in_.displaySize.x && in_.mousePos.y < in_.displaySize.y) {
        for (const auto& [windowId, candidate] : windows_) {
            if (!candidate.open || !candidate.init) continue;
            if (candidate.dock == DockSide::Center && centerGroup.size() > 1 &&
                windowId != activeCenterTab_)
                continue;
            if (candidate.tabGroup != 0) {
                const auto group = tabGroups_.find(candidate.tabGroup);
                if (group != tabGroups_.end() && group->second.active != windowId)
                    continue;
            }
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
    w.titleHeight = titleH;
    constexpr float controlWidth = 25.f;
    Rect frame{w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect closeRect{frame.x + frame.w - controlWidth - ts.padding.right, frame.y,
                         controlWidth, titleH};
    constexpr float resizeEdgeSize = 8.f;
    const Rect dragRect{
        frame.x + (w.dock == DockSide::None ? resizeEdgeSize + 1.f : 0.f),
        frame.y + (w.dock == DockSide::None ? resizeEdgeSize + 1.f : 0.f),
        std::max(0.f, (w.dock == DockSide::None
            ? closeRect.x - frame.x : frame.w) -
            (w.dock == DockSide::None ? resizeEdgeSize * 2.f + 2.f : 0.f)),
        std::max(0.f, titleH -
            (w.dock == DockSide::None ? resizeEdgeSize + 1.f : 0.f))};
    const WidgetId dragId = HashStr("##titlebar", id);
    const WidgetId resizeId = HashStr("##resize", id);
    const bool wasDragging = w.dragging;
    const Interaction drag = Behavior(dragId, dragRect);
    if (w.dock != DockSide::None) {
        if (drag.pressed) {
            const float dx = in_.mousePos.x - w.lastTitleClickPos.x;
            const float dy = in_.mousePos.y - w.lastTitleClickPos.y;
            const bool doubleClick = w.lastTitleClickTime >= 0.0 &&
                timeSeconds_ - static_cast<float>(w.lastTitleClickTime) <= 0.35f &&
                dx * dx + dy * dy <= 100.f;
            if (doubleClick) {
                w.dockDragPending = false;
                DetachTab(id);
                w.dock = DockSide::None;
                if (w.dockRestoreSize.x > 0.f && w.dockRestoreSize.y > 0.f)
                    w.size = w.dockRestoreSize;
                w.pos = {in_.mousePos.x - w.size.x * 0.5f,
                         in_.mousePos.y - titleH * 0.5f};
                w.pos.x = std::clamp(w.pos.x, 0.f,
                    std::max(0.f, in_.displaySize.x - w.size.x));
                w.pos.y = std::clamp(w.pos.y, menuBarHeight_,
                    std::max(menuBarHeight_, in_.displaySize.y - titleH));
                w.zOrder = ++nextWindowZ_;
                focusedWindow_ = id;
                w.lastTitleClickTime = -1.0;
                dockLayoutDone_ = false;
                frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
            } else {
                w.lastTitleClickTime = timeSeconds_;
                w.lastTitleClickPos = in_.mousePos;
            }
        }
        if (drag.held && !w.dragging) {
            if (!w.dockDragPending) {
                w.dockDragPending = true;
                w.dockDragStartPos = in_.mousePos;
            }
            const float dx = in_.mousePos.x - w.dockDragStartPos.x;
            const float dy = in_.mousePos.y - w.dockDragStartPos.y;
            if (dx * dx + dy * dy > 25.f) {
                tabDropTarget_ = dockingEnabled_ ? WindowTitleAt(in_.mousePos, id) : 0;
            }
        } else if (w.dockDragPending && !in_.mouseDown[0]) {
            const float dx = in_.mousePos.x - w.dockDragStartPos.x;
            const float dy = in_.mousePos.y - w.dockDragStartPos.y;
            const WidgetId targetWindow =
                dx * dx + dy * dy > 25.f && dockingEnabled_
                    ? WindowTitleAt(in_.mousePos, id) : 0;
            if (targetWindow != 0) {
                JoinTab(id, targetWindow);
                tabDropTarget_ = targetWindow;
            }
            w.dockDragPending = false;
        }
    } else if (drag.held) {
        if (!wasDragging) {
            w.dragOff = in_.mousePos - w.pos;
            if (w.tabGroup != 0) {
                if (!w.dockDragPending) {
                    w.dockDragPending = true;
                    w.dockDragStartPos = in_.mousePos;
                }
                const float dx = in_.mousePos.x - w.dockDragStartPos.x;
                const float dy = in_.mousePos.y - w.dockDragStartPos.y;
                if (dx * dx + dy * dy > 25.f) {
                    DetachTab(id);
                    w.dock = DockSide::None;
                    w.dockDragPending = false;
                    w.dragging = true;
                }
            } else {
                w.dragging = true;
            }
        } else if (w.dockDragPending) {
            const float dx = in_.mousePos.x - w.dockDragStartPos.x;
            const float dy = in_.mousePos.y - w.dockDragStartPos.y;
            if (dx * dx + dy * dy > 25.f) {
                DetachTab(id);
                w.dockDragPending = false;
                w.dragging = true;
            }
        }
        if (w.dragging) {
            w.pos = in_.mousePos - w.dragOff;
            tabDropTarget_ = WindowTitleAt(in_.mousePos, id);
            dockCompassRect_ = {in_.displaySize.x * 0.5f - kDockCompassSize * 0.5f,
                                in_.displaySize.y * 0.5f - kDockCompassSize * 0.5f,
                                kDockCompassSize, kDockCompassSize};
            dockCompassVisible_ = dockingEnabled_ && tabDropTarget_ == 0;
            dockCompassSelection_ = dockCompassVisible_
                ? DockTargetAt(in_.mousePos) : DockSide::None;
            SetDockPreview(dockCompassSelection_);
        }
    } else if (w.dockDragPending && !in_.mouseDown[0]) {
        w.dockDragPending = false;
    } else if (wasDragging && !in_.mouseDown[0]) {
        const WidgetId targetWindow = dockingEnabled_ ? WindowTitleAt(in_.mousePos, id) : 0;
        const DockSide target = targetWindow == 0 && dockingEnabled_
            ? DockTargetAt(in_.mousePos) : DockSide::None;
        dockCompassVisible_ = false;
        dockCompassSelection_ = target;
        if (targetWindow != 0) {
            JoinTab(id, targetWindow);
            tabDropTarget_ = targetWindow;
        } else if (target != DockSide::None) {
            w.dockRestoreSize = w.size;
            DetachTab(id);
            w.dock = target;
            auto& dockGroup = dockedWindows_[static_cast<size_t>(target)];
            if (std::find(dockGroup.begin(), dockGroup.end(), id) == dockGroup.end()) {
                float existingSpan = 0.f;
                for (WidgetId dockedId : dockGroup)
                    existingSpan += std::max(0.01f, windows_[dockedId].dockSpan);
                w.dockSpan = dockGroup.empty()
                    ? 1.f : existingSpan / static_cast<float>(dockGroup.size());
                dockGroup.push_back(id);
            }
            if (target == DockSide::Center) activeCenterTab_ = id;
            dockLayoutDone_ = false;
            LayoutDockedWindows();
        }
        w.dragging = false;
    }

    frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
    const uint8_t hitResizeEdges = w.resizing
        ? w.resizeEdges : (w.dock == DockSide::None ? ResizeEdgesAt(frame, in_.mousePos) : 0);
    if (hitResizeEdges != 0 && windowInputEnabled_)
        cursorShape_ = CursorForResize(hitResizeEdges);
    const Interaction resize =
        w.dock == DockSide::None && (hitResizeEdges != 0 || active_ == resizeId)
            ? Behavior(resizeId, frame) : Interaction{};
    if (resize.held) {
        if (!w.resizing) {
            w.resizing = true;
            w.resizeEdges = hitResizeEdges;
            w.resizeStart = in_.mousePos;
            w.startSize = w.size;
            w.resizeStartPos = w.pos;
        }
        const float deltaX = in_.mousePos.x - w.resizeStart.x;
        const float deltaY = in_.mousePos.y - w.resizeStart.y;
        constexpr float minWidth = 220.f;
        const float minHeight = titleH + 100.f;
        if (w.resizeEdges & kResizeLeft) {
            w.size.x = std::max(minWidth, w.startSize.x - deltaX);
            w.pos.x = w.resizeStartPos.x + w.startSize.x - w.size.x;
        } else if (w.resizeEdges & kResizeRight) {
            w.size.x = std::max(minWidth, w.startSize.x + deltaX);
        }
        if (w.resizeEdges & kResizeTop) {
            w.size.y = std::max(minHeight, w.startSize.y - deltaY);
            w.pos.y = w.resizeStartPos.y + w.startSize.y - w.size.y;
        } else if (w.resizeEdges & kResizeBottom) {
            w.size.y = std::max(minHeight, w.startSize.y + deltaY);
        }
        cursorShape_ = CursorForResize(w.resizeEdges);
    } else if (w.resizing && !in_.mouseDown[0]) {
        w.resizing = false;
        w.resizeEdges = 0;
    }
    if (w.dock == DockSide::None && menuBarHeight_ > 0.f)
        w.pos.y = std::max(w.pos.y, menuBarHeight_);
    if (w.tabGroup != 0) {
        const auto group = tabGroups_.find(w.tabGroup);
        if (group != tabGroups_.end()) {
            for (WidgetId memberId : group->second.windows) {
                auto member = windows_.find(memberId);
                if (member == windows_.end() || memberId == id) continue;
                member->second.pos = w.pos;
                member->second.size = w.size;
            }
        }
    }
    frame = {w.pos.x, w.pos.y, w.size.x, w.size.y};
    const Rect bar{frame.x, frame.y, frame.w, titleH};
    const Rect finalClose{frame.x + frame.w - controlWidth - ts.padding.right, frame.y,
                          controlWidth, titleH};
    const Interaction close = w.dock == DockSide::None
        ? Behavior(HashStr("##close", id), finalClose) : Interaction{};
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
    ComputedStyle windowStyle = ws;
    if (w.dock != DockSide::None) windowStyle.shadowEnabled = false;
    DrawBox(frame, windowStyle);
    DrawBox(bar, ts);
    float contentTop = titleH;
    const std::vector<WidgetId>* tabWindows = nullptr;
    WidgetId selectedTab = 0;
    bool centerTabs = false;
    if (w.dock == DockSide::Center && centerGroup.size() > 1) {
        tabWindows = &centerGroup;
        selectedTab = activeCenterTab_;
        centerTabs = true;
    } else if (genericTabs != tabGroups_.end() &&
               genericTabs->second.windows.size() > 1) {
        tabWindows = &genericTabs->second.windows;
        selectedTab = genericTabs->second.active;
    }
    if (tabWindows) {
        const ComputedStyle tabBarStyle = Resolve("dock-tab-bar", 0);
        const ComputedStyle tabBaseStyle = Resolve("dock-tab", 0);
        const float tabHeight = tabBaseStyle.fontSize * 1.2f +
                                tabBaseStyle.padding.Vertical();
        const Rect tabBar{frame.x, frame.y + titleH, frame.w, tabHeight};
        DrawBox(tabBar, tabBarStyle);
        const float tabWidth = frame.w / static_cast<float>(tabWindows->size());
        for (size_t index = 0; index < tabWindows->size(); ++index) {
            const WidgetId tabId = (*tabWindows)[index];
            const auto tabWindow = windows_.find(tabId);
            if (tabWindow == windows_.end()) continue;
            const Rect tabRect{frame.x + tabWidth * static_cast<float>(index),
                               tabBar.y, index + 1 == tabWindows->size()
                                   ? frame.x + frame.w -
                                         (frame.x + tabWidth * static_cast<float>(index))
                                   : tabWidth,
                               tabHeight};
            const bool selected = tabId == selectedTab;
            const WidgetId tabButtonId = HashStr("##dock-tab", tabId);
            const Interaction interaction = Behavior(tabButtonId, tabRect);
            const ComputedStyle tabStyle = Animate(tabButtonId, "dock-tab",
                selected ? State_Checked : State_None, interaction.hovered, interaction.held);
            DrawBox(tabRect, tabStyle);
            const std::string_view tabLabel = tabWindow->second.title;
            const Vec2 labelSize = text_->Measure(tabLabel, tabStyle.fontSize);
            EmitText({tabRect.x + std::max(0.f, (tabRect.w - labelSize.x) * 0.5f),
                      tabRect.y + std::max(0.f, (tabRect.h - labelSize.y) * 0.5f)},
                     tabLabel, tabStyle);
            if (interaction.pressed) {
                if (centerTabs) activeCenterTab_ = tabId;
                else if (auto group = tabGroups_.find(w.tabGroup);
                         group != tabGroups_.end())
                    group->second.active = tabId;
                focusedWindow_ = tabId;
                tabWindow->second.zOrder = ++nextWindowZ_;
            }
        }
        contentTop += tabHeight;
        const WidgetId activeTab = centerTabs ? activeCenterTab_ :
            (genericTabs == tabGroups_.end() ? 0 : genericTabs->second.active);
        if (activeTab != id) {
            draw_.EndLayer(w.drawLayer);
            return false;
        }
    }
    const float titleRightPadding = w.dock == DockSide::None
        ? ts.padding.right + controlWidth : ts.padding.right;
    const Rect titleRect{bar.x + ts.padding.left, bar.y,
                         std::max(0.f, bar.w - ts.padding.left - titleRightPadding), bar.h};
    DrawTextCentered(titleRect, text, ts);

    if (w.dock == DockSide::None) {
        const WidgetId closeId = HashStr("##close", id);
        ComputedStyle closeStyle = Resolve("window-close", 0);
        closeStyle = Animate(closeId, "window-close", State_None,
                             windowInputEnabled_ && finalClose.Contains(in_.mousePos),
                             active_ == closeId && in_.mouseDown[0]);
        DrawTextCentered(finalClose, "X", closeStyle);
    }

    draw_.PushClipRect(frame);
    interactionClips_.push_back(frame);
    idStack_.push_back(id);
    currentWindowId_ = id;

    const float scrollBarWidth = 12.f;
    content_ = {frame.x + ws.padding.left,
                frame.y + contentTop + ws.padding.top,
                frame.w - ws.padding.Horizontal() - scrollBarWidth,
                frame.h - contentTop - ws.padding.Vertical()};
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
            DrawStyledColor(track, trackStyle.background, trackStyle.backgroundGradient,
                            trackStyle.opacity, trackWidth * 0.5f);
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
            DrawStyledColor({thumb.x, thumbY, thumb.w, thumb.h},
                            trackStyle.accent, trackStyle.accentGradient,
                            trackStyle.opacity, trackWidth * 0.5f);
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
    EmitText({r.x + (r.w - measured.x) * 0.5f, r.y + (r.h - measured.y) * 0.5f},
             text, style);
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
    DrawOutline(r, s);
    EmitText({r.x + s.padding.left, r.y + s.padding.top}, text, s);
}

void Context::TextLabel(std::string_view text) { Text(text); }

void Context::TextLabelLeft(std::string_view text, std::string_view icon) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 iconSize = text_->Measure(icon, style.fontSize);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Rect r = Place({iconSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(iconSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    DrawOutline(r, style);
    EmitText({r.x + style.padding.left, r.y + (r.h - iconSize.y) * 0.5f}, icon, style);
    EmitText({r.x + style.padding.left + iconSize.x + style.spacing,
              r.y + (r.h - textSize.y) * 0.5f}, text, style);
}

void Context::TextLabelRight(std::string_view text, std::string_view icon) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Vec2 iconSize = text_->Measure(icon, style.fontSize);
    const Rect r = Place({textSize.x + style.spacing + iconSize.x + style.padding.Horizontal(),
                          std::max(textSize.y, iconSize.y) + style.padding.Vertical()}, style.spacing);
    DrawOutline(r, style);
    EmitText({r.x + style.padding.left, r.y + (r.h - textSize.y) * 0.5f}, text, style);
    EmitText({r.x + style.padding.left + textSize.x + style.spacing,
              r.y + (r.h - iconSize.y) * 0.5f}, icon, style);
}

void Context::TextLabelLeft(std::string_view text, GPUImageId icon, Vec2 iconSize) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Rect r = Place({iconSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(iconSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    DrawOutline(r, style);
    draw_.AddQuad({r.x + style.padding.left, r.y + (r.h - iconSize.y) * 0.5f,
                   iconSize.x, iconSize.y}, {0.f, 0.f, 1.f, 1.f},
                  Fade(Color{}, style.opacity), icon);
    EmitText({r.x + style.padding.left + iconSize.x + style.spacing,
              r.y + (r.h - textSize.y) * 0.5f}, text, style);
}

void Context::TextLabelRight(std::string_view text, GPUImageId icon, Vec2 iconSize) {
    const ComputedStyle style = Resolve("text-label", 0);
    const Vec2 textSize = text_->Measure(text, style.fontSize);
    const Rect r = Place({textSize.x + style.spacing + iconSize.x + style.padding.Horizontal(),
                          std::max(textSize.y, iconSize.y) + style.padding.Vertical()}, style.spacing);
    DrawOutline(r, style);
    EmitText({r.x + style.padding.left, r.y + (r.h - textSize.y) * 0.5f}, text, style);
    draw_.AddQuad({r.x + style.padding.left + textSize.x + style.spacing,
                   r.y + (r.h - iconSize.y) * 0.5f, iconSize.x, iconSize.y},
                  {0.f, 0.f, 1.f, 1.f}, Fade(Color{}, style.opacity), icon);
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
    EmitText({r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f}, text, s);
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
        DrawStyledColor(boxR.Expanded(-box * 0.25f), s.accent, s.accentGradient,
                        s.opacity,
                        {s.cornerRadii.topLeft * 0.5f, s.cornerRadii.topRight * 0.5f,
                         s.cornerRadii.bottomRight * 0.5f, s.cornerRadii.bottomLeft * 0.5f});
    EmitText({boxR.x + box + s.spacing, r.y + (r.h - ts.y) * 0.5f}, text, s);

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
    DrawOutline(track, style);
    DrawStyledColor(track, *value ? style.accent : style.background,
                    *value ? style.accentGradient : style.backgroundGradient,
                    style.opacity, trackH * 0.5f);
    const float knob = trackH * 0.78f;
    const float knobX = *value ? track.x + track.w - trackH * 0.89f : track.x + trackH * 0.11f;
    DrawStyledColor({knobX, track.y + (trackH - knob) * 0.5f, knob, knob},
                    style.foreground, style.textGradient, style.opacity, knob * 0.5f);
    EmitText({track.x + track.w + style.spacing, r.y + (r.h - textSize.y) * 0.5f},
             text, style);
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
    DrawOutline(circle, style);
    DrawStyledColor(circle, style.borderColor.a > 0.f ? style.borderColor : style.background,
                    style.borderColor.a > 0.f ? style.borderColorGradient
                                              : style.backgroundGradient,
                    style.opacity, diameter * 0.5f);
    if (checked) {
        const float inset = diameter * 0.28f;
        DrawStyledColor(circle.Expanded(-inset), style.accent, style.accentGradient,
                        style.opacity, diameter * 0.5f);
    }
    EmitText({circle.x + diameter + style.spacing, r.y + (r.h - textSize.y) * 0.5f},
             text, style);
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
        DrawStyledColor({r.x, r.y, r.w * t, r.h}, s.accent, s.accentGradient,
                        s.opacity, s.cornerRadii);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.*s: %.2f", static_cast<int>(text.size()), text.data(), *value);
    const Vec2 ts = text_->Measure(buf, s.fontSize);
    EmitText({r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f}, buf, s);
    return changed;
}

bool Context::ValueSlider(std::string_view label, float* value, float lo, float hi) {
    return SliderFloat(label, value, lo, hi);
}

void Context::Image(GPUImageId imageId, Vec2 size) {
    const Rect r = Place(size, Resolve("image", 0).spacing);
    draw_.AddQuad(r, {0.f, 0.f, 1.f, 1.f}, Color{}, imageId);
}

void Context::ImageWidget(std::string_view id, GPUImageId imageId, Vec2 size) {
    if (imageId == 0 || size.x <= 0.f || size.y <= 0.f) return;
    PushID(id);
    const ComputedStyle style = Resolve("image-widget", 0);
    const Rect rect = Place({size.x + style.padding.Horizontal(),
                             size.y + style.padding.Vertical()}, style.spacing);
    DrawBox(rect, style);
    ComputedStyle imageStyle;
    imageStyle.backgroundImage = imageId;
    imageStyle.backgroundImageSize = size;
    imageStyle.backgroundFit = style.backgroundFit;
    imageStyle.backgroundPosition = style.backgroundPosition;
    DrawBackgroundImage({rect.x + style.padding.left, rect.y + style.padding.top,
                         size.x, size.y}, imageStyle);
    PopID();
}

void Context::EmptyWidget(std::string_view id, Vec2 size) {
    PushID(id);
    const ComputedStyle style = Resolve("empty-widget", 0);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) size.y = style.fontSize * 6.f + style.padding.Vertical();
    const Rect rect = Place(size, style.spacing);
    DrawBox(rect, style);
    PopID();
}

void Context::ImageLabel(std::string_view label, GPUImageId imageId, Vec2 imageSize) {
    const ComputedStyle style = Resolve("image-label", 0);
    const Vec2 textSize = text_->Measure(label, style.fontSize);
    const Rect r = Place({imageSize.x + style.spacing + textSize.x + style.padding.Horizontal(),
                          std::max(imageSize.y, textSize.y) + style.padding.Vertical()}, style.spacing);
    DrawOutline(r, style);
    draw_.AddQuad({r.x + style.padding.left, r.y + (r.h - imageSize.y) * 0.5f,
                   imageSize.x, imageSize.y}, {0.f, 0.f, 1.f, 1.f}, Color{}, imageId);
    EmitText({r.x + style.padding.left + imageSize.x + style.spacing,
              r.y + (r.h - textSize.y) * 0.5f}, label, style);
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
    EmitText({r.x + style.padding.left, r.y + (r.h - fullSize.y) * 0.5f}, display, style);
    const float arrowX = r.x + r.w - style.padding.right - style.fontSize * 0.35f;
    if (style.textGradient.Empty()) {
        draw_.AddTriangleFilled({arrowX - 4.f, r.y + r.h * 0.42f},
                                {arrowX + 4.f, r.y + r.h * 0.42f},
                                {arrowX, r.y + r.h * 0.58f},
                                Fade(style.foreground, style.opacity));
    } else {
        draw_.AddTriangleFilledGradient({arrowX - 4.f, r.y + r.h * 0.42f},
                                        {arrowX + 4.f, r.y + r.h * 0.42f},
                                        {arrowX, r.y + r.h * 0.58f},
                                        style.textGradient, style.opacity);
    }
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