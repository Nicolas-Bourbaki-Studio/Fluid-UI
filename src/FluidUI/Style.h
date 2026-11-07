#pragma once
#include "Export.h"
#include "Types.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fluid {

// Every stylable property. Add new ones here + in ComputedStyle + Apply() in Style.cpp.
enum class Prop : uint8_t {
    Background, Foreground, Accent, BorderColor, ShadowColor,
    BorderWidth, Rounding, FontSize, Spacing, ShadowBlur, Opacity,
    Padding, TransitionSpeed
};

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
    float fontSize    = 16.f;
    float spacing     = 6.f;
    float shadowBlur  = 0.f;
    float opacity     = 1.f;
    float transitionSpeed = 14.f; // higher = snappier
    Edges padding {4.f, 8.f, 4.f, 8.f};
};

FLUID_API ComputedStyle Lerp(const ComputedStyle& a, const ComputedStyle& b, float t);

struct StyleValue { float v[4] = {0, 0, 0, 0}; };
struct StyleDecl  { Prop prop; StyleValue value; };

struct StyleRule {
    std::string widget;      // empty = any
    std::string cls;         // empty = any
    uint8_t stateMask = 0;   // all bits must be present in the current state
    std::vector<StyleDecl> decls;
};

class FLUID_API StyleSheet {
public:
    class RuleBuilder {
    public:
        RuleBuilder(StyleSheet* s, size_t i) : sheet_(s), index_(i) {}

        RuleBuilder& Background(Color c)      { return Col(Prop::Background, c); }
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
        RuleBuilder& Padding(Edges e) {
            return Set(Prop::Padding, {{e.top, e.right, e.bottom, e.left}});
        }
        RuleBuilder& Set(Prop p, StyleValue v) { sheet_->SetProp(index_, p, v); return *this; }

    private:
        RuleBuilder& Col(Prop p, Color c) { return Set(p, {{c.r, c.g, c.b, c.a}}); }
        RuleBuilder& Num(Prop p, float v) { return Set(p, {{v, 0, 0, 0}}); }
        StyleSheet* sheet_;
        size_t index_;
    };

    // Selector grammar:  [widget|*][.class][:state[:state...]]
    // e.g. "button", "button.primary", "button.primary:hover", "*", ".danger:active"
    // States: hover, active, checked, disabled
    RuleBuilder Rule(std::string_view selector);

    // Cascade: lower specificity first, later rules win ties.
    ComputedStyle Resolve(std::string_view widget,
                          const std::vector<std::string>& classes,
                          uint8_t state) const;

    void SetProp(size_t ruleIndex, Prop p, StyleValue v);
    void Clear();
    uint32_t Version() const { return version_; }

    static StyleSheet Dark(); // built-in theme

private:
    std::vector<StyleRule> rules_;
    mutable std::unordered_map<uint32_t, ComputedStyle> cache_; // not thread-safe
    uint32_t version_ = 0;
};

}
