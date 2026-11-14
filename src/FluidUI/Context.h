#pragma once
#include "Backend.h"
#include "DrawList.h"
#include "Export.h"
#include "Style.h"

#include <array>
#include <functional>
#include <optional>
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
    bool LoadLayout(const std::string& path, std::string& error);
    bool SaveLayout(const std::string& path, std::string& error) const;
    bool BeginChild(std::string_view id, Vec2 size = {});
    void EndChild();
    bool BeginContainer(std::string_view label, Vec2 size);
    bool BeginList(std::string_view label, Vec2 size, bool horizontal);
    void EndContainer();
    void SetNextItemWidth(float width);
    Vec2 GetContentRegionAvail() const;
    void PushWindowPadding(Vec2 padding);
    void PopWindowPadding();
    void BeginDisabled(bool disabled = true);
    void EndDisabled();
    void BeginGroup();
    void EndGroup();
    bool BeginPropertyGrid(std::string_view id);
    void EndPropertyGrid();
    void PropertyRow(std::string_view label);
    void ScrollToBottom();
    void ClippedList(int count, const std::function<void(int)>& drawItem,
                     float itemHeight = 0.f);

    bool BeginMainMenuBar();
    void EndMainMenuBar();
    bool BeginMenu(std::string_view label);
    void EndMenu();
    bool MenuItem(std::string_view label);
    bool MenuItem(std::string_view label, std::string_view shortcut,
                  bool selected, bool enabled);
    bool OpenPopup(std::string_view id);
    bool BeginPopup(std::string_view id);
    bool BeginPopupModal(std::string_view title);
    bool BeginPopupContextItem(std::string_view id = {});
    bool BeginPopupContextWindow(std::string_view id = {});
    void CloseCurrentPopup();
    void EndPopup();
    bool BeginDragSource();
    void SetDragPayload(std::string_view type, std::string_view data);
    void EndDragSource();
    bool BeginDropTarget();
    bool AcceptDrop(std::string_view type, std::string& data);
    void EndDropTarget();

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
    void TextWrapped(std::string_view text);
    void TextDisabled(std::string_view text);
    void TextLabel(std::string_view text);
    void TextLabelLeft(std::string_view text, std::string_view icon);
    void TextLabelRight(std::string_view text, std::string_view icon);
    void TextLabelLeft(std::string_view text, GPUImageId icon, Vec2 iconSize);
    void TextLabelRight(std::string_view text, GPUImageId icon, Vec2 iconSize);
    bool Button(std::string_view label, Vec2 size = {});
    bool ImageButton(std::string_view label, GPUImageId imageId, Vec2 imageSize);
    bool Checkbox(std::string_view label, bool* value);
    bool Toggle(std::string_view label, bool* value);
    bool Radio(std::string_view label, int* value, int option);
    bool SliderFloat(std::string_view label, float* value, float minV, float maxV);
    bool ValueSlider(std::string_view label, float* value, float minV, float maxV);
    bool DragFloat(std::string_view label, float* value, float speed,
                   float minV, float maxV);
    bool DragInt(std::string_view label, int* value, float speed, int minV, int maxV);
    bool ColorEdit3(std::string_view label, float* value);
    bool ColorEdit4(std::string_view label, float* value);
    bool InputText(std::string_view label, std::string* value, bool multiline = false,
                   bool submitOnEnter = false, bool* submitted = nullptr);
    void SetKeyboardFocusHere();
    bool Selectable(std::string_view label, bool selected = false);
    bool TreeNode(std::string_view label, uint32_t flags = 0);
    void TreePop();
    bool CollapsingHeader(std::string_view label, bool defaultOpen = true);
    bool Dropdown(std::string_view label, const std::vector<std::string_view>& items,
                  int* selected, std::string_view icon = {}, bool showText = true);
    void ImageLabel(std::string_view label, GPUImageId imageId, Vec2 imageSize);
    bool BeginCollapsible(std::string_view label, bool* open, Vec2 size = {0.f, 120.f});
    void Image(GPUImageId imageId, Vec2 size, bool flipY = false);
    void InteractiveImage(GPUImageId imageId, Vec2 size, bool flipY = false);
    bool ImageTile(std::string_view id, GPUImageId imageId, Vec2 size,
                   bool selected, bool flipY, std::string_view fallbackLabel,
                   Color fallbackColor);
    void ImageWidget(std::string_view id, GPUImageId imageId, Vec2 size);
    void EmptyWidget(std::string_view id, Vec2 size);
    bool IsItemHovered() const;
    bool IsItemClicked(int mouseButton = 0) const;
    bool IsItemDoubleClicked() const;
    bool IsItemActivated() const { return lastItemActivated_; }
    bool IsItemDeactivated() const { return lastItemDeactivated_; }
    bool IsAnyItemActive() const;
    bool IsAnyItemHovered() const { return anyItemHovered_; }
    bool IsWindowHovered() const;
    bool IsWindowFocused() const;
    Vec2 GetItemRectMin() const { return {lastItemRect_.x, lastItemRect_.y}; }
    void ItemTooltip(std::string_view text);
    void DrawLine(Vec2 a, Vec2 b, Color color, float thickness = 1.f);
    void DrawPolyline(const std::vector<Vec2>& points, Color color, float thickness = 1.f);
    void DrawTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color);
    void DrawRectFilled(Vec2 min, Vec2 max, Color color);
    void DrawLabel(Vec2 position, Color color, std::string_view text);

    // Layout
    void SameLine();
    void Spacing(float pixels);
    void Separator();
    void DockWindows(bool enabled) { dockingEnabled_ = enabled; }

    // Styling / IDs
    void PushClass(std::string_view cls);
    void PopClass();
    void PushID(std::string_view id);
    void PopID();
    bool WantsMouse() const { return wantsMouse_; }
    bool WantsMouseAt(Vec2 position) const;
    bool WantsKeyboard() const { return wantsKeyboard_; }

private:
    struct Interaction {
        bool hovered = false, held = false, pressed = false;
        bool clicked = false, doubleClicked = false;
    };
    enum class DockSide : uint8_t {
        None, Left, Right, Top, Bottom, Center,
        TopLeft, TopRight, BottomLeft, BottomRight
    };
    struct WindowState {
        Vec2 pos, size, dragOff, resizeStart, startSize, resizeStartPos;
        Vec2 dockRestoreSize, lastTitleClickPos;
        float scrollY = 0.f, contentHeight = 0.f, scrollDragOffset = 0.f, dockSpan = 1.f;
        double lastTitleClickTime = -1.0;
        uint64_t zOrder = 0;
        DrawList::Layer drawLayer;
        DockSide dock = DockSide::None;
        uint8_t resizeEdges = 0;
        bool init = false, open = true, hasDrawLayer = false;
        bool dragging = false, resizing = false, scrolling = false;
    };
    struct Anim { float hover = 0.f, active = 0.f; };
    struct LayoutState {
        Rect content;
        float cursorY = 0.f, lineTop = 0.f, lineBottom = 0.f, lastRight = 0.f;
        float cursorX = 0.f;
        bool sameLine = false, horizontal = false;
    };
    struct GroupState {
        LayoutState outer;
        Rect bounds;
        bool hasBounds = false;
    };
    struct ContainerState {
        LayoutState outer;
        Rect frame;
        WidgetId id = 0;
        float contentStartY = 0.f;
        float scrollY = 0.f;
        float contentHeight = 0.f;
        bool scrollable = false;
    };
    struct DockSplitter {
        Rect rect;
        WidgetId id = 0;
    };
    struct MenuState {
        WidgetId id = 0;
        Rect popup;
        Rect parentPopup;
        float cursorY = 0.f;
        float contentWidth = 0.f;
        bool hasParentPopup = false;
        bool popupMenu = false;
    };
    struct TextInputState {
        size_t cursor = 0;
        size_t selectionAnchor = 0;
        float scrollX = 0.f;
        bool initialized = false;
    };
    struct NumericInputState {
        std::string text;
        bool focus = true;
    };
    struct TreeNodeLayout {
        float oldContentX = 0.f;
        float oldContentWidth = 0.f;
        bool indented = false;
    };
    struct PopupState {
        Vec2 pos;
        Vec2 size{280.f, 220.f};
        Rect anchor, frame, clip;
        WidgetId ownerWindowId = 0;
        WidgetId parentPopupId = 0;
        DrawList::Layer layer;
        bool hasLayer = false;
        bool open = false;
        bool modal = false;
    };
    struct PopupStackEntry {
        LayoutState outer;
        DrawList::Layer layer;
        WidgetId id = 0;
        std::vector<Rect> restoreClips;
    };

    WidgetId GetID(std::string_view label) const;
    ComputedStyle Resolve(std::string_view widget, uint8_t state) const;
    ComputedStyle Animate(WidgetId id, std::string_view widget, uint8_t extraState,
                          bool hovered, bool held);
    Interaction Behavior(WidgetId id, Rect r);
    bool IsFloatingWindowAt(Vec2 position) const;
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
                            std::string_view widget = "container",
                            bool scrollable = false);
    bool BeginPopupImpl(std::string_view id, bool modal);
    void EndPopupImpl();
    void ClosePopupAndChildren(WidgetId id);
    void SetLastItem(WidgetId id, Rect rect, const Interaction& interaction);
    bool IsDisabled() const;
    Rect BeginMenuPopup(WidgetId id, Rect anchor, size_t depth);
    void DrawTextCentered(Rect r, std::string_view text, const ComputedStyle& s);

    StyleSheet style_;
    DrawList draw_;
    BasicTextBackend defaultText_;
    ITextBackend* text_;

    InputState in_;
    bool prevMouseDown_ = false;
    std::array<bool, 3> prevMouseDownButtons_{};
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
    WidgetId hoveredItem_ = 0, lastItemId_ = 0;
    Rect lastItemRect_;
    bool lastItemHovered_ = false, lastItemClicked_ = false;
    bool lastItemDoubleClicked_ = false, lastItemActivated_ = false;
    bool lastItemDeactivated_ = false, anyItemHovered_ = false;
    bool windowHovered_ = false, wantsMouse_ = false, wantsKeyboard_ = false;
    bool focusNextTextInput_ = false;
    bool focusNextTextInputSelectAll_ = false;
    WidgetId focusedTextInput_ = 0;
    WidgetId deactivatedTextInput_ = 0;
    std::optional<float> nextItemWidth_;
    std::vector<Edges> windowPaddingStack_;
    std::vector<bool> disabledStack_;
    std::vector<float> propertyGridWidths_;
    std::unordered_map<WidgetId, TextInputState> textInputs_;
    std::unordered_map<WidgetId, NumericInputState> numericInputs_;
    std::unordered_map<WidgetId, bool> treeNodeOpen_;
    std::unordered_map<WidgetId, double> lastItemClickTime_;
    std::unordered_map<WidgetId, PopupState> popups_;
    std::vector<WidgetId> openPopups_;
    std::vector<PopupStackEntry> popupStack_;
    std::string dragPayloadType_, dragPayloadData_;
    std::string dragPreviewText_;
    WidgetId dragSourceId_ = 0;
    Vec2 dragStartPosition_;
    bool draggingPayload_ = false, dragPayloadDelivered_ = false;
    bool dragSourceScope_ = false;
    bool scrollToBottom_ = false;

    std::vector<WidgetId> idStack_;
    std::vector<std::string> classStack_;
    std::vector<Rect> interactionClips_;
    std::unordered_map<WidgetId, WindowState> windows_;
    std::unordered_map<WidgetId, float> containerScroll_;
    std::unordered_map<WidgetId, float> containerScrolling_;
    std::unordered_map<WidgetId, Rect> containerFrameCache_;
    std::unordered_map<WidgetId, Rect> previousContainerFrameCache_;
    std::array<std::vector<WidgetId>, 10> dockedWindows_;
    WidgetId focusedWindow_ = 0;
    uint64_t nextWindowZ_ = 0;
    std::unordered_map<WidgetId, Anim> anims_;
    std::vector<ContainerState> containerStack_;
    std::vector<GroupState> groupStack_;
    std::vector<TreeNodeLayout> treeNodeStack_;
    std::vector<MenuState> menuStack_;
    std::vector<WidgetId> openMenus_;
    std::vector<Rect> menuPopupRects_;
    std::vector<std::string> pendingTooltips_;
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
    bool hasDockPreview_ = false;
    Rect dockCompassRect_;
    DockSide dockCompassSelection_ = DockSide::None;
    bool dockCompassVisible_ = false;
    float cursorY_ = 0.f, lineTop_ = 0.f, lineBottom_ = 0.f, lastRight_ = 0.f, cursorX_ = 0.f;
    bool sameLine_ = false;
};

} // namespace fluid