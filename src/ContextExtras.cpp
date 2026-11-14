#include "FluidUI/Context.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace fluid {
namespace {

std::string_view VisibleLabel(std::string_view text) {
    const size_t marker = text.find("##");
    return marker == std::string_view::npos ? text : text.substr(0, marker);
}

bool KeyDown(const InputState& input, size_t key) {
    return key < InputState::KeyCount && input.keysDown[key];
}

bool KeyPressed(const InputState& input, size_t key) {
    return key < InputState::KeyCount && input.keysPressed[key];
}

size_t PreviousCodepoint(std::string_view text, size_t offset) {
    if (offset == 0) return 0;
    size_t index = std::min(offset - 1, text.size() - 1);
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
        --index;
    return index;
}

size_t NextCodepoint(std::string_view text, size_t offset) {
    if (offset >= text.size()) return text.size();
    size_t index = offset + 1;
    while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
        ++index;
    return index;
}

std::string EncodeUtf8(uint32_t codepoint) {
    std::string result;
    if (codepoint <= 0x7F) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF && (codepoint < 0xD800 || codepoint > 0xDFFF)) {
        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return result;
}

} // namespace

bool Context::LoadLayout(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Could not open FluidUI layout: " + path;
        return false;
    }

    std::string signature;
    unsigned version = 0;
    if (!(input >> signature >> version) || signature != "FLUIDUI_LAYOUT" || version != 1) {
        error = "Unsupported or malformed FluidUI layout header: " + path;
        return false;
    }

    float leftRatio = dockLeftRatio_, rightRatio = dockRightRatio_;
    float topRatio = dockTopRatio_, bottomRatio = dockBottomRatio_;
    bool hasRatios = false;
    std::unordered_map<WidgetId, WindowState> loadedWindows;
    std::string record;
    while (input >> record) {
        if (record == "R") {
            if (hasRatios || !(input >> leftRatio >> rightRatio >> topRatio >> bottomRatio) ||
                !std::isfinite(leftRatio) || !std::isfinite(rightRatio) ||
                !std::isfinite(topRatio) || !std::isfinite(bottomRatio) ||
                leftRatio < 0.f || leftRatio > 1.f || rightRatio < 0.f || rightRatio > 1.f ||
                topRatio < 0.f || topRatio > 1.f || bottomRatio < 0.f || bottomRatio > 1.f) {
                error = "Invalid docking ratios in FluidUI layout: " + path;
                return false;
            }
            hasRatios = true;
        } else if (record == "W") {
            WidgetId id = 0;
            int open = 0, dock = 0;
            WindowState state;
            if (!(input >> id >> open >> dock
                        >> state.pos.x >> state.pos.y >> state.size.x >> state.size.y
                        >> state.dockRestoreSize.x >> state.dockRestoreSize.y
                        >> state.dockSpan >> state.zOrder) ||
                id == 0 || (open != 0 && open != 1) ||
                dock < static_cast<int>(DockSide::None) ||
                dock > static_cast<int>(DockSide::BottomRight) ||
                !std::isfinite(state.pos.x) || !std::isfinite(state.pos.y) ||
                !std::isfinite(state.size.x) || !std::isfinite(state.size.y) ||
                !std::isfinite(state.dockRestoreSize.x) ||
                !std::isfinite(state.dockRestoreSize.y) ||
                !std::isfinite(state.dockSpan) || state.size.x <= 0.f || state.size.y <= 0.f ||
                state.dockSpan <= 0.f || loadedWindows.contains(id)) {
                error = "Invalid window record in FluidUI layout: " + path;
                return false;
            }
            state.init = true;
            state.open = open != 0;
            state.dock = static_cast<DockSide>(dock);
            loadedWindows.emplace(id, state);
        } else {
            error = "Unknown record in FluidUI layout: " + record;
            return false;
        }
    }
    if (input.bad()) {
        error = "Could not read FluidUI layout: " + path;
        return false;
    }

    dockLeftRatio_ = leftRatio;
    dockRightRatio_ = rightRatio;
    dockTopRatio_ = topRatio;
    dockBottomRatio_ = bottomRatio;
    for (auto& side : dockedWindows_) side.clear();
    nextWindowZ_ = 0;
    for (const auto& [id, state] : loadedWindows) {
        windows_[id] = state;
        nextWindowZ_ = std::max(nextWindowZ_, state.zOrder);
        if (state.dock != DockSide::None)
            dockedWindows_[static_cast<size_t>(state.dock)].push_back(id);
    }
    focusedWindow_ = 0;
    dockLayoutDone_ = false;
    error.clear();
    return true;
}

bool Context::SaveLayout(const std::string& path, std::string& error) const {
    if (path.empty()) {
        error = "FluidUI layout path is empty";
        return false;
    }
    const std::filesystem::path destination(path);
    std::error_code fileError;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), fileError);
    if (fileError) {
        error = "Could not create FluidUI layout directory: " + fileError.message();
        return false;
    }

    std::ofstream output(destination, std::ios::trunc);
    if (!output) {
        error = "Could not write FluidUI layout: " + path;
        return false;
    }
    output << "FLUIDUI_LAYOUT 1\n" << std::setprecision(9)
           << "R " << dockLeftRatio_ << ' ' << dockRightRatio_ << ' '
           << dockTopRatio_ << ' ' << dockBottomRatio_ << '\n';
    for (const auto& [id, state] : windows_) {
        if (!state.init) continue;
        output << "W " << id << ' ' << (state.open ? 1 : 0) << ' '
               << static_cast<int>(state.dock) << ' '
               << state.pos.x << ' ' << state.pos.y << ' '
               << state.size.x << ' ' << state.size.y << ' '
               << state.dockRestoreSize.x << ' ' << state.dockRestoreSize.y << ' '
               << state.dockSpan << ' ' << state.zOrder << '\n';
    }
    output.flush();
    if (!output) {
        error = "Could not finish writing FluidUI layout: " + path;
        return false;
    }
    error.clear();
    return true;
}

void Context::TextWrapped(std::string_view text) {
    const ComputedStyle style = Resolve("text", 0);
    const float availableWidth = std::max(1.f, content_.w);
    const float lineHeight = std::max(1.f, style.fontSize * 1.2f + style.padding.Vertical());
    std::string line;
    auto emitLine = [&]() {
        const Vec2 measured = text_->Measure(line, style.fontSize);
        const Rect rect = Place({std::min(availableWidth, measured.x + style.padding.Horizontal()),
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

bool Context::DragFloat(std::string_view label, float* value, float speed,
                        float minV, float maxV) {
    if (!value) return false;
    const WidgetId id = GetID(label);
    if (auto editing = numericInputs_.find(id); editing != numericInputs_.end()) {
        const bool disabled = IsDisabled();
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
        bool changed = false;
        if (!disabled && end != editing->second.text.c_str() && end && *end == '\0' &&
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
    const Rect rect = Place({content_.w, height}, base.spacing);
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
    const std::string_view visible = VisibleLabel(label);
    char valueText[64];
    std::snprintf(valueText, sizeof(valueText), "%.3f", *value);
    const std::string display = visible.empty()
        ? std::string(valueText)
        : std::string(visible) + ": " + valueText;
    const Vec2 measured = text_->Measure(display, style.fontSize);
    EmitText({rect.x + style.padding.left,
              rect.y + (rect.h - measured.y) * 0.5f}, display, style);
    if (interaction.doubleClicked && !IsDisabled()) {
        char valueText[64];
        std::snprintf(valueText, sizeof(valueText), "%.3f", *value);
        numericInputs_.emplace(id, NumericInputState{valueText});
    }
    return changed;
}

bool Context::DragInt(std::string_view label, int* value, float speed,
                      int minV, int maxV) {
    if (!value) return false;
    float edited = static_cast<float>(*value);
    const bool changed = DragFloat(label, &edited, speed,
                                   static_cast<float>(minV), static_cast<float>(maxV));
    if (changed) {
        const int next = minV < maxV
            ? std::clamp(static_cast<int>(std::lround(edited)), minV, maxV)
            : static_cast<int>(std::lround(edited));
        if (next != *value) *value = next;
        else return false;
    }
    return changed;
}

bool Context::ColorEdit3(std::string_view label, float* value) {
    if (!value) return false;
    bool changed = false;
    PushID(label);
    const float width = std::max(1.f, (content_.w - 2.f * Resolve("text", 0).spacing) / 3.f);
    for (int i = 0; i < 3; ++i) {
        if (i) SameLine();
        SetNextItemWidth(width);
        const char* names[] = {"R##r", "G##g", "B##b"};
        changed = DragFloat(names[i], value + i, 0.005f, 0.f, 1.f) || changed;
    }
    PopID();
    return changed;
}

bool Context::ColorEdit4(std::string_view label, float* value) {
    if (!value) return false;
    bool changed = false;
    PushID(label);
    const float width = std::max(1.f, (content_.w - 3.f * Resolve("text", 0).spacing) / 4.f);
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
    const Rect rect = Place({content_.w, height}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    TextInputState& state = textInputs_[id];
    state.cursor = std::min(state.cursor, value->size());
    state.selectionAnchor = std::min(state.selectionAnchor, value->size());
    bool changed = false;
    if (interaction.clicked || (focusNextTextInput_ && !IsDisabled())) {
        const bool selectAll = focusNextTextInput_ && focusNextTextInputSelectAll_;
        if (focusedTextInput_ != id) {
            lastItemDeactivated_ = focusedTextInput_ != 0;
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
            if (targetX > closest) cursor = value->size();
            state.cursor = cursor;
        } else {
            state.cursor = value->size();
        }
        state.selectionAnchor = state.cursor;
        state.initialized = true;
    }
    if (focusedTextInput_ == id && interaction.held && !interaction.clicked) {
        const float targetX = std::max(0.f, in_.mousePos.x -
            rect.x - base.padding.left - 2.f + state.scrollX);
        float closest = 0.f;
        size_t cursor = 0;
        for (size_t next = 0; next < value->size();) {
            next = NextCodepoint(*value, next);
            const float advance = text_->Measure(value->substr(0, next), base.fontSize).x;
            if (std::abs(advance - targetX) < std::abs(closest - targetX)) {
                closest = advance;
                cursor = next;
            }
        }
        if (targetX > closest) cursor = value->size();
        state.cursor = cursor;
    }
    focusNextTextInput_ = false;
    focusNextTextInputSelectAll_ = false;
    if (deactivatedTextInput_ == id) {
        lastItemDeactivated_ = true;
        deactivatedTextInput_ = 0;
    }

    bool didSubmit = false;
    if (focusedTextInput_ == id && !IsDisabled()) {
        wantsKeyboard_ = true;
        const bool shift = KeyDown(in_, 340) || KeyDown(in_, 344);
        const bool control = KeyDown(in_, 341) || KeyDown(in_, 345);
        auto selection = [&]() {
            const size_t begin = std::min(state.cursor, state.selectionAnchor);
            const size_t end = std::max(state.cursor, state.selectionAnchor);
            return std::pair<size_t, size_t>{begin, end};
        };
        auto eraseSelection = [&]() {
            const auto [begin, end] = selection();
            if (begin == end) return false;
            value->erase(begin, end - begin);
            state.cursor = state.selectionAnchor = begin;
            return true;
        };
        if (KeyPressed(in_, 65) && control) {
            state.selectionAnchor = 0;
            state.cursor = value->size();
        } else if ((KeyPressed(in_, 67) || KeyPressed(in_, 88)) && control) {
            const auto [begin, end] = selection();
            if (begin != end) {
                const std::string selected = value->substr(begin, end - begin);
                if (in_.setClipboardText) in_.setClipboardText(selected);
                if (KeyPressed(in_, 88)) changed = eraseSelection();
            }
        } else if (KeyPressed(in_, 86) && control) {
            changed = eraseSelection() || changed;
            const std::string clipboard = in_.clipboardText;
            value->insert(state.cursor, clipboard);
            state.cursor += clipboard.size();
            state.selectionAnchor = state.cursor;
            changed = !clipboard.empty() || changed;
        } else {
            if (KeyPressed(in_, 263)) {
                if (!shift && state.cursor != state.selectionAnchor)
                    state.cursor = std::min(state.cursor, state.selectionAnchor);
                else state.cursor = PreviousCodepoint(*value, state.cursor);
                if (!shift) state.selectionAnchor = state.cursor;
            }
            if (KeyPressed(in_, 262)) {
                if (!shift && state.cursor != state.selectionAnchor)
                    state.cursor = std::max(state.cursor, state.selectionAnchor);
                else state.cursor = NextCodepoint(*value, state.cursor);
                if (!shift) state.selectionAnchor = state.cursor;
            }
            if (KeyPressed(in_, 268)) {
                state.cursor = value->rfind('\n', state.cursor == 0 ? 0 : state.cursor - 1);
                if (state.cursor == std::string::npos) state.cursor = 0;
                if (!shift) state.selectionAnchor = state.cursor;
            }
            if (KeyPressed(in_, 269)) {
                const size_t lineEnd = value->find('\n', state.cursor);
                state.cursor = lineEnd == std::string::npos ? value->size() : lineEnd;
                if (!shift) state.selectionAnchor = state.cursor;
            }
            if (KeyPressed(in_, 259)) {
                if (eraseSelection()) {
                    changed = true;
                } else if (state.cursor > 0) {
                    const size_t previous = PreviousCodepoint(*value, state.cursor);
                    value->erase(previous, state.cursor - previous);
                    state.cursor = state.selectionAnchor = previous;
                    changed = true;
                }
            }
            if (KeyPressed(in_, 261)) {
                if (eraseSelection()) {
                    changed = true;
                } else if (state.cursor < value->size()) {
                    value->erase(state.cursor, NextCodepoint(*value, state.cursor) - state.cursor);
                    state.selectionAnchor = state.cursor;
                    changed = true;
                }
            }
            if (KeyPressed(in_, 257) || KeyPressed(in_, 335)) {
                if (multiline && !submitOnEnter && !control) {
                    changed = eraseSelection() || changed;
                    value->insert(state.cursor, 1, '\n');
                    ++state.cursor;
                    state.selectionAnchor = state.cursor;
                    changed = true;
                } else {
                    didSubmit = true;
                    focusedTextInput_ = 0;
                    lastItemDeactivated_ = true;
                }
            }
            if (!didSubmit) {
                for (uint32_t codepoint : in_.textInput) {
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
    } else if (focusedTextInput_ == id) {
        focusedTextInput_ = 0;
        lastItemDeactivated_ = true;
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
    if (focused) {
        state.scrollX = std::max(0.f, caretMeasure.x -
            std::max(0.f, rect.w - style.padding.Horizontal() - 8.f));
    }
    const auto [selectionStart, selectionEnd] = std::minmax(state.cursor, state.selectionAnchor);
    const float selectionX = text_->Measure(value->substr(0, selectionStart), style.fontSize).x;
    const float selectionWidth = text_->Measure(value->substr(selectionStart,
        selectionEnd - selectionStart), style.fontSize).x;
    if (focused && selectionWidth > 0.f)
        draw_.AddRectFilled({textX + selectionX - state.scrollX, textY,
                             selectionWidth, style.fontSize * 1.2f},
                            style.accent);
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
    const std::string_view visible = VisibleLabel(label);
    const Vec2 textSize = text_->Measure(visible, base.fontSize);
    const Rect rect = Place({content_.w, textSize.y + base.padding.Vertical()}, base.spacing);
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
    bool& open = openIt->second;
    const ComputedStyle style = Resolve("tree-node", (flags & 1u) ? State_Checked : State_None);
    const std::string_view visible = VisibleLabel(label);
    const Vec2 measured = text_->Measure(visible, style.fontSize);
    const float rowHeight = std::max(measured.y, style.fontSize * 1.15f) + style.padding.Vertical();
    const Rect rect = Place({content_.w, rowHeight}, style.spacing);
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
            draw_.AddTriangleFilled({iconX, iconY}, {iconX + iconSize, iconY + iconSize * 0.5f},
                                    {iconX, iconY + iconSize}, animated.foreground);
    }
    EmitText({iconX + iconSize + animated.spacing,
              rect.y + (rect.h - measured.y) * 0.5f}, visible, animated);
    const bool expanded = leaf || open;
    if (expanded && !leaf) {
        const float indent = animated.fontSize * 1.1f;
        treeNodeStack_.push_back({content_.x, content_.w, true});
        content_.x += indent;
        content_.w = std::max(0.f, content_.w - indent);
    } else if (expanded) {
        treeNodeStack_.push_back({content_.x, content_.w, false});
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
    bool& open = it->second;
    const std::string_view visible = VisibleLabel(label);
    const ComputedStyle base = Resolve("collapsing-header", open ? State_Checked : State_None);
    const Vec2 measured = text_->Measure(visible, base.fontSize);
    const Rect rect = Place({content_.w, measured.y + base.padding.Vertical()}, base.spacing);
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

bool Context::IsItemHovered() const { return lastItemHovered_; }

bool Context::IsItemClicked(int mouseButton) const {
    if (mouseButton < 0 || mouseButton >= 3) return false;
    if (mouseButton == 0) return lastItemClicked_;
    const bool pressed = in_.mousePressed[mouseButton] ||
        (in_.mouseDown[mouseButton] && !prevMouseDownButtons_[mouseButton]);
    return pressed && lastItemRect_.Contains(in_.mousePos) &&
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

bool Context::IsWindowFocused() const { return currentWindowId_ != 0 && focusedWindow_ == currentWindowId_; }

bool Context::WantsMouseAt(Vec2 position) const {
    if (active_ != 0 || !openPopups_.empty() ||
        (menuBarHeight_ > 0.f && position.y < menuBarHeight_))
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
    if (!lastItemHovered_ || text.empty()) return;
    pendingTooltips_.emplace_back(text);
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

void Context::InteractiveImage(GPUImageId imageId, Vec2 size, bool flipY) {
    const WidgetId id = GetID("##interactive-image");
    const ComputedStyle base = Resolve("image-button", 0);
    const Rect rect = Place({size.x + base.padding.Horizontal(),
                             size.y + base.padding.Vertical()}, base.spacing);
    const Interaction interaction = Behavior(id, rect);
    const ComputedStyle style = Animate(id, "image-button", State_None,
                                        interaction.hovered, interaction.held);
    DrawBox(rect, style);
    draw_.AddQuad({rect.x + style.padding.left, rect.y + style.padding.top,
                   size.x, size.y},
                  {0.f, flipY ? 1.f : 0.f, 1.f, flipY ? -1.f : 1.f},
                  Color{1.f, 1.f, 1.f, style.opacity}, imageId);
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
        if (!candidate.open || keepOpen.count(candidateId) != 0) continue;
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
        if (candidate.open && keepOpen.count(candidateId) == 0) {
            candidate.open = false;
            candidate.hasLayer = false;
        }
    }
    openPopups_.erase(std::remove_if(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId candidateId) { return keepOpen.count(candidateId) == 0; }),
        openPopups_.end());

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
    return true;
}

bool Context::BeginPopupImpl(std::string_view id, bool modal) {
    const WidgetId popupId = GetID(id);
    auto found = popups_.find(popupId);
    if (found == popups_.end() || !found->second.open) return false;
    PopupState& popup = found->second;
    popup.modal = modal;
    popup.clip = {0.f, 0.f, in_.displaySize.x, in_.displaySize.y};
    if (modal) {
        wantsMouse_ = true;
        wantsKeyboard_ = true;
        popup.ownerWindowId = currentWindowId_;
    } else {
        if ((in_.mousePressed[0] || (in_.mouseDown[0] && !prevMouseDown_)) &&
            !popup.frame.Contains(in_.mousePos) &&
            !popup.anchor.Contains(in_.mousePos)) {
            ClosePopupAndChildren(popupId);
            return false;
        }
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
    const ComputedStyle style = Resolve("popup", 0);
    LayoutState outer{content_, cursorY_, lineTop_, lineBottom_, lastRight_, cursorX_,
                      sameLine_, horizontalLayout_};
    PopupStackEntry entry;
    entry.outer = outer;
    entry.restoreClips = interactionClips_;
    for (size_t i = 0; i < entry.restoreClips.size(); ++i)
        draw_.PopClipRect();
    interactionClips_.clear();
    entry.layer = draw_.BeginLayer();
    entry.id = popupId;
    popupStack_.push_back(entry);
    popup.hasLayer = true;
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
    std::string popupName;
    if (id.empty()) {
        popupName = "##context-item-" + std::to_string(lastItemId_);
        id = popupName;
    }
    const bool rightPressed = in_.mousePressed[1] ||
        (in_.mouseDown[1] && !prevMouseDownButtons_[1]);
    if (rightPressed && lastItemRect_.Contains(in_.mousePos)) OpenPopup(id);
    return BeginPopup(id);
}

bool Context::BeginPopupContextWindow(std::string_view id) {
    std::string popupName;
    if (id.empty()) {
        popupName = "##context-window-" + std::to_string(currentWindowId_);
        id = popupName;
    }
    const bool rightPressed = in_.mousePressed[1] ||
        (in_.mouseDown[1] && !prevMouseDownButtons_[1]);
    if (rightPressed && hoveredItem_ == 0 &&
        windowViewport_.Contains(in_.mousePos)) {
        OpenPopup(id);
        PopupState& popup = popups_[GetID(id)];
        popup.anchor = {in_.mousePos.x, in_.mousePos.y, 1.f, 1.f};
        popup.pos = in_.mousePos;
    }
    return BeginPopup(id);
}

void Context::CloseCurrentPopup() {
    if (popupStack_.empty()) return;
    ClosePopupAndChildren(popupStack_.back().id);
}

void Context::ClosePopupAndChildren(WidgetId id) {
    std::unordered_set<WidgetId> toClose{id};
    for (const auto& [candidateId, candidate] : popups_) {
        WidgetId ancestor = candidate.parentPopupId;
        while (ancestor != 0) {
            if (ancestor == id) {
                toClose.insert(candidateId);
                break;
            }
            const auto parent = popups_.find(ancestor);
            if (parent == popups_.end()) break;
            ancestor = parent->second.parentPopupId;
        }
    }
    for (WidgetId popupId : toClose) {
        const auto popup = popups_.find(popupId);
        if (popup != popups_.end()) {
            popup->second.open = false;
            popup->second.hasLayer = false;
        }
    }
    openPopups_.erase(std::remove_if(openPopups_.begin(), openPopups_.end(),
        [&](WidgetId popupId) { return toClose.count(popupId) != 0; }),
        openPopups_.end());
}

void Context::EndPopupImpl() {
    if (popupStack_.empty()) return;
    draw_.PopClipRect();
    if (!interactionClips_.empty()) interactionClips_.pop_back();
    if (!idStack_.empty()) idStack_.pop_back();
    PopupStackEntry entry = popupStack_.back();
    popupStack_.pop_back();
    draw_.EndLayer(entry.layer);
    popups_[entry.id].layer = entry.layer;
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
}

void Context::EndPopup() { EndPopupImpl(); }

bool Context::BeginDragSource() {
    if (dragSourceScope_ || lastItemId_ == 0 || active_ != lastItemId_ || !in_.mouseDown[0] ||
        !lastItemRect_.Contains(dragStartPosition_)) return false;
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
    dragPayloadType_.assign(type);
    dragPayloadData_.assign(data);
    draggingPayload_ = true;
}

void Context::EndDragSource() { dragSourceScope_ = false; }

bool Context::BeginDropTarget() {
    const bool hovered = windowInputEnabled_ && lastItemRect_.Contains(in_.mousePos) &&
        std::all_of(interactionClips_.begin(), interactionClips_.end(),
            [&](const Rect& clip) { return clip.Contains(in_.mousePos); });
    return draggingPayload_ && lastItemId_ != 0 && lastItemId_ != dragSourceId_ &&
           hovered;
}

bool Context::AcceptDrop(std::string_view type, std::string& data) {
    const bool released = in_.mouseReleased[0] ||
                          (!in_.mouseDown[0] && prevMouseDownButtons_[0]);
    if (!released || dragPayloadDelivered_ || type != dragPayloadType_ || !BeginDropTarget())
        return false;
    data = dragPayloadData_;
    dragPayloadDelivered_ = true;
    return true;
}

void Context::EndDropTarget() {}

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

} // namespace fluid
