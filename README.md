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
horizontal/vertical lists, and tabbed containers. Text/icon labels can be combined as needed.
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
```

Call `EndContainer()` for each successful `BeginContainer`, `BeginList`,
`BeginTabContainer`, or `BeginCollapsible`. `BeginList(label, size, true)` creates a
horizontal list; `false` creates a vertical list. `BeginTabContainer` accepts a
`TabPosition::Top`, `Bottom`, or `Left` setting. A `TabItem` with no icon displays text;
one with an icon displays both by default, or just the icon when `showText` is false.

## CSS-like themes and classes

Styles can be configured in C++ with `StyleSheet::Rule` or loaded from a CSS-like file.
Selectors support widget names, multiple stacked classes, grouped selectors, and widget
states (`hover`, `active`, `checked`, `disabled`). Global custom properties can be declared
in `:root` and referenced with `var(--name)` (including a fallback). Colors support 3/4/6/8
digit hex, common named colors, `rgb()` and `rgba()`, and the `transparent` keyword;
numeric values may include `px`.
Supported properties are `background`, `color`/`foreground`, `accent`, `border-color`,
`shadow-color`, `border-width`, `border-radius`, `font-size`, `gap`, `shadow-blur`,
`opacity`, `transition-speed`, the `border` shorthand, and CSS-style one-to-four-value
`padding`. Unsupported selectors and declarations report an error, and failed stylesheet
loads do not partially modify the active theme.

```css
button.primary {
    background: #3559D8;
    border-radius: 8px;
    padding: 7px 14px;
}

button.primary:hover {
    background: #4A6CF7;
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
```

The `examples` target includes `theme.css` and a GLFW demo covering the controls, container
layouts, all tab positions, image widgets, dropdown variants, CSS classes, and collapsible
content.

### Rendering API image IDs

Use a nonzero `fluid::GPUImageId` for a GPU image that your application loaded or created.
The image is sampled with the full `[0, 1]` UV range. `ImageLabel` and `ImageButton` accept
the same ID and an explicit display size. The UI never takes ownership of that resource.

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
