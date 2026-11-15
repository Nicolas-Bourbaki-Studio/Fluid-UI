#include "FluidUI/Context.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fluid {

namespace {

std::string_view DisplayLabel(std::string_view s) {
    const size_t p = s.find("##");
    return p == std::string_view::npos ? s : s.substr(0, p);
}

Color Fade(Color c, float opacity) { c.a *= opacity; return c; }

bool KeyDown(const InputState& input, size_t key) {
    return key < InputState::KeyCount && input.keysDown[key];
}

bool KeyPressed(const InputState& input, size_t key) {
    return key < InputState::KeyCount && input.keysPressed[key];
}

size_t PreviousCodepoint(std::string_view text, size_t offset) {
    if (offset == 0) return 0;
    size_t index = std::min(offset - 1, text.size() - 1);
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0u) == 0x80u)
        --index;
    return index;
}

size_t NextCodepoint(std::string_view text, size_t offset) {
    if (offset >= text.size()) return text.size();
    size_t index = offset + 1;
    while (index < text.size() &&
           (static_cast<unsigned char>(text[index]) & 0xC0u) == 0x80u)
        ++index;
    return index;
}

std::string EncodeUtf8(uint32_t codepoint) {
    std::string result;
    if (codepoint <= 0x7Fu) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        result.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu &&
               (codepoint < 0xD800u || codepoint > 0xDFFFu)) {
        result.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0x10FFFFu) {
        result.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    return result;
}

constexpr uint8_t kResizeLeft = 1u << 0;
constexpr uint8_t kResizeRight = 1u << 1;
constexpr uint8_t kResizeTop = 1u << 2;
constexpr uint8_t kResizeBottom = 1u << 3;
constexpr float kDockTargetSize = 28.f;
constexpr float kDockTargetGap = 4.f;
constexpr float kDockCompassSize = 104.f;
constexpr float kContainerScrollbarSize = 8.f;

struct ContainerViewport {
    Rect rect;
    bool horizontal = false;
    bool vertical = false;
};

ContainerViewport CalculateContainerViewport(Rect content, float contentWidth,
                                             float contentHeight) {
    bool horizontal = contentWidth > content.w;
    bool vertical = contentHeight > content.h;
    for (int i = 0; i < 3; ++i) {
        const float width = std::max(0.f, content.w -
            (vertical ? kContainerScrollbarSize : 0.f));
        const float height = std::max(0.f, content.h -
            (horizontal ? kContainerScrollbarSize : 0.f));
        const bool nextHorizontal = contentWidth > width;
        const bool nextVertical = contentHeight > height;
        if (nextHorizontal == horizontal && nextVertical == vertical) break;
        horizontal = nextHorizontal;
        vertical = nextVertical;
    }
    return {
        {content.x, content.y,
         std::max(0.f, content.w - (vertical ? kContainerScrollbarSize : 0.f)),
         std::max(0.f, content.h - (horizontal ? kContainerScrollbarSize : 0.f))},
        horizontal,
        vertical
    };
}

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
    previousScrollableContainerRects_ = std::move(scrollableContainerRects_);
    scrollableContainerRects_.clear();
    in_ = input;
    if (in_.mouseDelta.x == 0.f && in_.mouseDelta.y == 0.f)
        in_.mouseDelta = in_.mousePos - prevMousePos_;
    for (size_t button = 0; button < 3; ++button) {
        if (!in_.mousePressed[button])
            in_.mousePressed[button] = in_.mouseDown[button] &&
                                       !prevMouseDownButtons_[button];
        if (!in_.mouseReleased[button])
            in_.mouseReleased[button] = !in_.mouseDown[button] &&
                                        prevMouseDownButtons_[button];
    }
    timeSeconds_ += std::max(0.f, input.deltaTime);
    cursorShape_ = CursorShape::Arrow;
    wantsMouse_ = false;
    wantsKeyboard_ = false;
    anyItemHovered_ = false;
    windowHovered_ = false;
    lastItemClicked_ = false;
    lastItemDoubleClicked_ = false;
    lastItemActivated_ = false;
    lastItemDeactivated_ = false;
    scrollToBottom_ = false;
    dragPayloadDelivered_ = false;
    dragSourceScope_ = false;
    const bool leftPressed = in_.mousePressed[0];
    const bool rightPressed = in_.mousePressed[1];
    if (KeyPressed(in_, 256) && !openPopups_.empty())
        ClosePopupAndChildren(openPopups_.back());
    if (leftPressed && !openPopups_.empty()) {
        const WidgetId topPopup = openPopups_.back();
        const auto popup = popups_.find(topPopup);
        if (popup != popups_.end() && popup->second.open &&
            !popup->second.modal &&
            !popup->second.frame.Contains(in_.mousePos) &&
            !popup->second.anchor.Contains(in_.mousePos))
            ClosePopupAndChildren(topPopup);
    }
    if (leftPressed && focusedConsoleInput_ != 0 &&
        !focusedConsoleInputRect_.Contains(in_.mousePos))
        focusedConsoleInput_ = 0;
    if (leftPressed && focusedTextInput_ != 0 &&
        !focusedTextInputRect_.Contains(in_.mousePos)) {
        deactivatedTextInput_ = focusedTextInput_;
        focusedTextInput_ = 0;
    }
    const bool menuClickOutside = in_.mouseDown[0] && !prevMouseDown_ &&
        !openMenus_.empty() && in_.mousePos.y >= menuBarHeight_ &&
        std::none_of(menuPopupRects_.begin(), menuPopupRects_.end(),
            [&](const Rect& popup) { return popup.Contains(in_.mousePos); });
    if (menuClickOutside) openMenus_.clear();
    if (leftPressed && std::any_of(dropdowns_.begin(), dropdowns_.end(),
            [](const auto& dropdown) { return dropdown.second; }) &&
        std::none_of(dropdownPopupRects_.begin(), dropdownPopupRects_.end(),
            [&](const Rect& popup) { return popup.Contains(in_.mousePos); })) {
        for (auto& [id, open] : dropdowns_) {
            (void)id;
            open = false;
        }
    }
    previousDropdownPopupRects_.clear();
    if (std::any_of(dropdowns_.begin(), dropdowns_.end(),
            [](const auto& dropdown) { return dropdown.second; }))
        previousDropdownPopupRects_ = dropdownPopupRects_;
    if (rmbPopupOwner_ != 0 && (leftPressed || rightPressed) &&
        !rmbPopupRect_.Contains(in_.mousePos))
        rmbPopupOwner_ = 0;
    menuPopupRects_.clear();
    dropdownPopupRects_.clear();
    dropdownPopupLayers_.clear();
    lastItemRect_ = {};
    lastItemId_ = 0;
    lastItemPressed_ = false;
    lastItemHovered_ = false;
    groupStack_.clear();
    treeNodeStack_.clear();
    popupStack_.clear();
    lmbScopeActive_ = false;
    rmbPopupActiveThisFrame_ = false;
    dropdownItemInteraction_ = false;
    rmbPopupHasLayer_ = false;
    hintHasLayer_ = false;
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
    for (auto& [id, popup] : popups_) {
        (void)id;
        popup.hasLayer = false;
    }
}

void Context::BringLayerToFront(DrawList::Layer& layer) {
    std::vector<DrawList::Layer*> tracked;
    tracked.reserve(windows_.size() + dropdownPopupLayers_.size() +
                    popups_.size() + 4);
    auto addTracked = [&](DrawList::Layer& candidate) {
        if (&candidate != &layer) tracked.push_back(&candidate);
    };
    for (auto& [id, window] : windows_) {
        (void)id;
        if (window.hasDrawLayer) addTracked(window.drawLayer);
    }
    if (menuBarLayerActive_) addTracked(menuBarLayer_);
    if (rmbPopupHasLayer_) addTracked(rmbPopupLayer_);
    if (hintHasLayer_) addTracked(hintLayer_);
    for (DrawList::Layer& popup : dropdownPopupLayers_) addTracked(popup);
    for (auto& [id, popup] : popups_) {
        (void)id;
        if (popup.hasLayer) addTracked(popup.layer);
    }
    draw_.BringLayerToFront(layer, tracked);
}

const DrawList& Context::EndFrame() {
    if (const auto focused = windows_.find(focusedWindow_);
        focused != windows_.end() && focused->second.hasDrawLayer)
        BringLayerToFront(focused->second.drawLayer);
    if (menuBarLayerActive_) {
        BringLayerToFront(menuBarLayer_);
    }
    for (WidgetId popupId : openPopups_) {
        auto popup = popups_.find(popupId);
        if (popup != popups_.end() && popup->second.open && popup->second.hasLayer)
            BringLayerToFront(popup->second.layer);
    }
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
                            target->second.size.x, target->second.size.y};
            const ComputedStyle style = Resolve("dock-highlight", State_Checked);
            constexpr float border = 3.f;
            DrawStyledColor({rect.x, rect.y, rect.w, border},
                            style.foreground, style.textGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x, rect.y + rect.h - border, rect.w, border},
                            style.foreground, style.textGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x, rect.y + border, border, rect.h - border * 2.f},
                            style.foreground, style.textGradient, style.opacity, border * 0.5f);
            DrawStyledColor({rect.x + rect.w - border, rect.y + border, border,
                             rect.h - border * 2.f},
                            style.foreground, style.textGradient, style.opacity, border * 0.5f);
        }
    }
    if (dockCompassVisible_) {
        std::vector<std::string> dockClasses = classStack_;
        dockClasses.emplace_back("docking-options");
        const ComputedStyle menuStyle = style_.Resolve("dock-menu", dockClasses, 0);
        DrawBox(dockCompassRect_, menuStyle);
        constexpr DockSide targets[] = {
            DockSide::Top, DockSide::Left, DockSide::Center,
            DockSide::Right, DockSide::Bottom
        };
        constexpr DockSide gridCells[] = {
            DockSide::TopLeft, DockSide::Top, DockSide::TopRight,
            DockSide::Left, DockSide::Center, DockSide::Right,
            DockSide::BottomLeft, DockSide::Bottom, DockSide::BottomRight
        };
        for (DockSide side : targets) {
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
                    const DockSide cellSide = gridCells[row * 3 + column];
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
    for (DrawList::Layer& popup : dropdownPopupLayers_)
        BringLayerToFront(popup);
    if (rmbPopupHasLayer_) BringLayerToFront(rmbPopupLayer_);
    if (hintHasLayer_) BringLayerToFront(hintLayer_);
    if (!notificationMessage_.empty() && notificationExpiresAt_ > timeSeconds_) {
        ComputedStyle style = Resolve("notification", 0);
        const auto ease = [](float progress) {
            const float value = std::clamp(progress, 0.f, 1.f);
            return value * value * (3.f - 2.f * value);
        };
        const float fadeIn = ease((timeSeconds_ - notificationStartedAt_) / 0.2f);
        const float fadeOut = ease((notificationExpiresAt_ - timeSeconds_) / 0.25f);
        style.opacity *= std::min(fadeIn, fadeOut);
        const Vec2 textSize = text_->Measure(notificationMessage_, style.fontSize);
        const float width = std::min(textSize.x + style.padding.Horizontal(),
                                     std::max(0.f, in_.displaySize.x - 32.f));
        const float height = textSize.y + style.padding.Vertical();
        const float slideOffset = (1.f - fadeIn) * 8.f + (1.f - fadeOut) * 8.f;
        const Rect rect{(in_.displaySize.x - width) * 0.5f,
                        std::max(0.f, in_.displaySize.y - height - 24.f + slideOffset),
                        width, height};
        DrawBox(rect, style);
        EmitText({rect.x + (rect.w - textSize.x) * 0.5f,
                  rect.y + (rect.h - textSize.y) * 0.5f},
                 notificationMessage_, style);
    }
    if (hintHasLayer_) BringLayerToFront(hintLayer_);
    draw_.PopClipRect();
    prevMouseDown_ = in_.mouseDown[0];
    for (size_t button = 0; button < 3; ++button)
        prevMouseDownButtons_[button] = in_.mouseDown[button];
    prevMousePos_ = in_.mousePos;
    if (!in_.mouseDown[0]) {
        active_ = 0;
        activeDockSplitter_ = 0;
        if (draggingPayload_) {
            draggingPayload_ = false;
            dragSourceId_ = 0;
            dragPayloadType_.clear();
            dragPayloadData_.clear();
        }
    }
    prevRightMouseDown_ = in_.mouseDown[1];
    wantsMouse_ = wantsMouse_ || windowHovered_ || active_ != 0 ||
                  !openPopups_.empty() || !dropdownPopupRects_.empty();
    return draw_;
}

// ---------------------------------------------------------------- helpers

WidgetId Context::GetID(std::string_view label) const {
    return HashStr(label, idStack_.empty() ? kHashSeed : idStack_.back());
}

ComputedStyle Context::Resolve(std::string_view widget, uint8_t state) const {
    if (IsDisabled()) state |= State_Disabled;
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
    lastItemId_ = id;
    lastItemRect_ = r;
    const bool down = in_.mouseDown[0];
    const bool justPressed = in_.mousePressed[0];
    const bool wasActive = active_ == id;

    const bool insideClip = std::all_of(interactionClips_.begin(), interactionClips_.end(),
        [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
    const bool windowAllowsInput = !IsDisabled() && (windowInputEnabled_ || active_ == id ||
                                                     dropdownItemInteraction_);
    const bool blockedByContextMenu = rmbPopupOwner_ != 0 &&
        rmbPopupRect_.Contains(in_.mousePos) && !rmbItemInteraction_;
    const bool blockedByDropdownPopup = !dropdownItemInteraction_ &&
        (std::any_of(previousDropdownPopupRects_.begin(), previousDropdownPopupRects_.end(),
             [&](const Rect& popup) { return popup.Contains(in_.mousePos); }) ||
         std::any_of(dropdownPopupRects_.begin(), dropdownPopupRects_.end(),
             [&](const Rect& popup) { return popup.Contains(in_.mousePos); }));
    const bool blockedByPopup = std::any_of(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId popupId) {
            const auto popup = popups_.find(popupId);
            return popup != popups_.end() && popup->second.open &&
                   (popup->second.modal || popup->second.frame.Contains(in_.mousePos));
        }) && popupStack_.empty();
    res.hovered = windowAllowsInput && insideClip && !blockedByContextMenu &&
                  !blockedByDropdownPopup && !blockedByPopup && r.Contains(in_.mousePos) &&
                  (active_ == 0 || active_ == id);
    if (res.hovered) {
        anyItemHovered_ = true;
        wantsMouse_ = true;
    }
    if (res.hovered && justPressed && active_ == 0) {
        active_ = id;
        dragStartPosition_ = in_.mousePos;
    }
    if (res.hovered && justPressed && focusedTextInput_ != 0 &&
        focusedTextInput_ != id) {
        deactivatedTextInput_ = focusedTextInput_;
        focusedTextInput_ = 0;
    }
    res.clicked = res.hovered && justPressed && active_ == id;

    if (active_ == id) {
        if (down) {
            res.held = true;
        } else {
            res.pressed = insideClip && !blockedByContextMenu &&
                          !blockedByDropdownPopup && !blockedByPopup &&
                          r.Contains(in_.mousePos);
            active_ = 0;
            if (res.pressed) {
                const auto previous = lastItemClickTime_.find(id);
                res.doubleClicked = previous != lastItemClickTime_.end() &&
                                    timeSeconds_ - previous->second <= 0.35;
                lastItemClickTime_[id] = timeSeconds_;
            }
        }
    }
    lastItemDeactivated_ = wasActive && !down;
    SetLastItem(id, r, res);
    return res;
}

void Context::SetLastItem(WidgetId id, Rect rect, const Interaction& interaction) {
    lastItemId_ = id;
    lastItemRect_ = rect;
    const bool insideClip = std::all_of(interactionClips_.begin(), interactionClips_.end(),
        [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
    const bool blockedByContextMenu = rmbPopupOwner_ != 0 &&
        rmbPopupRect_.Contains(in_.mousePos) && !rmbItemInteraction_;
    const bool blockedByDropdownPopup = !dropdownItemInteraction_ &&
        (std::any_of(previousDropdownPopupRects_.begin(), previousDropdownPopupRects_.end(),
             [&](const Rect& popup) { return popup.Contains(in_.mousePos); }) ||
         std::any_of(dropdownPopupRects_.begin(), dropdownPopupRects_.end(),
             [&](const Rect& popup) { return popup.Contains(in_.mousePos); }));
    const bool blockedByPopup = std::any_of(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId popupId) {
            const auto popup = popups_.find(popupId);
            return popup != popups_.end() && popup->second.open &&
                   (popup->second.modal || popup->second.frame.Contains(in_.mousePos));
        }) && popupStack_.empty();
    const bool geometricHover = !IsDisabled() && windowInputEnabled_ && insideClip &&
        !blockedByContextMenu && !blockedByDropdownPopup && !blockedByPopup &&
        rect.Contains(in_.mousePos) && (active_ == 0 || active_ == id);
    lastItemHovered_ = interaction.hovered || geometricHover;
    lastItemPressed_ = interaction.pressed;
    lastItemClicked_ = interaction.clicked || interaction.pressed;
    lastItemDoubleClicked_ = interaction.doubleClicked;
    lastItemActivated_ = interaction.clicked;
    if (lastItemHovered_) {
        anyItemHovered_ = true;
        wantsMouse_ = true;
    }
}

bool Context::IsDisabled() const {
    return std::find(disabledStack_.begin(), disabledStack_.end(), true) !=
           disabledStack_.end();
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

WidgetId Context::WindowAt(Vec2 position, WidgetId exclude) const {
    WidgetId hit = 0;
    uint64_t topZ = 0;
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
        }
    }
    return hit != 0 && windows_.at(hit).canBeChildDock ? hit : 0;
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
            } else {
                NormalizeTabGroupIdentity(groupId);
            }
        }
        window->second.tabGroup = 0;
    }
    dockLayoutDone_ = false;
}

void Context::NormalizeTabGroupIdentity(WidgetId groupId) {
    auto group = tabGroups_.find(groupId);
    if (group == tabGroups_.end() || group->second.windows.size() < 2 ||
        std::find(group->second.windows.begin(), group->second.windows.end(), groupId) !=
            group->second.windows.end())
        return;

    WidgetId replacementId = group->second.windows.front();
    if (tabGroups_.contains(replacementId)) {
        replacementId = HashStr("##tab-group", groupId);
        while (replacementId == 0 || tabGroups_.contains(replacementId) ||
               windows_.contains(replacementId)) {
            ++replacementId;
            if (replacementId == 0) ++replacementId;
        }
    }

    TabGroup remapped = std::move(group->second);
    tabGroups_.erase(group);
    for (WidgetId member : remapped.windows) {
        if (auto window = windows_.find(member); window != windows_.end())
            window->second.tabGroup = replacementId;
    }
    tabGroups_.emplace(replacementId, std::move(remapped));
}

void Context::JoinTab(WidgetId id, WidgetId target) {
    if (id == target) return;
    auto dragged = windows_.find(id);
    auto targetWindow = windows_.find(target);
    if (dragged == windows_.end() || targetWindow == windows_.end() ||
        !dragged->second.open || !targetWindow->second.open ||
        !dragged->second.canDock || !targetWindow->second.canBeChildDock)
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
        Notification("Double click window title to undock");
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
    Notification("Double click window title to undock");
}

Rect Context::Place(Vec2 size, float spacing) {
    if (nextItemWidth_) {
        const float width = *nextItemWidth_;
        const float right = content_.x + content_.w;
        size.x = width >= 0.f
            ? width
            : std::max(0.f, right -
                (sameLine_ ? lastRight_ + spacing : content_.x) + width);
        nextItemWidth_.reset();
    }
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
    if (!groupStack_.empty()) {
        GroupState& group = groupStack_.back();
        if (!group.hasBounds) {
            group.bounds = {pos.x, pos.y, size.x, size.y};
            group.hasBounds = true;
        } else {
            const float left = std::min(group.bounds.x, pos.x);
            const float top = std::min(group.bounds.y, pos.y);
            const float right = std::max(group.bounds.x + group.bounds.w, pos.x + size.x);
            const float bottom = std::max(group.bounds.y + group.bounds.h, pos.y + size.y);
            group.bounds = {left, top, right - left, bottom - top};
        }
    }
    if (!containerStack_.empty()) {
        ContainerState& container = containerStack_.back();
        container.contentRight = std::max(container.contentRight,
                                           pos.x + size.x + container.scrollX);
        container.contentBottom = std::max(container.contentBottom,
                                            pos.y + size.y + container.scrollY);
    }
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
void Context::SetNextItemWidth(float width) { nextItemWidth_ = width; }

Vec2 Context::GetContentRegionAvail() const {
    const float x = sameLine_ ? std::max(0.f, content_.x + content_.w - lastRight_)
                              : content_.w;
    const float y = std::max(0.f, content_.y + content_.h - cursorY_);
    return {x, y};
}

void Context::PushWindowPadding(Vec2 padding) {
    windowPaddingStack_.emplace_back(padding.y, padding.x);
}

void Context::PopWindowPadding() {
    if (!windowPaddingStack_.empty()) windowPaddingStack_.pop_back();
}

void Context::BeginDisabled(bool disabled) { disabledStack_.push_back(disabled); }

void Context::EndDisabled() {
    if (!disabledStack_.empty()) disabledStack_.pop_back();
}

void Context::BeginGroup() {
    LayoutState outer{content_, cursorY_, lineTop_, lineBottom_, lastRight_, cursorX_,
                      sameLine_, horizontalLayout_};
    groupStack_.push_back({outer, {}, false});
    if (sameLine_) {
        cursorY_ = lineTop_;
        lineBottom_ = lineTop_;
    }
}

void Context::EndGroup() {
    if (groupStack_.empty()) return;
    const GroupState group = groupStack_.back();
    groupStack_.pop_back();
    const LayoutState inner{content_, cursorY_, lineTop_, lineBottom_, lastRight_, cursorX_,
                            sameLine_, horizontalLayout_};
    content_ = group.outer.content;
    if (!group.hasBounds) {
        cursorY_ = group.outer.cursorY;
        lineTop_ = group.outer.lineTop;
        lineBottom_ = group.outer.lineBottom;
        lastRight_ = group.outer.lastRight;
        cursorX_ = group.outer.cursorX;
        sameLine_ = group.outer.sameLine;
        horizontalLayout_ = group.outer.horizontal;
        return;
    }
    cursorY_ = std::max(group.outer.cursorY, inner.cursorY);
    lineTop_ = group.outer.lineTop;
    lineBottom_ = std::max(group.outer.lineBottom, group.bounds.y + group.bounds.h);
    lastRight_ = std::max(group.outer.lastRight, group.bounds.x + group.bounds.w);
    cursorX_ = std::max(group.outer.cursorX, inner.cursorX);
    sameLine_ = false;
    horizontalLayout_ = group.outer.horizontal;
}

bool Context::BeginPropertyGrid(std::string_view id) {
    PushID(id);
    propertyGridWidths_.push_back(content_.w);
    return true;
}

void Context::EndPropertyGrid() {
    if (propertyGridWidths_.empty()) return;
    propertyGridWidths_.pop_back();
    PopID();
}

void Context::PropertyRow(std::string_view label) {
    if (propertyGridWidths_.empty()) {
        Text(label);
        SameLine();
        return;
    }
    const float width = propertyGridWidths_.back();
    const float labelWidth = std::max(0.f, width * 0.38f);
    SetNextItemWidth(labelWidth);
    Text(label);
    SameLine();
    SetNextItemWidth(std::max(0.f, width - labelWidth - Resolve("text", 0).spacing));
}

void Context::ScrollToBottom() { scrollToBottom_ = true; }

void Context::ClippedList(int count, const std::function<void(int)>& drawItem,
                          float itemHeight) {
    if (count <= 0 || !drawItem) return;
    if (itemHeight <= 0.f) {
        const ComputedStyle style = Resolve("text", 0);
        itemHeight = std::max(1.f, style.fontSize * 1.2f +
                                    style.padding.Vertical() + style.spacing);
    }
    const Rect clip = interactionClips_.empty()
        ? windowViewport_ : interactionClips_.back();
    const float firstVisible = std::max(0.f, clip.y - cursorY_);
    const int first = std::clamp(static_cast<int>(firstVisible / itemHeight), 0, count);
    const int end = std::clamp(
        static_cast<int>((clip.y + clip.h - cursorY_) / itemHeight) + 1, first, count);
    const float startY = cursorY_;
    for (int i = first; i < end; ++i) {
        cursorY_ = startY + static_cast<float>(i) * itemHeight;
        lineTop_ = cursorY_;
        lineBottom_ = cursorY_;
        lastRight_ = content_.x;
        sameLine_ = false;
        drawItem(i);
    }
    cursorY_ = startY + static_cast<float>(count) * itemHeight;
    lineBottom_ = cursorY_;
    lastRight_ = content_.x;
    sameLine_ = false;
    if (!containerStack_.empty()) {
        ContainerState& container = containerStack_.back();
        container.contentBottom = std::max(container.contentBottom,
            cursorY_ + container.scrollY);
    }
}

void Context::Separator() {
    const ComputedStyle style = Resolve("separator", 0);
    const Rect rect = Place({content_.w, std::max(1.f, style.borderWidth)}, style.spacing);
    const Color color = style.borderColor.a > 0.f
        ? Fade(style.borderColor, style.opacity)
        : Fade(style.foreground, 0.25f * style.opacity);
    draw_.AddRectFilled(rect, color);
    SetLastItem(GetID("##separator"), rect, {});
}

bool Context::IsItemHovered() const { return lastItemHovered_; }

bool Context::IsItemClicked(int mouseButton) const {
    if (mouseButton < 0 || mouseButton >= 3) return false;
    if (mouseButton == 0) return lastItemClicked_;
    return in_.mousePressed[mouseButton] && lastItemRect_.Contains(in_.mousePos) &&
        std::all_of(interactionClips_.begin(), interactionClips_.end(),
            [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
}

bool Context::IsItemDoubleClicked() const { return lastItemDoubleClicked_; }

bool Context::IsAnyItemActive() const {
    if (active_ != 0 || focusedTextInput_ != 0 || draggingPayload_) return true;
    return std::any_of(popups_.begin(), popups_.end(),
        [](const auto& entry) { return entry.second.open; });
}

bool Context::IsWindowHovered() const { return windowHovered_; }

bool Context::IsWindowFocused() const {
    return currentWindowId_ != 0 && focusedWindow_ == currentWindowId_;
}

bool Context::WantsMouseAt(Vec2 position) const {
    if (active_ != 0 || !openPopups_.empty() ||
        (menuBarHeight_ > 0.f && position.y < menuBarHeight_))
        return true;
    if (rmbPopupOwner_ != 0 && rmbPopupRect_.Contains(position)) return true;
    if (std::any_of(menuPopupRects_.begin(), menuPopupRects_.end(),
            [&](const Rect& rect) { return rect.Contains(position); }) ||
        std::any_of(dropdownPopupRects_.begin(), dropdownPopupRects_.end(),
            [&](const Rect& rect) { return rect.Contains(position); }))
        return true;
    for (const auto& [id, window] : windows_) {
        (void)id;
        if (window.open && window.init &&
            Rect{window.pos.x, window.pos.y, window.size.x, window.size.y}.Contains(position))
            return true;
    }
    return false;
}

void Context::ItemTooltip(std::string_view text) {
    if (lastItemHovered_) Hint(text);
}

void Context::DrawLine(Vec2 a, Vec2 b, Color color, float thickness) {
    const Vec2 delta = b - a;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.f || thickness <= 0.f) return;
    const Vec2 normal{-delta.y * (thickness * 0.5f / length),
                       delta.x * (thickness * 0.5f / length)};
    draw_.AddTriangleFilled(a + normal, b + normal, b - normal, color);
    draw_.AddTriangleFilled(a + normal, b - normal, a - normal, color);
}

void Context::DrawPolyline(const std::vector<Vec2>& points, Color color, float thickness) {
    for (size_t i = 1; i < points.size(); ++i)
        DrawLine(points[i - 1], points[i], color, thickness);
}

void Context::DrawTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color) {
    draw_.AddTriangleFilled(a, b, c, color);
}

void Context::DrawRectFilled(Vec2 min, Vec2 max, Color color) {
    draw_.AddRectFilled({min.x, min.y, max.x - min.x, max.y - min.y}, color);
}

void Context::DrawLabel(Vec2 position, Color color, std::string_view text) {
    ComputedStyle style = Resolve("text", 0);
    style.foreground = color;
    style.textGradient = {};
    EmitText(position, text, style);
}

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
    std::unordered_map<WidgetId, WidgetId> savedTabGroupIds;
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
        const WidgetId savedGroupId =
            std::find(savedGroup.windows.begin(), savedGroup.windows.end(), groupId) !=
                savedGroup.windows.end()
            ? groupId : savedGroup.windows.front();
        savedGroup.active = std::find(savedGroup.windows.begin(), savedGroup.windows.end(),
                                      group.active) != savedGroup.windows.end()
            ? group.active : savedGroup.windows.front();
        for (WidgetId member : savedGroup.windows)
            savedTabGroupIds.emplace(member, savedGroupId);
        savedTabGroups.emplace_back(savedGroupId, std::move(savedGroup));
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
        const auto savedGroup = savedTabGroupIds.find(id);
        file << "window " << id << ' ' << (window.open ? 1 : 0) << ' '
             << window.pos.x << ' ' << window.pos.y << ' '
             << window.size.x << ' ' << window.size.y << ' '
             << static_cast<unsigned int>(window.dock) << ' ' << window.dockSpan << ' '
             << window.dockRestoreSize.x << ' ' << window.dockRestoreSize.y << ' '
             << window.scrollY << ' ' << window.zOrder << ' '
             << (savedGroup == savedTabGroupIds.end() ? 0 : savedGroup->second) << '\n';
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
                group.windows.end())
                return fail("Duplicate or invalid tab group in layout file: " + path);
            const WidgetId rawGroupId = static_cast<WidgetId>(rawGroup);
            const WidgetId canonicalGroupId =
                std::find(group.windows.begin(), group.windows.end(), rawGroupId) !=
                    group.windows.end()
                ? rawGroupId : group.windows.front();
            for (WidgetId member : group.windows)
                loadedWindows.at(member).tabGroup = canonicalGroupId;
            if (!loadedTabGroups.emplace(canonicalGroupId, std::move(group)).second)
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
    constexpr DockSide targets[] = {
        DockSide::Top, DockSide::Left, DockSide::Center,
        DockSide::Right, DockSide::Bottom
    };
    for (DockSide side : targets)
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

    std::vector<WidgetId> orphanedTabGroups;
    for (const auto& [groupId, group] : tabGroups_) {
        if (group.windows.size() >= 2 &&
            std::find(group.windows.begin(), group.windows.end(), groupId) ==
                group.windows.end())
            orphanedTabGroups.push_back(groupId);
    }
    for (WidgetId groupId : orphanedTabGroups)
        NormalizeTabGroupIdentity(groupId);

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
        dockSplitters_.push_back(
            {rect, id, side, true, interaction.hovered, interaction.held});
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
    windowHovered_ = false;
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
    ComputedStyle ws = Resolve("window", 0);
    if (!windowPaddingStack_.empty()) ws.padding = windowPaddingStack_.back();
    const ComputedStyle ts = Resolve("window-title", 0);
    w.canDock = ws.windowCanDock;
    w.canBeChildDock = ws.windowChildDock;
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
    windowHovered_ = windowInputEnabled_;
    wantsMouse_ = wantsMouse_ || windowHovered_;
    const bool justPressed = in_.mouseDown[0] && !prevMouseDown_;
    if (justPressed && windowInputEnabled_) {
        w.zOrder = ++nextWindowZ_;
        focusedWindow_ = id;
    }

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
                const Rect selector{in_.displaySize.x * 0.5f - kDockCompassSize * 0.5f,
                                    in_.displaySize.y * 0.5f - kDockCompassSize * 0.5f,
                                    kDockCompassSize, kDockCompassSize};
                tabDropTarget_ = dockingEnabled_ && w.canDock &&
                    !selector.Contains(in_.mousePos) ? WindowAt(in_.mousePos, id) : 0;
            }
        } else if (w.dockDragPending && !in_.mouseDown[0]) {
            const float dx = in_.mousePos.x - w.dockDragStartPos.x;
            const float dy = in_.mousePos.y - w.dockDragStartPos.y;
            const Rect selector{in_.displaySize.x * 0.5f - kDockCompassSize * 0.5f,
                                in_.displaySize.y * 0.5f - kDockCompassSize * 0.5f,
                                kDockCompassSize, kDockCompassSize};
            const WidgetId targetWindow =
                dx * dx + dy * dy > 25.f && dockingEnabled_ && w.canDock &&
                !selector.Contains(in_.mousePos)
                    ? WindowAt(in_.mousePos, id) : 0;
            if (targetWindow != 0) {
                JoinTab(id, targetWindow);
                tabDropTarget_ = targetWindow;
            }
            w.dockDragPending = false;
        }
    } else if (drag.pressed && w.tabGroup != 0 && !wasDragging) {
        w.dockDragPending = false;
        const float dx = in_.mousePos.x - w.lastTitleClickPos.x;
        const float dy = in_.mousePos.y - w.lastTitleClickPos.y;
        const bool doubleClick = w.lastTitleClickTime >= 0.0 &&
            timeSeconds_ - static_cast<float>(w.lastTitleClickTime) <= 0.35f &&
            dx * dx + dy * dy <= 100.f;
        if (doubleClick) {
            DetachTab(id);
            w.lastTitleClickTime = -1.0;
            dockLayoutDone_ = false;
        } else {
            w.lastTitleClickTime = timeSeconds_;
            w.lastTitleClickPos = in_.mousePos;
        }
    } else if (drag.held) {
        if (!wasDragging) {
            if (w.tabGroup != 0) {
                if (!w.dockDragPending) {
                    w.dockDragPending = true;
                    w.dockDragStartPos = in_.mousePos;
                    w.dragOff = in_.mousePos - w.pos;
                }
                const float dx = in_.mousePos.x - w.dockDragStartPos.x;
                const float dy = in_.mousePos.y - w.dockDragStartPos.y;
                if (dx * dx + dy * dy > 25.f) {
                    w.dockDragPending = false;
                    w.dragging = true;
                }
            } else {
                w.dragOff = in_.mousePos - w.pos;
                w.dragging = true;
            }
        } else if (w.dockDragPending) {
            const float dx = in_.mousePos.x - w.dockDragStartPos.x;
            const float dy = in_.mousePos.y - w.dockDragStartPos.y;
            if (dx * dx + dy * dy > 25.f) {
                w.dockDragPending = false;
                w.dragging = true;
            }
        }
        if (w.dragging) {
            w.pos = in_.mousePos - w.dragOff;
            dockCompassRect_ = {in_.displaySize.x * 0.5f - kDockCompassSize * 0.5f,
                                in_.displaySize.y * 0.5f - kDockCompassSize * 0.5f,
                                kDockCompassSize, kDockCompassSize};
            const bool overSelector = dockCompassRect_.Contains(in_.mousePos);
            tabDropTarget_ = dockingEnabled_ && w.canDock && !overSelector
                ? WindowAt(in_.mousePos, id) : 0;
            dockCompassVisible_ = dockingEnabled_ && w.canDock && tabDropTarget_ == 0;
            dockCompassSelection_ = dockCompassVisible_
                ? DockTargetAt(in_.mousePos) : DockSide::None;
            SetDockPreview(dockCompassSelection_);
        }
    } else if (w.dockDragPending && !in_.mouseDown[0]) {
        w.dockDragPending = false;
    } else if (wasDragging && !in_.mouseDown[0]) {
        const Rect selector{in_.displaySize.x * 0.5f - kDockCompassSize * 0.5f,
                            in_.displaySize.y * 0.5f - kDockCompassSize * 0.5f,
                            kDockCompassSize, kDockCompassSize};
        const bool overSelector = selector.Contains(in_.mousePos);
        const WidgetId targetWindow = dockingEnabled_ && w.canDock && !overSelector
            ? WindowAt(in_.mousePos, id) : 0;
        const DockSide target = targetWindow == 0 && dockingEnabled_ && w.canDock &&
                                overSelector
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
            Notification("Double click window title to undock");
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
    ComputedStyle titleStyle = ts;
    if (w.dock != DockSide::None) {
        windowStyle.shadowEnabled = false;
        windowStyle.cornerRadii = {};
        titleStyle.cornerRadii = {};
    }
    DrawBox(frame, windowStyle);
    DrawBox(bar, titleStyle);
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
    const bool overScrollableConsole = std::any_of(
        consoleSelections_.begin(), consoleSelections_.end(),
        [&](const auto& entry) {
            const TextSelection& selection = entry.second;
            return selection.outputWindow == id && selection.outputScrollable &&
                   selection.outputRect.Contains(in_.mousePos);
        });
    const bool overScrollableContainer = std::any_of(
        previousScrollableContainerRects_.begin(), previousScrollableContainerRects_.end(),
        [&](const ScrollableContainerRect& container) {
            return container.windowId == id && container.rect.Contains(in_.mousePos);
        });
    if (in_.mouseWheel != 0.f && content_.Contains(in_.mousePos) &&
        !overScrollableConsole && !overScrollableContainer) {
        w.scrollY = std::max(0.f, w.scrollY - in_.mouseWheel * ts.fontSize * 3.f);
        in_.mouseWheel = 0.f;
    }
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
        if (scrollToBottom_) {
            window.scrollY = maxScroll;
            scrollToBottom_ = false;
        }
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
        if (window.dock != DockSide::None) {
            const Rect frame{window.pos.x, window.pos.y, window.size.x, window.size.y};
            for (const DockSplitter& splitter : dockSplitters_) {
                if (!splitter.canvasBoundary) continue;
                bool applies = false;
                switch (splitter.side) {
                    case DockSide::Left:
                        applies = window.dock == DockSide::Left ||
                            window.dock == DockSide::TopLeft ||
                            window.dock == DockSide::BottomLeft;
                        break;
                    case DockSide::Right:
                        applies = window.dock == DockSide::Right ||
                            window.dock == DockSide::TopRight ||
                            window.dock == DockSide::BottomRight;
                        break;
                    case DockSide::Top:
                        applies = window.dock == DockSide::Top ||
                            window.dock == DockSide::TopLeft ||
                            window.dock == DockSide::TopRight;
                        break;
                    case DockSide::Bottom:
                        applies = window.dock == DockSide::Bottom ||
                            window.dock == DockSide::BottomLeft ||
                            window.dock == DockSide::BottomRight;
                        break;
                    default:
                        break;
                }
                if (!applies) continue;

                const uint8_t state = splitter.held
                    ? State_Hovered | State_Active
                    : splitter.hovered ? State_Hovered : State_None;
                const ComputedStyle splitterStyle = Resolve("dock-splitter", state);
                const float thickness = splitter.hovered || splitter.held ? 4.f : 2.f;
                Rect line;
                switch (splitter.side) {
                    case DockSide::Left:
                        line = {frame.x + frame.w - thickness, frame.y,
                                thickness, frame.h};
                        break;
                    case DockSide::Right:
                        line = {frame.x, frame.y, thickness, frame.h};
                        break;
                    case DockSide::Top:
                        line = {frame.x, frame.y + frame.h - thickness,
                                frame.w, thickness};
                        break;
                    case DockSide::Bottom:
                        line = {frame.x, frame.y, frame.w, thickness};
                        break;
                    default:
                        continue;
                }
                DrawBox(line, splitterStyle);
            }
        }
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
    const WidgetId id = GetID("##container-scroll");
    ContainerScrollState& scroll = containerScrollStates_[id];
    const Rect baseContent{frame.x + style.padding.left, frame.y + style.padding.top,
                           std::max(0.f, frame.w - style.padding.Horizontal()),
                           std::max(0.f, frame.h - style.padding.Vertical())};
    const ContainerViewport previousViewport = CalculateContainerViewport(
        baseContent, scroll.contentWidth, scroll.contentHeight);
    scroll.scrollX = std::clamp(scroll.scrollX, 0.f,
        std::max(0.f, scroll.contentWidth - previousViewport.rect.w));
    scroll.scrollY = std::clamp(scroll.scrollY, 0.f,
        std::max(0.f, scroll.contentHeight - previousViewport.rect.h));
    ContainerState container;
    container.outer = outer;
    container.id = id;
    container.frame = frame;
    container.baseContent = baseContent;
    container.contentRight = baseContent.x;
    container.contentBottom = baseContent.y;
    container.scrollX = scroll.scrollX;
    container.scrollY = scroll.scrollY;
    containerStack_.push_back(container);
    DrawBox(frame, style);
    draw_.PushClipRect(frame);
    draw_.PushClipRect(previousViewport.rect);
    interactionClips_.push_back(frame);
    interactionClips_.push_back(previousViewport.rect);
    content_ = {previousViewport.rect.x - scroll.scrollX,
                previousViewport.rect.y - scroll.scrollY,
                previousViewport.rect.w, previousViewport.rect.h};
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
    ContainerState& container = containerStack_.back();
    ContainerScrollState& scroll = containerScrollStates_[container.id];
    scroll.contentWidth = std::max(0.f, container.contentRight - container.baseContent.x);
    scroll.contentHeight = std::max(0.f, container.contentBottom - container.baseContent.y);
    const ContainerViewport viewport = CalculateContainerViewport(
        container.baseContent, scroll.contentWidth, scroll.contentHeight);
    const float maxScrollX = std::max(0.f, scroll.contentWidth - viewport.rect.w);
    const float maxScrollY = std::max(0.f, scroll.contentHeight - viewport.rect.h);
    scroll.scrollX = std::clamp(scroll.scrollX, 0.f, maxScrollX);
    scroll.scrollY = std::clamp(scroll.scrollY, 0.f, maxScrollY);
    if (viewport.horizontal || viewport.vertical)
        scrollableContainerRects_.push_back({container.frame, currentWindowId_});

    if (viewport.vertical) {
        const float trackWidth = std::min(kContainerScrollbarSize,
                                          container.baseContent.w);
        const Rect track{viewport.rect.x + viewport.rect.w, viewport.rect.y,
                         trackWidth, viewport.rect.h};
        const ComputedStyle style = Resolve("container-scrollbar", 0);
        DrawStyledColor(track, style.background, style.backgroundGradient,
                        style.opacity, trackWidth * 0.5f);
        if (container.frame.Contains(in_.mousePos) && in_.mouseWheel != 0.f) {
            const float lineHeight = Resolve("text", 0).fontSize;
            scroll.scrollY = std::clamp(scroll.scrollY -
                in_.mouseWheel * lineHeight * 3.f, 0.f, maxScrollY);
            in_.mouseWheel = 0.f;
        }
        const float thumbHeight = std::min(track.h, std::max(24.f,
            track.h * track.h / std::max(scroll.contentHeight, 1.f)));
        const float travel = std::max(0.f, track.h - thumbHeight);
        float thumbY = track.y + travel * (maxScrollY > 0.f
            ? scroll.scrollY / maxScrollY : 0.f);
        Rect thumb{track.x, thumbY, track.w, thumbHeight};
        const WidgetId scrollId = HashStr("##container-scroll-y", container.id);
        const WidgetId lastItemId = lastItemId_;
        const Rect lastItemRect = lastItemRect_;
        const bool lastItemPressed = lastItemPressed_;
        const bool lastItemHovered = lastItemHovered_;
        const Interaction interaction = Behavior(scrollId, track);
        lastItemId_ = lastItemId;
        lastItemRect_ = lastItemRect;
        lastItemPressed_ = lastItemPressed;
        lastItemHovered_ = lastItemHovered;
        if (interaction.held) {
            if (!scroll.scrollingY) {
                scroll.scrollingY = true;
                scroll.scrollDragOffsetY = thumb.Contains(in_.mousePos)
                    ? in_.mousePos.y - thumb.y : thumb.h * 0.5f;
            }
            const float ratio = std::clamp(
                (in_.mousePos.y - scroll.scrollDragOffsetY - track.y) /
                    std::max(1.f, travel), 0.f, 1.f);
            scroll.scrollY = ratio * maxScrollY;
            thumbY = track.y + travel * ratio;
        } else if (scroll.scrollingY && !in_.mouseDown[0]) {
            scroll.scrollingY = false;
        }
        DrawStyledColor(thumb, style.accent, style.accentGradient,
                        style.opacity, trackWidth * 0.5f);
    } else {
        scroll.scrollingY = false;
    }

    if (viewport.horizontal) {
        const float trackHeight = std::min(kContainerScrollbarSize,
                                           container.baseContent.h);
        const Rect track{viewport.rect.x, viewport.rect.y + viewport.rect.h,
                         viewport.rect.w, trackHeight};
        const ComputedStyle style = Resolve("container-scrollbar", 0);
        DrawStyledColor(track, style.background, style.backgroundGradient,
                        style.opacity, trackHeight * 0.5f);
        const float thumbWidth = std::min(track.w, std::max(24.f,
            track.w * track.w / std::max(scroll.contentWidth, 1.f)));
        const float travel = std::max(0.f, track.w - thumbWidth);
        float thumbX = track.x + travel * (maxScrollX > 0.f
            ? scroll.scrollX / maxScrollX : 0.f);
        Rect thumb{thumbX, track.y, thumbWidth, track.h};
        const WidgetId scrollId = HashStr("##container-scroll-x", container.id);
        const WidgetId lastItemId = lastItemId_;
        const Rect lastItemRect = lastItemRect_;
        const bool lastItemPressed = lastItemPressed_;
        const bool lastItemHovered = lastItemHovered_;
        const Interaction interaction = Behavior(scrollId, track);
        lastItemId_ = lastItemId;
        lastItemRect_ = lastItemRect;
        lastItemPressed_ = lastItemPressed;
        lastItemHovered_ = lastItemHovered;
        if (interaction.held) {
            if (!scroll.scrollingX) {
                scroll.scrollingX = true;
                scroll.scrollDragOffsetX = thumb.Contains(in_.mousePos)
                    ? in_.mousePos.x - thumb.x : thumb.w * 0.5f;
            }
            const float ratio = std::clamp(
                (in_.mousePos.x - scroll.scrollDragOffsetX - track.x) /
                    std::max(1.f, travel), 0.f, 1.f);
            scroll.scrollX = ratio * maxScrollX;
            thumbX = track.x + travel * ratio;
        } else if (scroll.scrollingX && !in_.mouseDown[0]) {
            scroll.scrollingX = false;
        }
        DrawStyledColor(thumb, style.accent, style.accentGradient,
                        style.opacity, trackHeight * 0.5f);
    } else {
        scroll.scrollingX = false;
    }

    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    const LayoutState outer = container.outer;
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
    SetLastItem(GetID(text), r, {});
}

void Context::TextWrapped(std::string_view text) {
    const ComputedStyle style = Resolve("text", 0);
    const float availableWidth = std::max(1.f, content_.w);
    const float lineHeight = std::max(1.f, style.fontSize * 1.2f +
                                            style.padding.Vertical());
    std::string line;
    auto emitLine = [&]() {
        const Vec2 measured = text_->Measure(line, style.fontSize);
        const Rect rect = Place({std::min(availableWidth,
                                  measured.x + style.padding.Horizontal()),
                                 lineHeight}, style.spacing);
        SetLastItem(GetID(text), rect, {});
        EmitText({rect.x + style.padding.left, rect.y + style.padding.top}, line, style);
        line.clear();
    };

    size_t offset = 0;
    while (offset < text.size()) {
        if (text[offset] == '\n') {
            emitLine();
            ++offset;
            continue;
        }
        const size_t wordEnd = text.find_first_of(" \n", offset);
        const size_t end = wordEnd == std::string_view::npos ? text.size() : wordEnd;
        const std::string_view word = text.substr(offset, end - offset);
        const std::string candidate = line.empty() ? std::string(word)
                                                   : line + " " + std::string(word);
        if (!line.empty() && text_->Measure(candidate, style.fontSize).x > availableWidth) {
            emitLine();
            line.assign(word);
        } else {
            if (!line.empty()) line.push_back(' ');
            line.append(word);
        }
        offset = end;
        if (offset < text.size() && text[offset] == ' ') ++offset;
    }
    if (!line.empty() || text.empty() || text.back() == '\n') emitLine();
}

void Context::TextDisabled(std::string_view text) {
    const ComputedStyle style = Resolve("text", State_Disabled);
    const Vec2 measured = text_->Measure(text, style.fontSize);
    const Rect rect = Place({measured.x + style.padding.Horizontal(),
                             measured.y + style.padding.Vertical()}, style.spacing);
    SetLastItem(GetID(text), rect, {});
    EmitText({rect.x + style.padding.left, rect.y + style.padding.top}, text, style);
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
    SetLastItem(GetID(text), r, {});
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
    SetLastItem(GetID(text), r, {});
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
    SetLastItem(GetID(text), r, {});
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
    SetLastItem(GetID(text), r, {});
}

bool Context::Button(std::string_view label) { return Button(label, {}); }

bool Context::Button(std::string_view label, Vec2 size) {
    const WidgetId id = GetID(label);
    const std::string_view text = DisplayLabel(label);

    const ComputedStyle base = Resolve("button", 0);
    const Vec2 ts = text_->Measure(text, base.fontSize);
    if (size.x <= 0.f) size.x = ts.x + base.padding.Horizontal();
    if (size.y <= 0.f) size.y = ts.y + base.padding.Vertical();
    const Rect r = Place(size, base.spacing);

    const Interaction b = Behavior(id, r);
    const ComputedStyle s = Animate(id, "button", State_None, b.hovered, b.held);

    DrawBox(r, s);
    EmitText({r.x + (r.w - ts.x) * 0.5f, r.y + (r.h - ts.y) * 0.5f}, text, s);
    return b.pressed && !IsDisabled();
}

void Context::Hint(std::string_view message) {
    if (message.empty() || lastItemId_ == 0 || !lastItemHovered_) return;
    const ComputedStyle style = Resolve("hint", 0);
    const Vec2 textSize = text_->Measure(message, style.fontSize);
    const Rect bounds = windowViewport_.w > 0.f && windowViewport_.h > 0.f
        ? windowViewport_ : Rect{0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    const float width = std::min(textSize.x + style.padding.Horizontal(),
                                 std::max(0.f, bounds.w));
    const float height = std::min(textSize.y + style.padding.Vertical(),
                                  std::max(0.f, bounds.h));
    float x = in_.mousePos.x + 14.f;
    float y = in_.mousePos.y + 18.f;
    if (x + width > bounds.x + bounds.w) x = in_.mousePos.x - width - 14.f;
    if (y + height > bounds.y + bounds.h) y = in_.mousePos.y - height - 14.f;
    x = std::clamp(x, bounds.x, std::max(bounds.x, bounds.x + bounds.w - width));
    y = std::clamp(y, bounds.y, std::max(bounds.y, bounds.y + bounds.h - height));
    const Rect rect{x, y, width, height};
    hintLayer_ = draw_.BeginLayer();
    DrawBox(rect, style);
    EmitText({rect.x + style.padding.left,
              rect.y + (rect.h - textSize.y) * 0.5f},
             message, style);
    draw_.EndLayer(hintLayer_);
    hintHasLayer_ = true;
}

void Context::Notification(std::string_view message) {
    notificationMessage_ = message;
    notificationStartedAt_ = message.empty() ? 0.f : timeSeconds_;
    notificationExpiresAt_ = message.empty() ? 0.f : notificationStartedAt_ + 3.f;
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

bool Context::MenuItem(std::string_view label, std::string_view shortcut,
                       bool selected, bool enabled) {
    if (!enabled) BeginDisabled();
    if (selected) PushClass("selected");
    std::string display(label);
    if (!shortcut.empty()) display += "    " + std::string(shortcut);
    const bool activated = MenuItem(display);
    if (selected) PopClass();
    if (!enabled) EndDisabled();
    return activated && enabled;
}

bool Context::OpenPopup(std::string_view id) {
    const WidgetId popupId = GetID(id);
    const WidgetId parentPopupId = popupStack_.empty() ? 0 : popupStack_.back().id;
    std::unordered_set<WidgetId> keepOpen{popupId};
    for (WidgetId ancestor = parentPopupId; ancestor != 0;) {
        if (!keepOpen.insert(ancestor).second) break;
        const auto parent = popups_.find(ancestor);
        if (parent == popups_.end()) break;
        ancestor = parent->second.parentPopupId;
    }
    for (const auto& [candidateId, candidate] : popups_) {
        if (!candidate.open || keepOpen.contains(candidateId)) continue;
        WidgetId ancestor = candidate.parentPopupId;
        while (ancestor != 0) {
            if (ancestor == popupId) {
                keepOpen.insert(candidateId);
                break;
            }
            const auto parent = popups_.find(ancestor);
            if (parent == popups_.end()) break;
            ancestor = parent->second.parentPopupId;
        }
    }
    for (auto& [candidateId, candidate] : popups_) {
        if (candidate.open && !keepOpen.contains(candidateId)) {
            candidate.open = false;
            candidate.hasLayer = false;
        }
    }
    openPopups_.erase(std::remove_if(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId candidate) { return !keepOpen.contains(candidate); }), openPopups_.end());
    PopupState& popup = popups_[popupId];
    if (!popup.open) {
        popup.anchor = lastItemRect_;
        if (popup.anchor.w <= 0.f || popup.anchor.h <= 0.f)
            popup.anchor = {in_.mousePos.x, in_.mousePos.y, 1.f, 1.f};
        popup.pos = {popup.anchor.x, popup.anchor.y + popup.anchor.h};
        popup.ownerWindowId = currentWindowId_;
    }
    popup.parentPopupId = parentPopupId;
    popup.clip = {0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    popup.open = true;
    if (std::find(openPopups_.begin(), openPopups_.end(), popupId) == openPopups_.end())
        openPopups_.push_back(popupId);
    wantsMouse_ = true;
    return true;
}

bool Context::BeginPopupImpl(std::string_view id, bool modal) {
    const WidgetId popupId = GetID(id);
    const auto found = popups_.find(popupId);
    if (found == popups_.end() || !found->second.open) return false;
    PopupState& popup = found->second;
    popup.modal = modal;
    popup.clip = {0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    if (modal) {
        wantsMouse_ = true;
        wantsKeyboard_ = true;
        popup.ownerWindowId = currentWindowId_;
    }

    const float minWidth = std::min(100.f, popup.clip.w);
    const float minHeight = std::min(60.f, popup.clip.h);
    const float width = std::clamp(popup.size.x, minWidth, popup.clip.w);
    const float height = std::clamp(popup.size.y, minHeight, popup.clip.h);
    popup.pos.x = std::clamp(popup.pos.x, popup.clip.x,
        std::max(popup.clip.x, popup.clip.x + popup.clip.w - width));
    popup.pos.y = std::clamp(popup.pos.y, popup.clip.y,
        std::max(popup.clip.y, popup.clip.y + popup.clip.h - height));
    const Rect frame{popup.pos.x, popup.pos.y, width, height};
    popup.frame = frame;

    PopupStackEntry entry;
    entry.outer = {content_, cursorY_, lineTop_, lineBottom_, lastRight_, cursorX_,
                   sameLine_, horizontalLayout_};
    entry.restoreClips = interactionClips_;
    entry.windowInputEnabled = windowInputEnabled_;
    for (size_t i = 0; i < entry.restoreClips.size(); ++i) draw_.PopClipRect();
    interactionClips_.clear();
    entry.layer = draw_.BeginLayer();
    entry.id = popupId;
    popupStack_.push_back(entry);
    popup.hasLayer = true;
    const ComputedStyle style = Resolve("popup", 0);
    DrawBox(frame, style);
    draw_.PushClipRect(frame);
    interactionClips_.push_back(frame);
    content_ = {frame.x + style.padding.left, frame.y + style.padding.top,
                std::max(0.f, frame.w - style.padding.Horizontal()),
                std::max(0.f, frame.h - style.padding.Vertical())};
    cursorY_ = lineTop_ = lineBottom_ = content_.y;
    cursorX_ = lastRight_ = content_.x;
    sameLine_ = false;
    horizontalLayout_ = false;
    idStack_.push_back(popupId);
    windowInputEnabled_ = true;
    wantsMouse_ = true;
    wantsKeyboard_ = wantsKeyboard_ || modal;
    return true;
}

bool Context::BeginPopup(std::string_view id) { return BeginPopupImpl(id, false); }

bool Context::BeginPopupModal(std::string_view title) {
    const WidgetId id = GetID(title);
    const auto found = popups_.find(id);
    if (found == popups_.end() || !found->second.open) return false;
    PopupState& popup = found->second;
    popup.modal = true;
    popup.ownerWindowId = currentWindowId_;
    popup.pos = {std::max(0.f, (in_.displaySize.x - popup.size.x) * 0.5f),
                 std::max(menuBarHeight_, (in_.displaySize.y - popup.size.y) * 0.5f)};
    return BeginPopupImpl(title, true);
}

bool Context::BeginPopupContextItem(std::string_view id) {
    std::string generated;
    if (id.empty()) {
        generated = "##context-item-" + std::to_string(lastItemId_);
        id = generated;
    }
    if (in_.mousePressed[1] && lastItemRect_.Contains(in_.mousePos)) OpenPopup(id);
    return BeginPopup(id);
}

bool Context::BeginPopupContextWindow(std::string_view id) {
    std::string generated;
    if (id.empty()) {
        generated = "##context-window-" + std::to_string(currentWindowId_);
        id = generated;
    }
    if (in_.mousePressed[1] && windowInputEnabled_ &&
        windowViewport_.Contains(in_.mousePos) && !lastItemHovered_) {
        OpenPopup(id);
        PopupState& popup = popups_[GetID(id)];
        popup.anchor = {in_.mousePos.x, in_.mousePos.y, 1.f, 1.f};
        popup.pos = in_.mousePos;
    }
    return BeginPopup(id);
}

void Context::ClosePopupAndChildren(WidgetId id) {
    std::unordered_set<WidgetId> closing{id};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [candidateId, candidate] : popups_) {
            if (closing.contains(candidateId) || !closing.contains(candidate.parentPopupId))
                continue;
            closing.insert(candidateId);
            changed = true;
        }
    }
    for (WidgetId popupId : closing) {
        const auto popup = popups_.find(popupId);
        if (popup != popups_.end()) {
            popup->second.open = false;
            popup->second.hasLayer = false;
        }
    }
    openPopups_.erase(std::remove_if(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId popupId) { return closing.contains(popupId); }), openPopups_.end());
}

void Context::CloseCurrentPopup() {
    if (!popupStack_.empty()) ClosePopupAndChildren(popupStack_.back().id);
}

void Context::EndPopupImpl() {
    if (popupStack_.empty()) return;
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    if (!idStack_.empty()) idStack_.pop_back();
    PopupStackEntry entry = popupStack_.back();
    popupStack_.pop_back();
    draw_.EndLayer(entry.layer);
    if (auto popup = popups_.find(entry.id); popup != popups_.end())
        popup->second.layer = entry.layer;
    for (const Rect& clip : entry.restoreClips) {
        draw_.PushClipRect(clip);
        interactionClips_.push_back(clip);
    }
    content_ = entry.outer.content;
    cursorY_ = entry.outer.cursorY;
    lineTop_ = entry.outer.lineTop;
    lineBottom_ = entry.outer.lineBottom;
    lastRight_ = entry.outer.lastRight;
    cursorX_ = entry.outer.cursorX;
    sameLine_ = entry.outer.sameLine;
    horizontalLayout_ = entry.outer.horizontal;
    windowInputEnabled_ = entry.windowInputEnabled;
}

void Context::EndPopup() { EndPopupImpl(); }

bool Context::BeginDragSource() {
    if (dragSourceScope_ || lastItemId_ == 0 || active_ != lastItemId_ ||
        !in_.mouseDown[0] || !lastItemRect_.Contains(dragStartPosition_))
        return false;
    const Vec2 delta = in_.mousePos - dragStartPosition_;
    if (delta.x * delta.x + delta.y * delta.y < 16.f) return false;
    dragSourceId_ = lastItemId_;
    draggingPayload_ = true;
    dragSourceScope_ = true;
    wantsMouse_ = true;
    return true;
}

void Context::SetDragPayload(std::string_view type, std::string_view data) {
    if (type.empty() || type.size() > 32)
        throw std::invalid_argument("FluidUI drag payload types must contain 1 to 32 characters");
    if (!dragSourceScope_)
        throw std::logic_error("SetDragPayload must be called within a drag source");
    dragPayloadType_.assign(type);
    dragPayloadData_.assign(data);
    draggingPayload_ = true;
}

void Context::EndDragSource() { dragSourceScope_ = false; }

bool Context::BeginDropTarget() {
    const bool insideClip = std::all_of(interactionClips_.begin(), interactionClips_.end(),
        [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
    const bool hovered = windowInputEnabled_ && insideClip &&
        lastItemRect_.Contains(in_.mousePos);
    return draggingPayload_ && lastItemId_ != 0 && lastItemId_ != dragSourceId_ && hovered;
}

bool Context::AcceptDrop(std::string_view type, std::string& data) {
    const bool released = in_.mouseReleased[0];
    if (!released || dragPayloadDelivered_ || type != dragPayloadType_ ||
        !BeginDropTarget())
        return false;
    data = dragPayloadData_;
    dragPayloadDelivered_ = true;
    return true;
}

void Context::EndDropTarget() {}

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

bool Context::DragFloat(std::string_view label, float* value, float speed,
                        float minV, float maxV) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    if (auto editing = numericInputs_.find(id); editing != numericInputs_.end()) {
        if (editing->second.focus) {
            focusNextTextInput_ = true;
            focusNextTextInputSelectAll_ = true;
            editing->second.focus = false;
        }
        bool submitted = false;
        InputText(label, &editing->second.text, false, true, &submitted);
        const bool deactivated = lastItemDeactivated_;
        if (!submitted && !deactivated) return false;
        char* end = nullptr;
        const float parsed = std::strtof(editing->second.text.c_str(), &end);
        while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        const bool enabled = !IsDisabled();
        bool changed = false;
        if (enabled && end != editing->second.text.c_str() && end && *end == '\0' &&
            std::isfinite(parsed)) {
            const float next = minV < maxV ? std::clamp(parsed, minV, maxV) : parsed;
            changed = next != *value;
            *value = next;
        }
        numericInputs_.erase(editing);
        return changed;
    }

    const ComputedStyle base = Resolve("slider", 0);
    const float height = base.fontSize * 1.2f + base.padding.Vertical();
    const Vec2 available = GetContentRegionAvail();
    const Rect rect = Place({available.x, height}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    bool changed = false;
    if (interaction.held && !IsDisabled()) {
        const float delta = in_.mouseDelta.x * speed;
        if (delta != 0.f) {
            const float next = minV < maxV
                ? std::clamp(*value + delta, minV, maxV)
                : *value + delta;
            changed = next != *value;
            *value = next;
        }
    }
    const ComputedStyle style = Animate(id, "slider", State_None,
                                        interaction.hovered, interaction.held);
    DrawBox(rect, style);
    const std::string_view visible = DisplayLabel(label);
    char valueText[64];
    std::snprintf(valueText, sizeof(valueText), "%.3f", *value);
    const std::string display = visible.empty()
        ? std::string(valueText)
        : std::string(visible) + ": " + valueText;
    const Vec2 measured = text_->Measure(display, style.fontSize);
    EmitText({rect.x + style.padding.left,
              rect.y + (rect.h - measured.y) * 0.5f}, display, style);
    if (interaction.doubleClicked && !IsDisabled())
        numericInputs_.emplace(id, NumericInputState{valueText, true});
    return changed;
}

bool Context::DragInt(std::string_view label, int* value, float speed,
                      int minV, int maxV) {
    if (!value) return false;
    float edited = static_cast<float>(*value);
    const bool changed = DragFloat(label, &edited, speed,
                                   static_cast<float>(minV), static_cast<float>(maxV));
    if (!changed) return false;
    const int next = minV < maxV
        ? std::clamp(static_cast<int>(std::lround(edited)), minV, maxV)
        : static_cast<int>(std::lround(edited));
    if (next == *value) return false;
    *value = next;
    return true;
}

bool Context::ColorEdit3(std::string_view label, float* value) {
    if (!value) return false;
    bool changed = false;
    PushID(label);
    const float width = std::max(1.f,
        (content_.w - 2.f * Resolve("text", 0).spacing) / 3.f);
    const char* names[] = {"R##r", "G##g", "B##b"};
    for (int i = 0; i < 3; ++i) {
        if (i) SameLine();
        SetNextItemWidth(width);
        changed = DragFloat(names[i], value + i, 0.005f, 0.f, 1.f) || changed;
    }
    PopID();
    return changed;
}

bool Context::ColorEdit4(std::string_view label, float* value) {
    if (!value) return false;
    bool changed = false;
    PushID(label);
    const float width = std::max(1.f,
        (content_.w - 3.f * Resolve("text", 0).spacing) / 4.f);
    const char* names[] = {"R##r", "G##g", "B##b", "A##a"};
    for (int i = 0; i < 4; ++i) {
        if (i) SameLine();
        SetNextItemWidth(width);
        changed = DragFloat(names[i], value + i, 0.005f, 0.f, 1.f) || changed;
    }
    PopID();
    return changed;
}

void Context::SetKeyboardFocusHere() {
    focusNextTextInput_ = true;
    focusNextTextInputSelectAll_ = false;
}

bool Context::InputText(std::string_view label, std::string* value, bool multiline,
                        bool submitOnEnter, bool* submitted) {
    if (!value) return false;
    if (submitted) *submitted = false;
    const WidgetId id = GetID(label);
    const ComputedStyle base = Resolve("input-text", 0);
    const float height = multiline
        ? std::max(base.fontSize * 6.f, base.fontSize * 1.2f + base.padding.Vertical())
        : base.fontSize * 1.2f + base.padding.Vertical();
    const Rect rect = Place({GetContentRegionAvail().x, height}, base.spacing);
    focusedTextInputRect_ = rect;
    const Interaction interaction = Behavior(id, rect);
    TextInputState& state = textInputs_[id];
    state.cursor = std::min(state.cursor, value->size());
    state.selectionAnchor = std::min(state.selectionAnchor, value->size());
    bool changed = false;
    if (interaction.clicked || (focusNextTextInput_ && !IsDisabled())) {
        const bool selectAll = focusNextTextInput_ && focusNextTextInputSelectAll_;
        if (focusedTextInput_ != id) {
            if (focusedTextInput_ != 0) deactivatedTextInput_ = focusedTextInput_;
            focusedTextInput_ = id;
            lastItemActivated_ = true;
        }
        if (selectAll) {
            state.cursor = value->size();
            state.selectionAnchor = 0;
        } else if (interaction.clicked) {
            const float targetX = std::max(0.f, in_.mousePos.x -
                rect.x - base.padding.left - 2.f + state.scrollX);
            float closest = 0.f;
            size_t cursor = 0;
            for (size_t next = 0; next < value->size();) {
                next = NextCodepoint(*value, next);
                const float advance = text_->Measure(value->substr(0, next),
                                                     base.fontSize).x;
                if (std::abs(advance - targetX) < std::abs(closest - targetX)) {
                    closest = advance;
                    cursor = next;
                }
            }
            state.cursor = targetX > closest ? value->size() : cursor;
            state.selectionAnchor = state.cursor;
        } else {
            state.cursor = state.selectionAnchor = value->size();
        }
        state.initialized = true;
    }
    focusNextTextInput_ = false;
    const bool deactivated = deactivatedTextInput_ == id;
    if (deactivated) {
        lastItemDeactivated_ = true;
        deactivatedTextInput_ = 0;
    }
    bool didSubmit = false;
    if (focusedTextInput_ == id && !IsDisabled()) {
        wantsKeyboard_ = true;
        const bool shift = in_.keyShift || KeyDown(in_, 340) || KeyDown(in_, 344);
        const bool control = in_.keyControl || KeyDown(in_, 341) || KeyDown(in_, 345);
        auto selection = [&]() {
            return std::minmax(state.cursor, state.selectionAnchor);
        };
        auto eraseSelection = [&]() {
            const auto [first, last] = selection();
            if (first == last) return false;
            value->erase(first, last - first);
            state.cursor = state.selectionAnchor = first;
            return true;
        };
        const bool selectAll = in_.keySelectAll || (control && KeyPressed(in_, 65));
        if (selectAll) {
            state.selectionAnchor = 0;
            state.cursor = value->size();
        } else {
            const bool left = in_.keyLeft || KeyPressed(in_, 263);
            const bool right = in_.keyRight || KeyPressed(in_, 262);
            if (left) {
                const auto [first, last] = selection();
                state.cursor = !shift && first != last ? first
                    : PreviousCodepoint(*value, state.cursor);
                if (!shift) state.selectionAnchor = state.cursor;
            } else if (right) {
                const auto [first, last] = selection();
                state.cursor = !shift && first != last ? last
                    : NextCodepoint(*value, state.cursor);
                if (!shift) state.selectionAnchor = state.cursor;
            } else if (in_.keyHome || KeyPressed(in_, 268)) {
                const size_t previousLine = state.cursor == 0 ? 0
                    : value->rfind('\n', state.cursor - 1);
                state.cursor = previousLine == std::string::npos ? 0 : previousLine + 1;
                if (!shift) state.selectionAnchor = state.cursor;
            } else if (in_.keyEnd || KeyPressed(in_, 269)) {
                const size_t nextLine = value->find('\n', state.cursor);
                state.cursor = nextLine == std::string::npos ? value->size() : nextLine;
                if (!shift) state.selectionAnchor = state.cursor;
            }
            if (in_.keyCopy && control) {
                const auto [first, last] = selection();
                if (first != last && in_.setClipboardText)
                    in_.setClipboardText(value->substr(first, last - first));
            }
            if (in_.keyPaste && control) {
                changed = eraseSelection() || changed;
                value->insert(state.cursor, in_.clipboardText);
                state.cursor += in_.clipboardText.size();
                state.selectionAnchor = state.cursor;
                changed = !in_.clipboardText.empty() || changed;
            }
            if (in_.keyBackspace || KeyPressed(in_, 259)) {
                bool erased = eraseSelection();
                if (!erased && state.cursor > 0) {
                    const size_t previous = PreviousCodepoint(*value, state.cursor);
                    value->erase(previous, state.cursor - previous);
                    state.cursor = state.selectionAnchor = previous;
                    erased = true;
                }
                changed = erased || changed;
            }
            if (in_.keyDelete || KeyPressed(in_, 261)) {
                bool erased = eraseSelection();
                if (!erased && state.cursor < value->size()) {
                    value->erase(state.cursor, NextCodepoint(*value, state.cursor) -
                                              state.cursor);
                    erased = true;
                }
                state.selectionAnchor = state.cursor;
                changed = erased || changed;
            }
            const bool enter = in_.keyEnter || KeyPressed(in_, 257) || KeyPressed(in_, 335);
            if (enter) {
                if (multiline && !submitOnEnter && !control) {
                    changed = eraseSelection() || changed;
                    value->insert(state.cursor, 1, '\n');
                    state.selectionAnchor = ++state.cursor;
                    changed = true;
                } else {
                    didSubmit = true;
                    focusedTextInput_ = 0;
                    lastItemDeactivated_ = true;
                }
            }
            if (!didSubmit) {
                if (!in_.textInput.empty()) {
                    changed = eraseSelection() || changed;
                    value->insert(state.cursor, in_.textInput);
                    state.cursor += in_.textInput.size();
                    state.selectionAnchor = state.cursor;
                    changed = true;
                }
                for (uint32_t codepoint : in_.textInputCodepoints) {
                    if (codepoint < 32 || codepoint == 127) continue;
                    const std::string encoded = EncodeUtf8(codepoint);
                    if (encoded.empty()) continue;
                    changed = eraseSelection() || changed;
                    value->insert(state.cursor, encoded);
                    state.cursor += encoded.size();
                    state.selectionAnchor = state.cursor;
                    changed = true;
                }
            }
        }
    }
    if (submitted) *submitted = didSubmit;
    const bool focused = focusedTextInput_ == id;
    const ComputedStyle style = Animate(id, "input-text",
        focused ? State_Active : State_None, interaction.hovered, focused);
    DrawBox(rect, style);
    draw_.PushClipRect(rect);
    const float textX = rect.x + style.padding.left + 2.f;
    const float textY = rect.y + style.padding.top + 1.f;
    const Vec2 caretMeasure = text_->Measure(value->substr(0, state.cursor), style.fontSize);
    if (focused)
        state.scrollX = std::max(0.f, caretMeasure.x -
            std::max(0.f, rect.w - style.padding.Horizontal() - 8.f));
    const auto [selectionStart, selectionEnd] =
        std::minmax(state.cursor, state.selectionAnchor);
    const float selectionX =
        text_->Measure(value->substr(0, selectionStart), style.fontSize).x;
    const float selectionWidth = text_->Measure(
        value->substr(selectionStart, selectionEnd - selectionStart), style.fontSize).x;
    if (focused && selectionWidth > 0.f)
        draw_.AddRectFilled({textX + selectionX - state.scrollX, textY,
                             selectionWidth, style.fontSize * 1.2f}, style.accent);
    EmitText({textX - state.scrollX, textY}, *value, style);
    if (focused) {
        const float caretX = textX + caretMeasure.x - state.scrollX;
        draw_.AddRectFilled({caretX, textY, 1.f, style.fontSize * 1.2f}, style.foreground);
    }
    draw_.PopClipRect();
    return changed;
}

bool Context::Selectable(std::string_view label, bool selected) {
    const WidgetId id = GetID(label);
    const ComputedStyle base = Resolve("selectable", selected ? State_Checked : State_None);
    const std::string_view visible = DisplayLabel(label);
    const Vec2 textSize = text_->Measure(visible, base.fontSize);
    const Rect rect = Place({GetContentRegionAvail().x,
                             textSize.y + base.padding.Vertical()}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    const ComputedStyle style = Animate(id, "selectable",
        selected ? State_Checked : State_None, interaction.hovered, interaction.held);
    DrawBox(rect, style);
    EmitText({rect.x + style.padding.left,
              rect.y + (rect.h - textSize.y) * 0.5f}, visible, style);
    return interaction.pressed && !IsDisabled();
}

bool Context::TreeNode(std::string_view label, uint32_t flags) {
    const WidgetId id = GetID(label);
    const bool leaf = (flags & (1u << 1)) != 0;
    auto [openIt, inserted] = treeNodeOpen_.try_emplace(id, (flags & (1u << 2)) != 0);
    (void)inserted;
    bool& open = openIt->second;
    const ComputedStyle style = Resolve("tree-node", flags & 1u ? State_Checked : State_None);
    const std::string_view visible = DisplayLabel(label);
    const Vec2 measured = text_->Measure(visible, style.fontSize);
    const float rowHeight = std::max(measured.y, style.fontSize * 1.15f) +
                            style.padding.Vertical();
    const Rect rect = Place({GetContentRegionAvail().x, rowHeight}, style.spacing);
    const Interaction interaction = Behavior(id, rect);
    if (interaction.pressed && !leaf && !IsDisabled()) open = !open;
    const ComputedStyle animated = Animate(id, "tree-node",
        ((flags & 1u) ? State_Checked : State_None) | (open ? State_Active : State_None),
        interaction.hovered, interaction.held);
    DrawBox(rect, animated);
    const float iconSize = animated.fontSize * 0.45f;
    const float iconX = rect.x + animated.padding.left;
    const float iconY = rect.y + (rect.h - iconSize) * 0.5f;
    if (!leaf) {
        if (open)
            draw_.AddTriangleFilled({iconX, iconY}, {iconX + iconSize, iconY},
                                    {iconX + iconSize * 0.5f, iconY + iconSize},
                                    animated.foreground);
        else
            draw_.AddTriangleFilled({iconX, iconY}, {iconX + iconSize,
                                    iconY + iconSize * 0.5f},
                                    {iconX, iconY + iconSize}, animated.foreground);
    }
    EmitText({iconX + iconSize + animated.spacing,
              rect.y + (rect.h - measured.y) * 0.5f}, visible, animated);
    const bool expanded = leaf || open;
    if (expanded) {
        const float oldX = content_.x, oldWidth = content_.w;
        const bool indented = !leaf;
        treeNodeStack_.push_back({oldX, oldWidth, indented});
        if (indented) {
            const float indent = animated.fontSize * 1.1f;
            content_.x += indent;
            content_.w = std::max(0.f, content_.w - indent);
        }
    }
    return expanded;
}

void Context::TreePop() {
    if (treeNodeStack_.empty()) return;
    const TreeNodeLayout layout = treeNodeStack_.back();
    treeNodeStack_.pop_back();
    content_.x = layout.oldContentX;
    content_.w = layout.oldContentWidth;
}

bool Context::CollapsingHeader(std::string_view label, bool defaultOpen) {
    const WidgetId id = GetID(label);
    auto [it, inserted] = treeNodeOpen_.try_emplace(id, defaultOpen);
    (void)inserted;
    bool& open = it->second;
    const std::string_view visible = DisplayLabel(label);
    const ComputedStyle base = Resolve("collapsing-header",
                                       open ? State_Checked : State_None);
    const Vec2 measured = text_->Measure(visible, base.fontSize);
    const Rect rect = Place({GetContentRegionAvail().x,
                             measured.y + base.padding.Vertical()}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    if (interaction.pressed && !IsDisabled()) open = !open;
    const ComputedStyle style = Animate(id, "collapsing-header",
        open ? State_Checked : State_None, interaction.hovered, interaction.held);
    DrawBox(rect, style);
    const std::string_view marker = open ? "v" : ">";
    const Vec2 markerSize = text_->Measure(marker, style.fontSize);
    EmitText({rect.x + style.padding.left,
              rect.y + (rect.h - markerSize.y) * 0.5f}, marker, style);
    EmitText({rect.x + style.padding.left + markerSize.x + style.spacing,
              rect.y + (rect.h - measured.y) * 0.5f}, visible, style);
    return open;
}

void Context::Image(GPUImageId imageId, Vec2 size) { Image(imageId, size, false); }

void Context::Image(GPUImageId imageId, Vec2 size, bool flipY) {
    const Rect r = Place(size, Resolve("image", 0).spacing);
    draw_.AddQuad(r, {0.f, flipY ? 1.f : 0.f, 1.f, flipY ? -1.f : 1.f},
                  Color{}, imageId);
    SetLastItem(GetID("##image"), r, {});
}

void Context::InteractiveImage(GPUImageId imageId, Vec2 size, bool flipY) {
    const WidgetId id = GetID("##interactive-image");
    const ComputedStyle base = Resolve("image-button", 0);
    const Rect rect = Place({size.x + base.padding.Horizontal(),
                             size.y + base.padding.Vertical()}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    const ComputedStyle style = Animate(id, "image-button", State_None,
                                        interaction.hovered, interaction.held);
    DrawBox(rect, style);
    if (imageId != 0)
        draw_.AddQuad({rect.x + style.padding.left, rect.y + style.padding.top,
                       size.x, size.y},
                      {0.f, flipY ? 1.f : 0.f, 1.f, flipY ? -1.f : 1.f},
                      Color{1.f, 1.f, 1.f, style.opacity}, imageId);
}

bool Context::ImageTile(std::string_view id, GPUImageId imageId, Vec2 size,
                        bool selected, bool flipY, std::string_view fallbackLabel,
                        Color fallbackColor) {
    PushID(id);
    const WidgetId widgetId = GetID("##tile");
    const ComputedStyle base = Resolve("image-button",
        selected ? State_Checked : State_None);
    const Rect rect = Place(size, base.spacing);
    const Interaction interaction = Behavior(widgetId, rect);
    const ComputedStyle style = Animate(widgetId, "image-button",
        selected ? State_Checked : State_None, interaction.hovered, interaction.held);
    DrawBox(rect, style);
    if (imageId != 0) {
        draw_.AddQuad({rect.x + style.padding.left, rect.y + style.padding.top,
                       std::max(0.f, rect.w - style.padding.Horizontal()),
                       std::max(0.f, rect.h - style.padding.Vertical())},
                      {0.f, flipY ? 1.f : 0.f, 1.f, flipY ? -1.f : 1.f},
                      Color{}, imageId);
    } else {
        DrawStyledColor(rect, fallbackColor, {}, style.opacity, style.cornerRadii);
        const Vec2 textSize = text_->Measure(fallbackLabel, style.fontSize);
        EmitText({rect.x + (rect.w - textSize.x) * 0.5f,
                  rect.y + (rect.h - textSize.y) * 0.5f}, fallbackLabel, style);
    }
    PopID();
    return interaction.pressed && !IsDisabled();
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
    SetLastItem(GetID("##image-widget"), rect, {});
    PopID();
}

void Context::EmptyWidget(std::string_view id, Vec2 size) {
    PushID(id);
    const ComputedStyle style = Resolve("empty-widget", 0);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) size.y = style.fontSize * 6.f + style.padding.Vertical();
    const Rect rect = Place(size, style.spacing);
    DrawBox(rect, style);
    SetLastItem(GetID(id), rect, {});
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
    SetLastItem(GetID(label), r, {});
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
    if (!dropdowns_[id]) return false;

    const ComputedStyle popupStyle = Resolve("menu-popup", 0);
    const ComputedStyle itemBase = Resolve("menu-item", 0);
    const float rowHeight = std::max(itemBase.fontSize * 1.4f +
                                     itemBase.padding.Vertical(), 1.f);
    float width = r.w;
    for (std::string_view item : items)
        width = std::max(width, text_->Measure(item, itemBase.fontSize).x +
                                itemBase.padding.Horizontal());
    width += popupStyle.padding.Horizontal();
    const Rect bounds = windowViewport_.w > 0.f && windowViewport_.h > 0.f
        ? windowViewport_ : Rect{0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    const float height = std::min(rowHeight * static_cast<float>(items.size()) +
                                  popupStyle.padding.Vertical(), bounds.h);
    float x = std::clamp(r.x, bounds.x, std::max(bounds.x, bounds.x + bounds.w - width));
    float y = r.y + r.h;
    if (y + height > bounds.y + bounds.h) y = r.y - height;
    y = std::clamp(y, bounds.y, std::max(bounds.y, bounds.y + bounds.h - height));
    const Rect popup{x, y, std::min(width, bounds.w), height};
    DrawList::Layer popupLayer = draw_.BeginLayer();
    DrawBox(popup, popupStyle);
    draw_.PushClipRect(popup);
    interactionClips_.push_back(popup);
    menuPopupRects_.push_back(popup);
    dropdownPopupRects_.push_back(popup);
    float rowY = popup.y + popupStyle.padding.top;
    for (size_t i = 0; i < items.size() && rowY + rowHeight <= popup.y + popup.h; ++i) {
        const Rect row{popup.x + popupStyle.padding.left, rowY,
                       std::max(0.f, popup.w - popupStyle.padding.Horizontal()), rowHeight};
        const WidgetId itemId = HashStr(items[i], HashStr("##dropdown-items", id));
        const bool isSelected = static_cast<int>(i) == current;
        const bool previousDropdownInteraction = dropdownItemInteraction_;
        dropdownItemInteraction_ = true;
        const Interaction itemInteraction = Behavior(itemId, row);
        dropdownItemInteraction_ = previousDropdownInteraction;
        const ComputedStyle itemStyle = Animate(itemId, "menu-item",
            isSelected ? State_Checked : State_None,
            itemInteraction.hovered, itemInteraction.held);
        DrawBox(row, itemStyle);
        EmitText({row.x + itemStyle.padding.left,
                  row.y + (row.h - text_->Measure(items[i], itemStyle.fontSize).y) * 0.5f},
                 items[i], itemStyle);
        if (itemInteraction.pressed) {
            *selected = static_cast<int>(i);
            dropdowns_[id] = false;
            changed = true;
        }
        rowY += rowHeight;
    }
    draw_.PopClipRect();
    interactionClips_.pop_back();
    draw_.EndLayer(popupLayer);
    dropdownPopupLayers_.push_back(popupLayer);
    return changed;
}

void Context::ProgressBar(std::string_view label, float value, Vec2 size) {
    const ComputedStyle style = Resolve("progress-bar", 0);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) size.y = std::max(style.fontSize * 1.4f + style.padding.Vertical(), 20.f);
    const Rect rect = Place(size, style.spacing);
    DrawBox(rect, style);
    const float fraction = std::clamp(value, 0.f, 1.f);
    if (fraction > 0.f) {
        const float inset = std::max(0.f, style.borderWidth);
        const Rect inner{rect.x + inset, rect.y + inset,
                         std::max(0.f, rect.w - inset * 2.f),
                         std::max(0.f, rect.h - inset * 2.f)};
        Rect fill{inner.x, inner.y, inner.w * fraction, inner.h};
        DrawStyledColor(fill, style.accent, style.accentGradient,
                        style.opacity, style.cornerRadii);
    }
    if (!label.empty())
        DrawTextCentered(rect, label, style);
}

bool Context::TileContainer(std::string_view label,
                            const std::vector<std::string_view>& items,
                            int* selected, Vec2 size, GPUImageId icon, Vec2 iconSize) {
    if (!selected) return false;
    PushID(label);
    if (!BeginContainerImpl(size, false, "tile-container")) {
        PopID();
        return false;
    }
    const ComputedStyle style = Resolve("tile", 0);
    const float gap = std::max(0.f, style.spacing);
    const float cellHeight = std::max(style.fontSize * 4.f + style.padding.Vertical(), 64.f);
    const float availableWidth = std::max(1.f, content_.w);
    const size_t columns = std::max<size_t>(1, static_cast<size_t>(
        std::floor((availableWidth + gap) / (96.f + gap))));
    const float cellWidth = std::max(1.f,
        (availableWidth - gap * static_cast<float>(columns - 1)) /
        static_cast<float>(columns));
    bool changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
        sameLine_ = i % columns != 0;
        const Rect cell = Place({cellWidth, cellHeight}, gap);
        const WidgetId itemId = HashStr(items[i], GetID("##tile-items"));
        const bool isSelected = *selected == static_cast<int>(i);
        const Interaction interaction = Behavior(itemId, cell);
        const ComputedStyle tileStyle = Animate(itemId, "tile",
            isSelected ? State_Checked : State_None,
            interaction.hovered, interaction.held);
        DrawBox(cell, tileStyle);
        const float iconWidth = std::min({iconSize.x, iconSize.y, 36.f, cell.w * 0.48f});
        const float iconHeight = iconSize.x > 0.f
            ? iconWidth * iconSize.y / iconSize.x : iconWidth;
        const Rect iconRect{cell.x + (cell.w - iconWidth) * 0.5f,
                            cell.y + tileStyle.padding.top + 5.f,
                            iconWidth, iconHeight};
        if (icon != 0) {
            draw_.AddQuad(iconRect, {0.f, 1.f, 1.f, -1.f},
                          Fade(Color{}, tileStyle.opacity), icon);
        } else {
            DrawStyledColor(iconRect, tileStyle.accent, tileStyle.accentGradient,
                            tileStyle.opacity, tileStyle.cornerRadii);
        }
        const Vec2 textSize = text_->Measure(items[i], tileStyle.fontSize);
        const float textY = iconRect.y + iconRect.h + 4.f;
        draw_.PushClipRect(cell);
        EmitText({cell.x + tileStyle.padding.left,
                  textY + std::max(0.f, (cell.y + cell.h - tileStyle.padding.bottom -
                                            textY - textSize.y) * 0.5f)},
                 items[i], tileStyle);
        draw_.PopClipRect();
        if (interaction.pressed) {
            *selected = static_cast<int>(i);
            changed = true;
        }
    }
    EndContainer();
    return changed;
}

void Context::ConsoleOutput(std::string_view label, std::string_view text, Vec2 size) {
    ConsoleOutputImpl(label, text, {}, size);
}

void Context::ConsoleOutput(std::string_view label,
                            const std::vector<ConsoleLine>& lines, Vec2 size) {
    std::string text;
    std::vector<ConsoleTextType> lineTypes;
    if (lines.empty()) lineTypes.push_back(ConsoleTextType::Normal);
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t start = 0;
        while (true) {
            lineTypes.push_back(lines[i].type);
            const size_t newline = lines[i].text.find('\n', start);
            if (newline == std::string::npos) {
                text.append(lines[i].text.substr(start));
                break;
            }
            text.append(lines[i].text.substr(start, newline - start));
            text.push_back('\n');
            start = newline + 1;
        }
        if (i + 1 < lines.size()) text.push_back('\n');
    }
    ConsoleOutputImpl(label, text, lineTypes, size);
}

void Context::ConsoleOutputImpl(std::string_view label, std::string_view text,
                                const std::vector<ConsoleTextType>& lineTypes,
                                Vec2 size) {
    PushID(label);
    const WidgetId id = GetID("##console-output");
    const ComputedStyle style = Resolve("console-output", 0);
    if (size.x <= 0.f) size.x = content_.w;
    const float lineHeight = std::max(style.fontSize * 1.25f, 1.f);
    size_t lineCount = 1;
    for (char ch : text) if (ch == '\n') ++lineCount;
    if (size.y <= 0.f) size.y = lineHeight * static_cast<float>(lineCount) +
                                style.padding.Vertical();
    const Rect rect = Place(size, style.spacing);
    DrawBox(rect, style);
    const Rect inner{rect.x + style.padding.left, rect.y + style.padding.top,
                     std::max(0.f, rect.w - style.padding.Horizontal()),
                     std::max(0.f, rect.h - style.padding.Vertical())};
    TextSelection& selection = consoleSelections_[id];
    selection.outputRect = rect;
    selection.outputWindow = currentWindowId_;
    const float contentHeight = lineHeight * static_cast<float>(lineCount);
    const float maxScroll = std::max(0.f, contentHeight - inner.h);
    const bool hasOverflow = maxScroll > 0.f;
    selection.outputScrollable = hasOverflow;
    if (!hasOverflow) {
        selection.scrollY = 0.f;
        selection.scrolling = false;
    }
    if (hasOverflow && rect.Contains(in_.mousePos) && in_.mouseWheel != 0.f) {
        selection.scrollY = std::clamp(
            selection.scrollY - in_.mouseWheel * lineHeight * 3.f, 0.f, maxScroll);
        in_.mouseWheel = 0.f;
    }
    selection.scrollY = std::clamp(selection.scrollY, 0.f, maxScroll);
    selection.anchor = std::min(selection.anchor, text.size());
    selection.focus = std::min(selection.focus, text.size());

    const float trackWidth = hasOverflow ? 8.f : 0.f;
    const Rect textViewport{inner.x, inner.y,
                            std::max(0.f, inner.w - (hasOverflow ? trackWidth + 4.f : 0.f)),
                            inner.h};
    const Rect track{inner.x + inner.w - trackWidth, inner.y, trackWidth, inner.h};
    const float thumbHeight = hasOverflow
        ? std::min(track.h, std::max(24.f, track.h * track.h / contentHeight))
        : 0.f;
    float thumbY = track.y + (track.h - thumbHeight) *
        (maxScroll > 0.f ? selection.scrollY / maxScroll : 0.f);
    const Rect thumb{track.x, thumbY, track.w, thumbHeight};
    const WidgetId scrollId = HashStr("##console-scrollbar", id);
    const Interaction scroll = hasOverflow
        ? Behavior(scrollId, thumb.Expanded(3.f)) : Interaction{};
    if (scroll.held) {
        if (!selection.scrolling) {
            selection.scrolling = true;
            selection.scrollDragOffset = in_.mousePos.y - thumb.y;
        }
        const float travel = std::max(1.f, track.h - thumbHeight);
        const float ratio = std::clamp(
            (in_.mousePos.y - selection.scrollDragOffset - track.y) / travel, 0.f, 1.f);
        selection.scrollY = ratio * maxScroll;
        thumbY = track.y + travel * ratio;
    } else if (selection.scrolling && !in_.mouseDown[0]) {
        selection.scrolling = false;
    }
    const bool onScrollbar = hasOverflow && track.Contains(in_.mousePos);
    const Interaction interaction = !onScrollbar && !selection.scrolling
        ? Behavior(id, rect) : Interaction{};
    auto characterAt = [&](Vec2 position) {
        size_t lineStart = 0;
        size_t lineIndex = 0;
        const float contentY = position.y - inner.y + selection.scrollY;
        const size_t targetLine = static_cast<size_t>(
            std::max(0.f, std::floor(contentY / lineHeight)));
        while (lineStart <= text.size()) {
            const size_t lineEnd = text.find('\n', lineStart);
            const size_t end = lineEnd == std::string_view::npos ? text.size() : lineEnd;
            if (lineIndex == targetLine) {
                const std::string_view line = text.substr(lineStart, end - lineStart);
                const float targetX = std::max(0.f, position.x - inner.x);
                size_t byte = 0, nearest = 0;
                float nearestDistance = std::numeric_limits<float>::max();
                while (byte < line.size()) {
                    size_t next = byte + 1;
                    while (next < line.size() &&
                           (static_cast<unsigned char>(line[next]) & 0xC0u) == 0x80u)
                        ++next;
                    const float x = text_->Measure(line.substr(0, next), style.fontSize).x;
                    const float distance = std::abs(targetX - x);
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearest = next;
                    }
                    byte = next;
                }
                if (targetX < text_->Measure(line.substr(0, nearest), style.fontSize).x * 0.5f)
                    nearest = 0;
                return lineStart + nearest;
            }
            if (lineEnd == std::string_view::npos) break;
            lineStart = lineEnd + 1;
            ++lineIndex;
        }
        return text.size();
    };
    if (interaction.held) {
        selectedConsole_ = id;
        const size_t offset = characterAt(in_.mousePos);
        if (!selection.selecting) {
            selection.anchor = offset;
            selection.selecting = true;
        }
        selection.focus = offset;
    } else if (!in_.mouseDown[0] && selection.selecting) {
        selection.selecting = false;
    }
    draw_.PushClipRect(textViewport);
    size_t lineStart = 0;
    size_t lineIndex = 0;
    const size_t selectionStart = std::min(selection.anchor, selection.focus);
    const size_t selectionEnd = std::max(selection.anchor, selection.focus);
    selection.text = std::string(text.substr(selectionStart, selectionEnd - selectionStart));
    while (lineStart <= text.size()) {
        const size_t lineEnd = text.find('\n', lineStart);
        const size_t end = lineEnd == std::string_view::npos ? text.size() : lineEnd;
        const std::string_view line = text.substr(lineStart, end - lineStart);
        const size_t selectedStart = std::clamp(selectionStart, lineStart, end);
        const size_t selectedEnd = std::clamp(selectionEnd, lineStart, end);
        const float lineY = inner.y + lineHeight * static_cast<float>(lineIndex) -
                            selection.scrollY;
        if (selectedEnd > selectedStart) {
            const float x0 = text_->Measure(text.substr(lineStart, selectedStart - lineStart),
                                            style.fontSize).x;
            const float x1 = text_->Measure(text.substr(lineStart, selectedEnd - lineStart),
                                            style.fontSize).x;
            DrawStyledColor({inner.x + x0, lineY,
                             std::max(0.f, x1 - x0), lineHeight},
                            style.accent, style.accentGradient, style.opacity * 0.55f,
                            style.cornerRadii);
        }
        ComputedStyle lineStyle = style;
        if (lineIndex < lineTypes.size()) {
            std::string_view lineSelector = "console-output-normal";
            switch (lineTypes[lineIndex]) {
                case ConsoleTextType::Warning:
                    lineSelector = "console-output-warning";
                    break;
                case ConsoleTextType::Error:
                    lineSelector = "console-output-error";
                    break;
                case ConsoleTextType::Normal:
                    break;
            }
            const ComputedStyle typeStyle = Resolve(lineSelector, 0);
            lineStyle.foreground = typeStyle.foreground;
            lineStyle.textGradient = typeStyle.textGradient;
        }
        EmitText({inner.x, lineY}, line, lineStyle);
        if (lineEnd == std::string_view::npos) break;
        lineStart = lineEnd + 1;
        ++lineIndex;
    }
    draw_.PopClipRect();
    if (hasOverflow) {
        const ComputedStyle trackStyle = Resolve("window-scrollbar", 0);
        DrawStyledColor(track, trackStyle.background, trackStyle.backgroundGradient,
                        trackStyle.opacity, trackWidth * 0.5f);
        DrawStyledColor({thumb.x, thumbY, thumb.w, thumb.h},
                        trackStyle.accent, trackStyle.accentGradient,
                        trackStyle.opacity, trackWidth * 0.5f);
    }
    PopID();
}

bool Context::ConsoleInput(std::string_view label, std::string* value, Vec2 size) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    const ComputedStyle style = Resolve("console-input", 0);
    if (size.x <= 0.f) size.x = content_.w;
    if (size.y <= 0.f) size.y = style.fontSize * 1.5f + style.padding.Vertical();
    const Rect rect = Place(size, style.spacing);
    focusedConsoleInputRect_ = rect;
    const Interaction interaction = Behavior(id, rect);
    if (interaction.held) focusedConsoleInput_ = id;
    if (interaction.pressed) focusedConsoleInput_ = id;
    const bool focused = focusedConsoleInput_ == id;
    if (focused) wantsKeyboard_ = true;
    TextSelection& selection = consoleSelections_[id];
    selection.anchor = std::min(selection.anchor, value->size());
    selection.focus = std::min(selection.focus, value->size());
    const Rect inner{rect.x + style.padding.left, rect.y + style.padding.top,
                     std::max(0.f, rect.w - style.padding.Horizontal()),
                     std::max(0.f, rect.h - style.padding.Vertical())};
    auto characterAt = [&]() {
        const float targetX = std::max(0.f, in_.mousePos.x - inner.x);
        size_t byte = 0, nearest = 0;
        float nearestDistance = std::numeric_limits<float>::max();
        while (byte < value->size()) {
            size_t next = byte + 1;
            while (next < value->size() &&
                   (static_cast<unsigned char>((*value)[next]) & 0xC0u) == 0x80u)
                ++next;
            const float x = text_->Measure(std::string_view(*value).substr(0, next),
                                           style.fontSize).x;
            const float distance = std::abs(targetX - x);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = next;
            }
            byte = next;
        }
        if (targetX < text_->Measure(std::string_view(*value).substr(0, nearest),
                                    style.fontSize).x * 0.5f)
            nearest = 0;
        return nearest;
    };
    if (interaction.held) {
        selectedConsole_ = id;
        const size_t offset = characterAt();
        if (!selection.selecting) {
            selection.anchor = offset;
            selection.selecting = true;
        }
        selection.focus = offset;
    } else if (!in_.mouseDown[0] && selection.selecting) {
        selection.selecting = false;
    }

    bool submitted = false;
    if (focused && currentWindowId_ == focusedWindow_) {
        if (in_.keySelectAll) {
            selection.anchor = 0;
            selection.focus = value->size();
        }
        if (in_.keyBackspace) {
            const size_t first = std::min(selection.anchor, selection.focus);
            const size_t last = std::max(selection.anchor, selection.focus);
            if (last > first) {
                value->erase(first, last - first);
                selection.anchor = selection.focus = first;
            } else if (first > 0) {
                size_t previous = first - 1;
                while (previous > 0 &&
                       (static_cast<unsigned char>((*value)[previous]) & 0xC0u) == 0x80u)
                    --previous;
                value->erase(previous, first - previous);
                selection.anchor = selection.focus = previous;
            }
        }
        if (!in_.textInput.empty()) {
            const size_t first = std::min(selection.anchor, selection.focus);
            const size_t last = std::max(selection.anchor, selection.focus);
            value->replace(first, last - first, in_.textInput);
            selection.anchor = selection.focus = first + in_.textInput.size();
        }
        if (in_.keyEnter) {
            submitted = true;
            focusedConsoleInput_ = 0;
        }
    }
    const size_t selectionStart = std::min(selection.anchor, selection.focus);
    const size_t selectionEnd = std::max(selection.anchor, selection.focus);
    selection.text = value->substr(selectionStart, selectionEnd - selectionStart);
    DrawBox(rect, style);
    draw_.PushClipRect(inner);
    if (selectionEnd > selectionStart) {
        const float x0 = text_->Measure(std::string_view(*value).substr(0, selectionStart),
                                        style.fontSize).x;
        const float x1 = text_->Measure(std::string_view(*value).substr(0, selectionEnd),
                                        style.fontSize).x;
        DrawStyledColor({inner.x + x0, inner.y, std::max(0.f, x1 - x0), inner.h},
                        style.accent, style.accentGradient, style.opacity * 0.55f,
                        style.cornerRadii);
    }
    EmitText({inner.x, inner.y + std::max(0.f,
        (inner.h - text_->Measure(*value, style.fontSize).y) * 0.5f)}, *value, style);
    if (focused && selectionStart == selectionEnd) {
        const float caretX = text_->Measure(std::string_view(*value).substr(0, selectionEnd),
                                            style.fontSize).x;
        DrawStyledColor({inner.x + caretX, inner.y + 2.f, 1.f,
                         std::max(0.f, inner.h - 4.f)},
                        style.foreground, style.textGradient, style.opacity);
    }
    draw_.PopClipRect();
    return submitted;
}

std::string Context::SelectedConsoleText() const {
    const auto selection = consoleSelections_.find(selectedConsole_);
    return selection == consoleSelections_.end() ? std::string{} : selection->second.text;
}

bool Context::RMB_Start() {
    if (currentWindowId_ == 0 || lastItemId_ == 0) return false;
    const bool rightPressed = in_.mouseDown[1] && !prevRightMouseDown_;
    if (rightPressed && lastItemRect_.Contains(in_.mousePos) && windowInputEnabled_) {
        rmbPopupOwner_ = lastItemId_;
        rmbPopupAnchor_ = in_.mousePos;
        rmbPopupWidth_ = 160.f;
        rmbPopupHeight_ = 0.f;
        rmbPopupItemCount_ = 0;
    }
    if (rmbPopupOwner_ != lastItemId_) return false;
    const ComputedStyle style = Resolve("context-menu", 0);
    const ComputedStyle itemStyle = Resolve("context-menu-item", 0);
    const float rowHeight = itemStyle.fontSize * 1.4f + itemStyle.padding.Vertical();
    const Rect bounds = windowViewport_.w > 0.f && windowViewport_.h > 0.f
        ? windowViewport_ : Rect{0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    const float height = std::clamp(std::max(rmbPopupHeight_, rowHeight) +
                                    style.padding.Vertical(), 1.f,
                                    std::max(1.f, bounds.h));
    const float width = std::min(std::max(rmbPopupWidth_ + style.padding.Horizontal(), 1.f),
                                 std::max(1.f, bounds.w));
    float x = rmbPopupAnchor_.x;
    float y = rmbPopupAnchor_.y;
    if (x + width > bounds.x + bounds.w) x -= width;
    if (y + height > bounds.y + bounds.h) y -= height;
    x = std::clamp(x, bounds.x, std::max(bounds.x, bounds.x + bounds.w - width));
    y = std::clamp(y, bounds.y, std::max(bounds.y, bounds.y + bounds.h - height));
    rmbPopupRect_ = {x, y, width, height};
    rmbPopupLayer_ = draw_.BeginLayer();
    DrawBox(rmbPopupRect_, style);
    draw_.PushClipRect(rmbPopupRect_);
    interactionClips_.push_back(rmbPopupRect_);
    menuPopupRects_.push_back(rmbPopupRect_);
    rmbPopupCursorY_ = y + style.padding.top;
    rmbPopupItemCount_ = 0;
    rmbPopupWidth_ = 0.f;
    rmbPopupActiveThisFrame_ = true;
    return true;
}

bool Context::RMB_Item(std::string_view label) {
    if (!rmbPopupActiveThisFrame_) return false;
    const ComputedStyle base = Resolve("context-menu-item", 0);
    const float rowHeight = base.fontSize * 1.4f + base.padding.Vertical();
    const Rect row{rmbPopupRect_.x, rmbPopupCursorY_,
                   rmbPopupRect_.w, rowHeight};
    const WidgetId id = HashStr(label, rmbPopupOwner_);
    rmbItemInteraction_ = true;
    const Interaction interaction = Behavior(id, row);
    rmbItemInteraction_ = false;
    const ComputedStyle style = Animate(id, "context-menu-item", State_None,
                                        interaction.hovered, interaction.held);
    DrawBox(row, style);
    EmitText({row.x + style.padding.left,
              row.y + (row.h - text_->Measure(label, style.fontSize).y) * 0.5f},
             label, style);
    rmbPopupCursorY_ += rowHeight;
    rmbPopupWidth_ = std::max(rmbPopupWidth_,
        text_->Measure(label, style.fontSize).x + style.padding.Horizontal());
    ++rmbPopupItemCount_;
    if (interaction.pressed) rmbPopupOwner_ = 0;
    return interaction.pressed;
}

void Context::RMB_End() {
    if (!rmbPopupActiveThisFrame_) return;
    const ComputedStyle style = Resolve("context-menu", 0);
    const ComputedStyle itemStyle = Resolve("context-menu-item", 0);
    rmbPopupHeight_ = static_cast<float>(rmbPopupItemCount_) *
        (itemStyle.fontSize * 1.4f + itemStyle.padding.Vertical());
    rmbPopupWidth_ += style.padding.Horizontal();
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    draw_.EndLayer(rmbPopupLayer_);
    rmbPopupHasLayer_ = true;
    rmbPopupActiveThisFrame_ = false;
}

bool Context::LMB_Start() {
    lmbScopeActive_ = lastItemId_ != 0 && lastItemPressed_ && !in_.mouseDown[0];
    return lmbScopeActive_;
}

void Context::LMB_End() {
    lmbScopeActive_ = false;
}

bool Context::BeginCollapsible(std::string_view label, bool* open, Vec2 size) {
    if (!open) return false;
    const WidgetId id = GetID(label);
    const std::string header = std::string(*open ? "v " : "> ") + std::string(DisplayLabel(label));
    if (Button(header)) *open = !*open;
    if (!*open) return false;
    size.x = content_.w;
    PushID(label);
    return BeginContainerImpl(size, false, "collapsible-container");
}

} // namespace fluid