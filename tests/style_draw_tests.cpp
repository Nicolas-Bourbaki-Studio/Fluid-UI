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
