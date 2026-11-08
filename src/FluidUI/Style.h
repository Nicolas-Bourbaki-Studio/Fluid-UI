#pragma once
#include "Export.h"
#include "Types.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fluid {

// Every stylable property. Add new ones here + in ComputedStyle + Apply() in Style.cpp.
enum class Prop : uint8_t {
    Background, BackgroundReset, Foreground, Accent, BorderColor, ShadowColor,
    BorderWidth, Rounding, FontSize, Spacing, ShadowBlur, Opacity,
    Padding, TransitionSpeed, BackgroundImage, BackgroundSize, BackgroundPosition,
    BackgroundGradient, TextGradient, ForegroundGradient, AccentGradient,
    BorderColorGradient, ShadowColorGradient, RoundingTopLeft, RoundingTopRight,
    RoundingBottomRight, RoundingBottomLeft
};

enum class BackgroundFit : uint8_t { Stretch, Cover, Contain };

enum StateFlags : uint8_t {
    State_None     = 0,
    State_Hovered  = 1 << 0,
    State_Active   = 1 << 1,
    State_Checked  = 1 << 2,
    State_Disabled = 1 << 3,
};

// Fully resolved style used by widgets at draw time.
struct ComputedStyle {
    Color background  {0.18f, 0.18f, 0.22f, 1.f};
    Color foreground  {0.90f, 0.90f, 0.92f, 1.f};
    Color accent      {0.29f, 0.42f, 0.97f, 1.f};
    Color borderColor {0.f, 0.f, 0.f, 0.f};
    Color shadowColor {0.f, 0.f, 0.f, 0.f};
    float borderWidth = 0.f;
    float rounding    = 4.f;
    CornerRadii cornerRadii{4.f};
    float fontSize    = 16.f;
    float spacing     = 6.f;
    float shadowBlur  = 0.f;
    float opacity     = 1.f;
    float transitionSpeed = 14.f; // higher = snappier
    Edges padding {4.f, 8.f, 4.f, 8.f};
    GPUImageId backgroundImage = 0;
    Vec2 backgroundImageSize;
    BackgroundFit backgroundFit = BackgroundFit::Stretch;
    Vec2 backgroundPosition{0.5f, 0.5f};
    LinearGradient backgroundGradient;
    LinearGradient textGradient;
    LinearGradient accentGradient;
    LinearGradient borderColorGradient;
    LinearGradient shadowColorGradient;
};

FLUID_API ComputedStyle Lerp(const ComputedStyle& a, const ComputedStyle& b, float t);

struct StyleValue { float v[4] = {0, 0, 0, 0}; };
struct StyleDecl  { Prop prop; StyleValue value; std::string text; };

struct StyleRule {
    std::string widget;      // empty = any
    std::vector<std::string> classes;
    uint8_t stateMask = 0;   // all bits must be present in the current state
    std::vector<StyleDecl> decls;
};

class FLUID_API StyleSheet {
public:
    class RuleBuilder {
    public:
        RuleBuilder(StyleSheet* s, size_t i) : sheet_(s), index_(i) {}

        RuleBuilder& Background(Color c)      { return Col(Prop::BackgroundReset, c); }
        RuleBuilder& Foreground(Color c)      { return Col(Prop::Foreground, c); }
        RuleBuilder& Accent(Color c)          { return Col(Prop::Accent, c); }
        RuleBuilder& BorderColor(Color c)     { return Col(Prop::BorderColor, c); }
        RuleBuilder& ShadowColor(Color c)     { return Col(Prop::ShadowColor, c); }
        RuleBuilder& BorderWidth(float v)     { return Num(Prop::BorderWidth, v); }
        RuleBuilder& Rounding(float v)        { return Num(Prop::Rounding, v); }
        RuleBuilder& FontSize(float v)        { return Num(Prop::FontSize, v); }
        RuleBuilder& Spacing(float v)         { return Num(Prop::Spacing, v); }
        RuleBuilder& ShadowBlur(float v)      { return Num(Prop::ShadowBlur, v); }
        RuleBuilder& Opacity(float v)         { return Num(Prop::Opacity, v); }
        RuleBuilder& TransitionSpeed(float v) { return Num(Prop::TransitionSpeed, v); }
        RuleBuilder& BackgroundImage(std::string source) {
            return Set(Prop::BackgroundImage, {}, std::move(source));
        }
        RuleBuilder& BackgroundGradient(std::string css) {
            return Set(Prop::BackgroundGradient, {}, std::move(css));
        }
        RuleBuilder& TextGradient(std::string css) {
            return Set(Prop::TextGradient, {}, std::move(css));
        }
        RuleBuilder& AccentGradient(std::string css) {
            return Set(Prop::AccentGradient, {}, std::move(css));
        }
        RuleBuilder& BorderColorGradient(std::string css) {
            return Set(Prop::BorderColorGradient, {}, std::move(css));
        }
        RuleBuilder& ShadowColorGradient(std::string css) {
            return Set(Prop::ShadowColorGradient, {}, std::move(css));
        }
        RuleBuilder& BackgroundSize(BackgroundFit fit) {
            return Num(Prop::BackgroundSize, static_cast<float>(fit));
        }
        RuleBuilder& BackgroundPosition(Vec2 position) {
            return Set(Prop::BackgroundPosition, {{position.x, position.y, 0.f, 0.f}});
        }
        RuleBuilder& Padding(Edges e) {
            return Set(Prop::Padding, {{e.top, e.right, e.bottom, e.left}});
        }
        RuleBuilder& Set(Prop p, StyleValue v, std::string text = {}) {
            sheet_->SetProp(index_, p, v, std::move(text));
            return *this;
        }

    private:
        RuleBuilder& Col(Prop p, Color c) { return Set(p, {{c.r, c.g, c.b, c.a}}); }
        RuleBuilder& Num(Prop p, float v) { return Set(p, {{v, 0, 0, 0}}); }
        StyleSheet* sheet_;
        size_t index_;
    };

    // Selector grammar: [widget|*][.class...][:state[:state...]]
    // e.g. "button", "button.primary", "button.primary.large:hover", "*", ".danger:active"
    // States: hover, active, checked, disabled
    RuleBuilder Rule(std::string_view selector);

    // Cascade: lower specificity first, later rules win ties.
    ComputedStyle Resolve(std::string_view widget,
                          const std::vector<std::string>& classes,
                          uint8_t state) const;

    void SetProp(size_t ruleIndex, Prop p, StyleValue v, std::string text = {});
    void RegisterImage(std::string_view source, GPUImageId image, Vec2 size);
    void Clear();
    bool LoadCSS(std::string_view css, std::string* error = nullptr);
    bool LoadCSSFile(const std::string& path, std::string* error = nullptr);
    uint32_t Version() const { return version_; }

    static StyleSheet Dark(); // built-in theme

private:
    struct ImageResource { GPUImageId id = 0; Vec2 size; };
    std::vector<StyleRule> rules_;
    std::unordered_map<std::string, ImageResource> images_;
    mutable std::unordered_map<uint32_t, ComputedStyle> cache_; // not thread-safe
    uint32_t version_ = 0;
};

}
