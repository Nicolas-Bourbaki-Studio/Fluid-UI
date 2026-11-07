#pragma once
#include "Backend.h"
#include "DrawList.h"
#include "Export.h"
#include "Style.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fluid {

class FLUID_API Context {
public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    StyleSheet& Style() { return style_; }
    void SetTextBackend(ITextBackend* backend); // not owned; nullptr restores the fallback

    // Frame
    void NewFrame(const InputState& input);
    const DrawList& EndFrame();

    // Containers
    bool BeginWindow(std::string_view title, Rect initial);
    void EndWindow();

    // Widgets. "Label##suffix" shows "Label" but hashes the full string for the ID.
    void Text(std::string_view text);
    bool Button(std::string_view label);
    bool Checkbox(std::string_view label, bool* value);
    bool SliderFloat(std::string_view label, float* value, float minV, float maxV);

    // Layout
    void SameLine();
    void Spacing(float pixels);

    // Styling / IDs
    void PushClass(std::string_view cls);
    void PopClass();
    void PushID(std::string_view id);
    void PopID();

private:
    struct Interaction { bool hovered = false, held = false, pressed = false; };
    struct WindowState { Vec2 pos, size, dragOff; bool init = false, dragging = false; };
    struct Anim { float hover = 0.f, active = 0.f; };

    WidgetId GetID(std::string_view label) const;
    ComputedStyle Resolve(std::string_view widget, uint8_t state) const;
    ComputedStyle Animate(WidgetId id, std::string_view widget, uint8_t extraState,
                          bool hovered, bool held);
    Interaction Behavior(WidgetId id, Rect r);
    Rect Place(Vec2 size, float spacing);
    void DrawBox(Rect r, const ComputedStyle& s);

    StyleSheet style_;
    DrawList draw_;
    BasicTextBackend defaultText_;
    ITextBackend* text_;

    InputState in_;
    bool prevMouseDown_ = false;
    WidgetId active_ = 0;

    std::vector<WidgetId> idStack_;
    std::vector<std::string> classStack_;
    std::unordered_map<WidgetId, WindowState> windows_;
    std::unordered_map<WidgetId, Anim> anims_;

    // Layout cursor (current window)
    Rect content_;
    float cursorY_ = 0.f, lineTop_ = 0.f, lineBottom_ = 0.f, lastRight_ = 0.f;
    bool sameLine_ = false;
};

} // namespace fluid