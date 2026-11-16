#include <FluidUI/DrawList.h>
#include <FluidUI/Context.h>
#include <FluidUI/Style.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void Require(bool condition) {
    if (!condition) std::abort();
}

bool ContainsPosition(const fluid::DrawList& drawList, fluid::Vec2 expected) {
    for (const fluid::Vertex& vertex : drawList.Vertices()) {
        if (std::abs(vertex.pos.x - expected.x) < 0.001f &&
            std::abs(vertex.pos.y - expected.y) < 0.001f)
            return true;
    }
    return false;
}

class CaptureText final : public fluid::ITextBackend {
public:
    fluid::Vec2 Measure(std::string_view text, float fontSize) override {
        return {static_cast<float>(text.size()) * fontSize * 0.5f, fontSize};
    }

    void Emit(fluid::DrawList&, fluid::Vec2, std::string_view text,
              float, fluid::Color color) override {
        emitted.emplace_back(text);
        colors.push_back(color);
    }

    std::vector<std::string> emitted;
    std::vector<fluid::Color> colors;
};

} // namespace

int main() {
    fluid::StyleSheet styles;
    std::string error;
    const bool loaded = styles.LoadCSS(
        "window {"
        "background: #f02121;"
        "border: 2px solid linear-gradient(90deg, #ffffff 0%, #000000 100%);"
        "outline: 3px solid linear-gradient(180deg, #ff0000, #0000ff);"
        "color: linear-gradient(90deg, #ffffff, #000000);"
        "}"
        "button {"
        "background: linear-gradient(45deg, #000000, #ffffff);"
        "border: 2px solid #ffffff;"
        "outline: 1px solid #00ff00;"
        "}",
        &error);
    Require(loaded && error.empty());

    const fluid::ComputedStyle style = styles.Resolve("window", {}, fluid::State_None);
    Require(style.background.r > 0.9f && style.background.g < 0.2f);
    Require(style.borderWidth == 2.f && !style.borderColorGradient.Empty());
    Require(style.outlineEnabled && style.outlineWidth == 3.f &&
            !style.outlineColorGradient.Empty());
    Require(!style.textGradient.Empty());
    const fluid::ComputedStyle solidStyle =
        styles.Resolve("button", {}, fluid::State_None);
    Require(!solidStyle.backgroundGradient.Empty());
    Require(solidStyle.borderWidth == 2.f && solidStyle.borderColor.a == 1.f);
    Require(solidStyle.outlineEnabled && solidStyle.outlineWidth == 1.f &&
            solidStyle.outlineColor.g > 0.9f);

    fluid::StyleSheet css;
    Require(css.LoadCSS(
        "/* comments, braces { }, and semicolons; are safe */"
        "button { background: red; unknown-property: invalid; }"
        "button.primary { background: blue; width: 0px; height: 0%;"
        " opacity: 0; border-radius: 0; outline: 0; text-align: right;"
        " vertical-align: bottom; align: center; }"
        "selectable, tree-node, collapsing-header { border: 3px solid #abcdef;"
        " border-radius: 12px; border-radius-top-left: 2px; }"
        "#confirm { background: green; }"
        "button#confirm { background: yellow; }"
        "button.primary#confirm { background: PuRpLe; }"
        "button > text { background: black; }",
        &error));
    Require(error.empty());
    const fluid::ComputedStyle idAndClass =
        css.Resolve("button", {"primary"}, fluid::State_None, "confirm");
    Require(idAndClass.background.r > 0.4f && idAndClass.background.b > 0.4f &&
            idAndClass.background.g < 0.1f);
    Require(idAndClass.width == 0.f && idAndClass.height == 0.f &&
            idAndClass.heightPercent);
    Require(idAndClass.opacity == 0.f && idAndClass.cornerRadii.topLeft == 0.f);
    Require(!idAndClass.outlineEnabled);
    Require(idAndClass.textAlign == fluid::TextAlignment::End &&
            idAndClass.textVerticalAlign == fluid::VerticalAlignment::End);
    Require(idAndClass.justifySelf == fluid::HorizontalAlignment::Center &&
            idAndClass.alignSelf == fluid::VerticalAlignment::Center);
    const fluid::ComputedStyle idOnly =
        css.Resolve("button", {}, fluid::State_None, "confirm");
    Require(idOnly.background.r > 0.9f && idOnly.background.g > 0.9f &&
            idOnly.background.b < 0.1f);
    for (const char* widget : {"selectable", "tree-node", "collapsing-header"}) {
        const fluid::ComputedStyle rowStyle =
            css.Resolve(widget, {}, fluid::State_None);
        Require(rowStyle.borderWidth == 3.f &&
                rowStyle.borderColor.b > 0.9f &&
                rowStyle.cornerRadii.topLeft == 2.f &&
                rowStyle.cornerRadii.topRight == 12.f);
    }

    fluid::StyleSheet positionStyles;
    Require(positionStyles.LoadCSS(
        "button { position: fixed; top: 10%; right: 12px; left: auto; }"
        "button.relative { position: relative; left: -5px; bottom: 25%; }"
        "button.coordinates { position: (15px, 30%); rotation: 45deg;"
        " translate: translate(10px, 20%); transition: all 200ms ease; }",
        &error));
    const fluid::ComputedStyle fixedStyle =
        positionStyles.Resolve("button", {}, fluid::State_None);
    Require(fixedStyle.position == fluid::PositionType::Fixed &&
            fixedStyle.topSet && fixedStyle.topPercent &&
            fixedStyle.top == 0.1f && fixedStyle.rightSet &&
            fixedStyle.right == 12.f && !fixedStyle.leftSet);
    const fluid::ComputedStyle relativeStyle =
        positionStyles.Resolve("button", {"relative"}, fluid::State_None);
    Require(relativeStyle.position == fluid::PositionType::Relative &&
            relativeStyle.leftSet && relativeStyle.left == -5.f &&
            relativeStyle.bottomSet && relativeStyle.bottomPercent &&
            relativeStyle.bottom == 0.25f);
    const fluid::ComputedStyle coordinatesStyle =
        positionStyles.Resolve("button", {"coordinates"}, fluid::State_None);
    Require(coordinatesStyle.position == fluid::PositionType::Absolute &&
            coordinatesStyle.leftSet && coordinatesStyle.left == 15.f &&
            coordinatesStyle.topPercent && coordinatesStyle.top == 0.3f);
    Require(coordinatesStyle.rotation == 45.f &&
            coordinatesStyle.translate.x == 10.f &&
            coordinatesStyle.translate.y == 0.2f &&
            coordinatesStyle.translateYPercent &&
            std::abs(coordinatesStyle.transitionSpeed - 5.f) < 0.01f &&
            coordinatesStyle.transitionEasing == fluid::TransitionEasing::Ease);

    fluid::StyleSheet scrollbarStyles;
    Require(scrollbarStyles.LoadCSS(
        "window { scrollbar-width: none; }"
        "container { scrollbar-width: thin; }"
        "console-output { scrollbar-width: auto; }"
        "window-scrollbar { width: 11px; }"
        "container-scrollbar { width: 13px; }",
        &error));
    Require(scrollbarStyles.Resolve("window", {}, fluid::State_None).scrollbarWidth ==
            fluid::ScrollbarWidth::None);
    Require(scrollbarStyles.Resolve("container", {}, fluid::State_None).scrollbarWidth ==
            fluid::ScrollbarWidth::Thin);
    Require(scrollbarStyles.Resolve("console-output", {}, fluid::State_None).scrollbarWidth ==
            fluid::ScrollbarWidth::Auto);
    Require(scrollbarStyles.Resolve("window-scrollbar", {}, fluid::State_None).width ==
            11.f);
    Require(scrollbarStyles.Resolve("container-scrollbar", {}, fluid::State_None).width ==
            13.f);

    fluid::StyleSheet important;
    Require(important.LoadCSS(
        "button { color: blue !important; }"
        "button.primary { color: red !important; color: green; }",
        &error));
    const fluid::ComputedStyle importantStyle =
        important.Resolve("button", {"primary"}, fluid::State_None);
    Require(importantStyle.foreground.r > 0.9f && importantStyle.foreground.g < 0.1f);

    fluid::StyleSheet variableStyles;
    Require(variableStyles.LoadCSS(
        ":root { --accent-color: #123456; }"
        "button { background: var(--accent-color); }",
        &error));
    Require(variableStyles.LoadCSS(
        "button { background: var(--accent-color, #abcdef); }", &error));
    const fluid::ComputedStyle variableStyle =
        variableStyles.Resolve("button", {}, fluid::State_None);
    Require(variableStyle.background.b > variableStyle.background.r);

    fluid::Context context;
    CaptureText captureText;
    context.SetTextBackend(&captureText);
    Require(context.Style().LoadCSS("#ThemeTextTest { color: #ff0000; }", &error));
    fluid::InputState input;
    input.displaySize = {400.f, 300.f};
    context.NewFrame(input);
    context.Text("Active stylesheet##ThemeTextTest");
    context.EndFrame();
    Require(captureText.emitted.size() == 1);
    Require(captureText.emitted.front() == "Active stylesheet");
    Require(captureText.colors.front().r > 0.9f &&
            captureText.colors.front().g < 0.1f);

    fluid::Context separatorUi;
    CaptureText separatorText;
    separatorUi.SetTextBackend(&separatorText);
    Require(separatorUi.Style().LoadCSS(
        "separator-text { color: #ffffff; border-color: #888888;"
        " border-width: 2px; }", &error));
    separatorUi.NewFrame(input);
    separatorUi.HorizontalSeparator();
    separatorUi.HorizontalSeparatorText("Section");
    separatorUi.EndFrame();
    Require(separatorText.emitted.size() == 1 &&
            separatorText.emitted.front() == "Section");

    fluid::Context fullTextUi;
    CaptureText fullText;
    fullTextUi.SetTextBackend(&fullText);
    Require(fullTextUi.Style().LoadCSS(
        "button#full-text { width: 40px; padding: 0; }", &error));
    fullTextUi.NewFrame(input);
    fullTextUi.Button("Long label text##full-text");
    fullTextUi.EndFrame();
    Require(fullText.emitted.size() == 1 &&
            fullText.emitted.front() == "Long label text");

    fluid::Context rotationUi;
    Require(rotationUi.Style().LoadCSS(
        "container { padding: 0; scrollbar-width: none; }"
        "button#transform { width: 40px; height: 20px; padding: 0;"
        " border-radius: 0; shadow-enabled: false; background: red;"
        " translate: 10px 5px; rotation: 90deg; }",
        &error));
    rotationUi.NewFrame(input);
    Require(rotationUi.BeginContainer("Transform parent", {100.f, 80.f}));
    rotationUi.Button("Transform##transform");
    const fluid::Vec2 translatedPosition = rotationUi.GetItemRectMin();
    Require(std::abs(translatedPosition.x - 10.f) < 0.01f &&
            std::abs(translatedPosition.y - 5.f) < 0.01f);
    rotationUi.EndContainer();
    const fluid::DrawList& transformedDraw = rotationUi.EndFrame();
    Require(ContainsPosition(transformedDraw, {40.f, 35.f}));

    fluid::Context positionUi;
    Require(positionUi.Style().LoadCSS(
        "container { padding: 0; scrollbar-width: none; }"
        "button#fixed { position: fixed; left: 25%; top: 10%;"
        " width: 50%; height: 20px; }"
        "button#absolute { position: absolute; right: 10px; bottom: 5px;"
        " width: 30px; height: 10px; }"
        "button#coordinates { position: (15px, 30%); width: 10px; height: 10px; }"
        "window#positioned { position: fixed; top: 15px; right: 10px;"
        " width: 100px; height: 120px; padding: 0; translate: 5px 0; }",
        &error));
    fluid::InputState positionFrame;
    positionFrame.displaySize = {400.f, 300.f};
    positionUi.NewFrame(positionFrame);
    Require(positionUi.BeginContainer("Position parent", {400.f, 300.f}));
    positionUi.Button("Fixed##fixed");
    const fluid::Vec2 fixedPosition = positionUi.GetItemRectMin();
    Require(std::abs(fixedPosition.x - 100.f) < 0.01f &&
            std::abs(fixedPosition.y - 30.f) < 0.01f);
    positionUi.Button("Flow after fixed");
    const fluid::Vec2 flowPosition = positionUi.GetItemRectMin();
    Require(std::abs(flowPosition.x) < 0.01f &&
            std::abs(flowPosition.y) < 0.01f);
    positionUi.Button("Absolute##absolute");
    const fluid::Vec2 absolutePosition = positionUi.GetItemRectMin();
    Require(std::abs(absolutePosition.x - 360.f) < 0.01f &&
            std::abs(absolutePosition.y - 285.f) < 0.01f);
    positionUi.Button("Coordinates##coordinates");
    const fluid::Vec2 coordinatePosition = positionUi.GetItemRectMin();
    Require(std::abs(coordinatePosition.x - 15.f) < 0.01f &&
            std::abs(coordinatePosition.y - 90.f) < 0.01f);
    positionUi.EndContainer();
    positionUi.EndFrame();
    positionUi.NewFrame(positionFrame);
    Require(positionUi.BeginWindow("Fixed window##positioned",
                                   {20.f, 20.f, 220.f, 160.f}));
    positionUi.Text("Window content");
    const fluid::Vec2 windowPosition = positionUi.GetItemRectMin();
    Require(std::abs(windowPosition.x - 295.f) < 0.01f);
    positionUi.EndWindow();
    positionUi.EndFrame();

    fluid::Context windowUi;
    Require(windowUi.Style().LoadCSS(
        "window { padding: 0; } window-scrollbar { width: 15px; }", &error));
    auto measureWindowContentWidth = [&](fluid::WindowFlags flags) {
        fluid::InputState frame;
        frame.displaySize = {400.f, 300.f};
        windowUi.NewFrame(frame);
        Require(windowUi.BeginWindow("Scrollable##window-id",
                                     {20.f, 20.f, 200.f, 100.f}, flags));
        const float width = windowUi.GetContentRegionAvail().x;
        for (int i = 0; i < 12; ++i) windowUi.Text("Overflow line");
        windowUi.EndWindow();
        windowUi.EndFrame();
        return width;
    };
    measureWindowContentWidth(fluid::WindowFlags::None);
    const float windowWithScrollbar =
        measureWindowContentWidth(fluid::WindowFlags::None);
    const float windowWithoutScrollbar =
        measureWindowContentWidth(fluid::WindowFlags::NoScrollbar);
    Require(std::abs(windowWithoutScrollbar - windowWithScrollbar - 20.f) < 0.01f);
    Require(windowWithoutScrollbar > windowWithScrollbar + 6.f);

    fluid::Context containerUi;
    Require(containerUi.Style().LoadCSS(
        "container { padding: 0; } container-scrollbar { width: 14px; }", &error));
    auto measureContainerContentWidth = [&]() {
        fluid::InputState frame;
        frame.displaySize = {400.f, 300.f};
        containerUi.NewFrame(frame);
        Require(containerUi.BeginContainer("Scrollable##container-id", {100.f, 80.f}));
        const float width = containerUi.GetContentRegionAvail().x;
        for (int i = 0; i < 12; ++i) containerUi.Text("Overflow line");
        containerUi.EndContainer();
        containerUi.EndFrame();
        return width;
    };
    measureContainerContentWidth();
    const float containerWithScrollbar = measureContainerContentWidth();
    Require(std::abs(containerWithScrollbar - 86.f) < 0.01f);
    Require(containerUi.Style().LoadCSS(
        "container { padding: 0; scrollbar-width: none; }", &error));
    const float containerWithoutScrollbar = measureContainerContentWidth();
    Require(containerWithoutScrollbar > containerWithScrollbar + 6.f);

    fluid::StyleSheet reload;
    Require(reload.LoadCSS("button { background: red; color: white; }", &error));
    Require(reload.Resolve("button", {}, fluid::State_None).background.r > 0.9f);
    Require(reload.LoadCSS("button { color: black; }", &error));
    const fluid::ComputedStyle afterRemoval =
        reload.Resolve("button", {}, fluid::State_None);
    Require(afterRemoval.background.r < 0.5f);
    Require(reload.LoadCSS("button { background: none; }", &error));
    const fluid::ComputedStyle transparent =
        reload.Resolve("button", {}, fluid::State_None);
    Require(transparent.background.a == 0.f && transparent.backgroundGradient.Empty());
    Require(!reload.LoadCSS("button { background: blue; /* unterminated",
                            &error));
    Require(reload.Resolve("button", {}, fluid::State_None).background.a == 0.f);

    fluid::StyleSheet emptyReload;
    Require(emptyReload.LoadCSS("button { background: red; }", &error));
    Require(emptyReload.Resolve("button", {}, fluid::State_None).background.r > 0.9f);
    Require(emptyReload.LoadCSS("button { unknown-property: invalid; }", &error));
    Require(emptyReload.Resolve("button", {}, fluid::State_None).background.r < 0.5f);

    fluid::StyleSheet theme;
    Require(theme.LoadCSSFile(std::string("examples/theme.css"), &error));
    Require(error.empty());

    fluid::DrawList drawList;
    drawList.AddRectFilled({0.f, 0.f, 20.f, 20.f}, {1.f, 0.f, 0.f, 1.f});
    const size_t backgroundVertices = drawList.Vertices().size();
    drawList.AddBorder({0.f, 0.f, 20.f, 20.f}, {0.f, 1.f, 0.f, 1.f}, 2.f, 0.f);
    Require(drawList.Vertices().size() > backgroundVertices);
    Require((drawList.Vertices().front().col ==
             fluid::Color{1.f, 0.f, 0.f, 1.f}.ToU32()));
    for (size_t i = backgroundVertices; i < drawList.Vertices().size(); ++i) {
        const fluid::Vec2 position = drawList.Vertices()[i].pos;
        Require(position.x <= 2.f || position.x >= 18.f ||
                position.y <= 2.f || position.y >= 18.f);
    }

    fluid::DrawList roundedBorder;
    roundedBorder.AddBorder({0.f, 0.f, 100.f, 100.f}, {0.f, 1.f, 0.f, 1.f},
                            2.f, fluid::CornerRadii{8.f});
    Require(ContainsPosition(roundedBorder, {92.f, 0.f}));
    Require(ContainsPosition(roundedBorder, {92.f, 2.f}));

    fluid::DrawList roundedGradientBorder;
    roundedGradientBorder.AddBorderGradient(
        {0.f, 0.f, 100.f, 100.f}, style.outlineColorGradient, 1.f, 2.f,
        fluid::CornerRadii{10.f});
    Require(ContainsPosition(roundedGradientBorder, {90.f, 0.f}));
    Require(ContainsPosition(roundedGradientBorder, {90.f, 2.f}));
}
