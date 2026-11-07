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
Images are caller-owned backend texture IDs rendered through `DrawList`; the UI does not
load or manage image files.

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

## Windows and dynamic docking

Window titles are centered in the title bar; the right-side controls minimize and close the
window. Drag the bottom-right corner to resize it. Closed windows can be restored with
`Context::SetWindowOpen("Window title", true)`. Long content scrolls vertically with the
mouse wheel or by dragging the scrollbar.

Drag a window by its title bar toward a screen edge to see a live docking preview. Dropping within 32 pixels of an edge docks the window into that edge's region. Windows
docked to the same edge tile together; drag the dividers between them to resize adjacent
panels, and the group recalculates as the display changes size. Use
`Context::DockWindows(false)` to disable edge snapping.
