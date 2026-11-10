#pragma once
#include "Backend.h"
#include "DrawList.h"
#include "Export.h"
#include "Style.h"

#include <array>
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
    CursorShape Cursor() const { return cursorShape_; }

    // Frame
    void NewFrame(const InputState& input);
    const DrawList& EndFrame();

    // Containers
    bool Begin(std::string_view title, bool* open = nullptr);
    void End();
    bool BeginWindow(std::string_view title, Rect initial);
    void EndWindow();
    void SetWindowOpen(std::string_view title, bool open);
    bool SaveLayout(const std::string& path, std::string* error = nullptr) const;
    bool LoadLayout(const std::string& path, std::string* error = nullptr);
    bool BeginChild(std::string_view id, Vec2 size = {});
    void EndChild();
    bool BeginContainer(std::string_view label, Vec2 size);
    bool BeginList(std::string_view label, Vec2 size, bool horizontal);
    void EndContainer();

    bool BeginMainMenuBar();
    void EndMainMenuBar();
    bool BeginMenu(std::string_view label);
    void EndMenu();
    bool MenuItem(std::string_view label);

    enum class TabPosition : uint8_t { Top, Bottom, Left };
    struct TabItem {
        std::string_view label;
        std::string_view icon;
        bool showText = true;
    };
    bool BeginTabContainer(std::string_view label, Vec2 size,
                           const std::vector<TabItem>& tabs, int* selected,
                           TabPosition position = TabPosition::Top);

    // Widgets. "Label##suffix" shows "Label" but hashes the full string for the ID.
    void Text(std::string_view text);
    void TextLabel(std::string_view text);
    void TextLabelLeft(std::string_view text, std::string_view icon);
    void TextLabelRight(std::string_view text, std::string_view icon);
    void TextLabelLeft(std::string_view text, GPUImageId icon, Vec2 iconSize);
    void TextLabelRight(std::string_view text, GPUImageId icon, Vec2 iconSize);
    bool Button(std::string_view label);
    bool ImageButton(std::string_view label, GPUImageId imageId, Vec2 imageSize);
    bool Checkbox(std::string_view label, bool* value);
    bool Toggle(std::string_view label, bool* value);
    bool Radio(std::string_view label, int* value, int option);
    bool SliderFloat(std::string_view label, float* value, float minV, float maxV);
    bool ValueSlider(std::string_view label, float* value, float minV, float maxV);
    bool Dropdown(std::string_view label, const std::vector<std::string_view>& items,
                  int* selected, std::string_view icon = {}, bool showText = true);
    void ImageLabel(std::string_view label, GPUImageId imageId, Vec2 imageSize);
    bool BeginCollapsible(std::string_view label, bool* open, Vec2 size = {0.f, 120.f});
    void Image(GPUImageId imageId, Vec2 size);
    void ImageWidget(std::string_view id, GPUImageId imageId, Vec2 size);
    void EmptyWidget(std::string_view id, Vec2 size);

    // Layout
    void SameLine();
    void Spacing(float pixels);
    void DockWindows(bool enabled) { dockingEnabled_ = enabled; }

    // Styling / IDs
    void PushClass(std::string_view cls);
    void PopClass();
    void PushID(std::string_view id);
    void PopID();

private:
    struct Interaction { bool hovered = false, held = false, pressed = false; };
    enum class DockSide : uint8_t {
        None, Left, Right, Top, Bottom, Center,
        TopLeft, TopRight, BottomLeft, BottomRight
    };
    struct WindowState {
        std::string title;
        Vec2 pos, size, dragOff, dockDragStartPos, resizeStart, startSize, resizeStartPos;
        Vec2 dockRestoreSize, lastTitleClickPos;
        float scrollY = 0.f, contentHeight = 0.f, scrollDragOffset = 0.f, dockSpan = 1.f;
        WidgetId tabGroup = 0;
        double lastTitleClickTime = -1.0;
        uint64_t zOrder = 0;
        DrawList::Layer drawLayer;
        DockSide dock = DockSide::None;
        uint8_t resizeEdges = 0;
        bool init = false, open = true, hasDrawLayer = false;
        bool dragging = false, dockDragPending = false, resizing = false, scrolling = false;
    };
    struct Anim { float hover = 0.f, active = 0.f; };
    struct LayoutState {
        Rect content;
        float cursorY = 0.f, lineTop = 0.f, lineBottom = 0.f, lastRight = 0.f;
        float cursorX = 0.f;
        bool sameLine = false, horizontal = false;
    };
    struct ContainerState { LayoutState outer; };
    struct DockSplitter {
        Rect rect;
        WidgetId id = 0;
    };
    struct TabGroup {
        std::vector<WidgetId> windows;
        WidgetId active = 0;
    };
    struct MenuState {
        WidgetId id = 0;
        Rect popup;
        Rect parentPopup;
        float cursorY = 0.f;
        float contentWidth = 0.f;
        bool hasParentPopup = false;
    };

    WidgetId GetID(std::string_view label) const;
    ComputedStyle Resolve(std::string_view widget, uint8_t state) const;
    ComputedStyle Animate(WidgetId id, std::string_view widget, uint8_t extraState,
                          bool hovered, bool held);
    Interaction Behavior(WidgetId id, Rect r);
    bool IsFloatingWindowAt(Vec2 position) const;
    WidgetId WindowAt(Vec2 position, WidgetId exclude) const;
    void DetachTab(WidgetId id);
    void JoinTab(WidgetId id, WidgetId target);
    Rect Place(Vec2 size, float spacing);
    void DrawStyledColor(Rect r, Color color, const LinearGradient& gradient,
                         float opacity, CornerRadii rounding = {});
    void DrawBox(Rect r, const ComputedStyle& s);
    void DrawBackgroundImage(Rect r, const ComputedStyle& s);
    void EmitText(Vec2 pos, std::string_view text, const ComputedStyle& style);
    Interaction DrawMenuButton(Rect rect, std::string_view label, WidgetId id,
                               std::string_view widget, bool selected = false);
    std::array<Rect, 10> DockRegions(DockSide extraSide = DockSide::None) const;
    Rect DockTargetRect(DockSide side) const;
    DockSide DockTargetAt(Vec2 position) const;
    void SetDockPreview(DockSide side, WidgetId newWindow = 0);
    void LayoutDockedWindows();
    bool BeginContainerImpl(Vec2 size, bool horizontal,
                            std::string_view widget = "container");
    Rect BeginMenuPopup(WidgetId id, Rect anchor, size_t depth);
    void DrawTextCentered(Rect r, std::string_view text, const ComputedStyle& s);

    StyleSheet style_;
    DrawList draw_;
    BasicTextBackend defaultText_;
    ITextBackend* text_;

    InputState in_;
    bool prevMouseDown_ = false;
    WidgetId active_ = 0;
    WidgetId currentWindowId_ = 0;
    WidgetId activeDockSplitter_ = 0;
    float timeSeconds_ = 0.f;
    CursorShape cursorShape_ = CursorShape::Arrow;
    float dockSplitterMouseStart_ = 0.f, dockSplitterSpanStart_ = 0.f;
    float dockSplitterPairSpan_ = 0.f;
    float dockBoundaryStart_ = 0.f;
    float dockLeftRatio_ = 0.30f, dockRightRatio_ = 0.30f;
    float dockTopRatio_ = 0.30f, dockBottomRatio_ = 0.30f;
    bool dockLayoutDone_ = false;

    std::vector<WidgetId> idStack_;
    std::vector<std::string> classStack_;
    std::vector<Rect> interactionClips_;
    std::unordered_map<WidgetId, WindowState> windows_;
    std::array<std::vector<WidgetId>, 10> dockedWindows_;
    std::unordered_map<WidgetId, TabGroup> tabGroups_;
    WidgetId activeCenterTab_ = 0;
    WidgetId focusedWindow_ = 0;
    uint64_t nextWindowZ_ = 0;
    std::unordered_map<WidgetId, Anim> anims_;
    std::vector<ContainerState> containerStack_;
    std::vector<MenuState> menuStack_;
    std::vector<WidgetId> openMenus_;
    std::vector<Rect> menuPopupRects_;
    std::unordered_map<WidgetId, Vec2> menuPopupSizes_;
    std::vector<DockSplitter> dockSplitters_;
    std::unordered_map<WidgetId, bool> dropdowns_;
    bool horizontalLayout_ = false;
    bool dockingEnabled_ = true;
    bool windowInputEnabled_ = true;
    bool menuBarActive_ = false;
    bool menuBarLayerActive_ = false;
    DrawList::Layer menuBarLayer_;
    float menuBarHeight_ = 0.f;
    float menuBarCursorX_ = 0.f;

    // Layout cursor (current window)
    Rect content_;
    Rect windowViewport_;
    Rect dockPreview_;
    Rect tabDropPreview_;
    WidgetId tabDropTarget_ = 0;
    bool hasDockPreview_ = false;
    Rect dockCompassRect_;
    DockSide dockCompassSelection_ = DockSide::None;
    bool dockCompassVisible_ = false;
    float cursorY_ = 0.f, lineTop_ = 0.f, lineBottom_ = 0.f, lastRight_ = 0.f, cursorX_ = 0.f;
    bool sameLine_ = false;
};

} // namespace fluid