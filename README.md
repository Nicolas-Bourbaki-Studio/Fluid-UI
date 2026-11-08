<div align="center" style={}>
    <img src="https://github.com/Nicolas-Bourbaki-Studio/Fluid-UI/blob/main/Github%20Assets/Images/FluidUI%20Brush.png?raw=true" alt="GifH">
</div>

<div align="center">
    <a href="#license"><img src="https://img.shields.io/github/license/Nicolas-Bourbaki-Studio/Fluid-UI"></a>
</div>

## Fluid UI is a lightweight, highly customizable UI library for C++ applications, designed to make building beautiful, native interfaces simple and intuitive.

## Components

`fluid::Context` includes buttons, checkboxes, toggles, radio buttons, float/value sliders,
dropdown menus, text and image labels, image buttons, collapsible containers, containers,
horizontal/vertical lists, tabbed containers, and class-styleable empty widgets.
Text/icon labels can be combined as needed.
Images accept caller-owned `GPUImageId` handles from any rendering API; Fluid UI does not
load, create, or destroy those image resources.

```cpp
fluid::Context ui;
ui.Style() = fluid::StyleSheet::Dark();

bool enabled = true;
float value = 0.5f;
int selected = 0;
const std::vector<std::string_view> options{"One", "Two", "Three"};

ui.Toggle("Enabled", &enabled);
ui.ValueSlider("Value", &value, 0.0f, 1.0f);
ui.Dropdown("Choice", options, &selected);
ui.Image(myGpuImageId, {128.0f, 128.0f});

if (ui.BeginContainer("Panel", {300.0f, 120.0f})) {
    ui.TextLabelLeft("Status", "*");
    ui.EndContainer();
}

ui.PushClass("brand");
ui.EmptyWidget("Brand preview", {360.0f, 120.0f});
ui.PopClass();
```

Call `EndContainer()` for each successful `BeginContainer`, `BeginList`,
`BeginTabContainer`, or `BeginCollapsible`. `BeginList(label, size, true)` creates a
horizontal list; `false` creates a vertical list. `BeginTabContainer` accepts a
`TabPosition::Top`, `Bottom`, or `Left` setting. A `TabItem` with no icon displays text;
one with an icon displays both by default, or just the icon when `showText` is false.

## Wrapper-friendly windows and menus

The window/layout subset of common immediate-mode wrappers is available without adding
dependencies on the wrapped application's types:

```cpp
if (ui.Begin("Inspector", &inspectorOpen)) {
    if (ui.BeginChild("Properties", {0.0f, -12.0f})) {
        ui.Text("The child fills the available width and height, leaving 12 px below.");
        ui.EndChild();
    }
    ui.End();
}
```

`Begin(title, open)` creates a centered default-size window; the optional `open` value is
updated when its close control is used. Pair a successful `Begin` with `End`. `BeginChild`
and `EndChild` are aliases for a clipped, padded container. A zero width fills the available
width; a non-positive height uses the remaining content height, minus the absolute value
when negative. The parameterless `SameLine()` places the next item alongside the previous
one.

The main menu bar is drawn at the top of Fluid UI's client area (below the host operating
system's native title bar). It reserves space from dock layouts and supports direct menu
items, nested menus, and popup items:

```cpp
if (ui.BeginMainMenuBar()) {
    if (ui.BeginMenu("File")) {
        if (ui.MenuItem("Open")) { /* open a document */ }
        if (ui.BeginMenu("Recent")) {
            if (ui.MenuItem("Project A")) { /* select a project */ }
            ui.EndMenu();
        }
        ui.EndMenu();
    }
    if (ui.MenuItem("Help")) { /* show help */ }
    ui.EndMainMenuBar();
}
```

Menu visuals can be themed with `main-menu-bar`, `menu`, `menu-popup`, and `menu-item`
CSS selectors. The GLFW demo includes File, Edit, View, Project, and Network menus,
a Settings window, and uses `src/Fonts/OpenSans.ttf` for its text atlas.
Floating windows can be resized from any edge or corner. Docking offers a 3x3 target
grid with a live pane preview; docked windows hide their close control and require a
double-click on the title bar to undock. The GLFW demo smooths rounded geometry with
multisampling and higher-resolution corner tessellation, and uses linear filtering for
images; multisampling is kept off for textured draws so it does not add antialiasing
to the text.

## CSS-like themes and classes

Styles can be configured in C++ with `StyleSheet::Rule` or loaded from a CSS-like file.
Selectors support widget names, multiple stacked classes, grouped selectors, and widget
states (`hover`, `active`, `checked`, `disabled`). Global custom properties can be declared
in `:root` and referenced with `var(--name)` (including a fallback). Colors support 3/4/6/8
digit hex, common named colors, `rgb()`/`rgba()`, `hsl()`/`hsla()`, and `transparent`;
numeric values may include `px`.
Supported properties are `background`, `color`/`foreground`, `accent`, `border-color`,
`shadow-color`, `border-width`, `border-radius` and its `border-radius-top-left`,
`border-radius-top-right`, `border-radius-bottom-left`, and `border-radius-bottom-right`
corner overrides, `font-size`, `gap`, `shadow-blur`,
`opacity`, `transition-speed`, the `border` shorthand, and CSS-style one-to-four-value
`padding`. Background images are supported with `background-image: url("...")`,
`background-size` (`stretch`, `cover`, or `contain`), and keyword/percentage
`background-position`. Image URLs resolve to caller-owned GPU resources registered with
`StyleSheet::RegisterImage`; Fluid UI does not load image files itself. Linear gradients
support `linear-gradient(...)` and `gradient(...)` syntax, including angles or
`to right`-style directions,
multiple color stops, and percentage stops. Gradients are accepted anywhere a color is
accepted (backgrounds, foreground/text, accent, borders, and shadows), with explicit
`text-gradient`, `accent-gradient`, `border-color-gradient`, and `shadow-color-gradient`
aliases available. Unsupported
declarations are ignored so themes can include properties not implemented by Fluid UI.
Unsupported selectors and invalid values for supported properties report an error, and
failed stylesheet loads do not partially modify the active theme.

```css
button.primary {
    background: #3559D8;
    border-radius: 8px;
    padding: 7px 14px;
}

button.primary:hover {
    background: #4A6CF7;
}

empty-widget.brand {
    background: linear-gradient(120deg, #101319, #1D2B42);
    background-image: url("../src/Images/Icon.jpg");
    background-size: contain;
    background-position: center;
}

empty-widget.gradient-demo {
    background: linear-gradient(110deg, #213A67 0%, #3978B8 52%, #8A5CE6 100%);
}

text.gradient-text {
    text-gradient: linear-gradient(90deg, #62D5FF, #A991FF, #FF86C8);
}

window-title {
    background: #1A2637;
    color: #F1F5FC;
    padding: 7px 12px;
}
```

```cpp
std::string error;
if (!ui.Style().LoadCSSFile("ui.css", &error)) {
    // Handle the reported file or syntax error.
}

ui.PushClass("primary");
ui.Button("Save");
ui.PopClass();

ui.Style().RegisterImage("../src/Images/Icon.jpg", myGpuImageId, {1024.0f, 1024.0f});
```

`EmptyWidget(id, size)` reserves and paints a class-styleable region without drawing
content. `ImageWidget(id, imageId, size)` draws a class-styleable framed image with CSS
padding, fit, position, background, border, and shadow styles. The GLFW demo loads
`src/Images/Icon.jpg`, registers it for its CSS background-image URL, and displays it in
both an empty widget and an image widget near the top of the component window. The registered
source must match the `url(...)` value in CSS. The demo also shows gradients on backgrounds,
text, borders, and accent colors.

The `examples` target includes `theme.css` and a GLFW demo covering the controls, container
layouts, all tab positions, image widgets, dropdown variants, CSS classes, and collapsible
content.

### Rendering API image IDs

Use a nonzero `fluid::GPUImageId` for a GPU image that your application loaded or created.
The image is sampled with the full `[0, 1]` UV range. `ImageLabel` and `ImageButton` accept
the same ID and an explicit display size. The UI never takes ownership of that resource.
`TextLabelLeft` and `TextLabelRight` also accept an image ID and display size for image
icons; the existing text-icon overloads remain available.

```cpp
// OpenGL texture name:
fluid::GPUImageId imageId = static_cast<fluid::GPUImageId>(glTexture);
ui.Image(imageId, {256.0f, 128.0f});
ui.ImageLabel("Preview", imageId, {64.0f, 64.0f});
if (ui.ImageButton("Use preview", imageId, {32.0f, 32.0f})) { /* ... */ }
```

The renderer consumes `DrawCmd::texture` and binds/resolves that ID in its own API. With
OpenGL, bind the texture name; with Vulkan or another descriptor-based API, use a stable
nonzero ID registered in your renderer and map it to the appropriate image view and sampler.
ID `0` is reserved for untextured geometry. Keep externally supplied images alive until the
draw list using them has been rendered.

## Windows and dynamic docking

Window titles are centered in the title bar; the right-side close control hides the window.
Click a window to focus and bring it above the other windows. Drag the bottom-right corner
to resize it. Closed windows can be restored with
`Context::SetWindowOpen("Window title", true)`. Long content scrolls vertically with the
mouse wheel or by dragging the scrollbar.

Hold a window's title bar to show the docking compass. Hover its left, right, top, bottom,
or center target to preview that layout, then release to dock. Docked windows tile without
overlapping; adding panels on other edges adjusts the available regions. Drag the boundary
splitters to resize edge regions horizontally or vertically, or drag dividers between
same-edge panels to resize individual windows. The layout adapts when the display changes
size. Use `Context::DockWindows(false)` to disable docking.
