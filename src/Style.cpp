#include "FluidUI/Style.h"

#include <algorithm>
#include <bit>

namespace fluid {

ComputedStyle Lerp(const ComputedStyle& a, const ComputedStyle& b, float t) {
    ComputedStyle o;
    o.background  = Lerp(a.background,  b.background,  t);
    o.foreground  = Lerp(a.foreground,  b.foreground,  t);
    o.accent      = Lerp(a.accent,      b.accent,      t);
    o.borderColor = Lerp(a.borderColor, b.borderColor, t);
    o.shadowColor = Lerp(a.shadowColor, b.shadowColor, t);
    o.borderWidth = Lerp(a.borderWidth, b.borderWidth, t);
    o.rounding    = Lerp(a.rounding,    b.rounding,    t);
    o.fontSize    = Lerp(a.fontSize,    b.fontSize,    t);
    o.spacing     = Lerp(a.spacing,     b.spacing,     t);
    o.shadowBlur  = Lerp(a.shadowBlur,  b.shadowBlur,  t);
    o.opacity     = Lerp(a.opacity,     b.opacity,     t);
    o.transitionSpeed = a.transitionSpeed;
    o.padding     = Lerp(a.padding,     b.padding,     t);
    return o;
}

namespace {

uint8_t StateFromName(std::string_view n) {
    if (n == "hover")    return State_Hovered;
    if (n == "active")   return State_Active;
    if (n == "checked")  return State_Checked;
    if (n == "disabled") return State_Disabled;
    return 0;
}

void ParseSelector(std::string_view s, std::string& widget, std::string& cls, uint8_t& mask) {
    const size_t colon = s.find(':');
    std::string_view head = s.substr(0, colon);
    std::string_view states = (colon == std::string_view::npos) ? std::string_view{} : s.substr(colon + 1);

    const size_t dot = head.find('.');
    widget = std::string(head.substr(0, dot));
    cls = (dot == std::string_view::npos) ? std::string{} : std::string(head.substr(dot + 1));
    if (widget == "*") widget.clear();

    mask = 0;
    while (!states.empty()) {
        const size_t c = states.find(':');
        mask |= StateFromName(states.substr(0, c));
        states = (c == std::string_view::npos) ? std::string_view{} : states.substr(c + 1);
    }
}

void Apply(ComputedStyle& s, const StyleDecl& d) {
    const float* v = d.value.v;
    const Color col{v[0], v[1], v[2], v[3]};
    switch (d.prop) {
        case Prop::Background:      s.background = col; break;
        case Prop::Foreground:      s.foreground = col; break;
        case Prop::Accent:          s.accent = col; break;
        case Prop::BorderColor:     s.borderColor = col; break;
        case Prop::ShadowColor:     s.shadowColor = col; break;
        case Prop::BorderWidth:     s.borderWidth = v[0]; break;
        case Prop::Rounding:        s.rounding = v[0]; break;
        case Prop::FontSize:        s.fontSize = v[0]; break;
        case Prop::Spacing:         s.spacing = v[0]; break;
        case Prop::ShadowBlur:      s.shadowBlur = v[0]; break;
        case Prop::Opacity:         s.opacity = v[0]; break;
        case Prop::TransitionSpeed: s.transitionSpeed = v[0]; break;
        case Prop::Padding:         s.padding = Edges(v[0], v[1], v[2], v[3]); break;
    }
}

} // namespace

StyleSheet::RuleBuilder StyleSheet::Rule(std::string_view selector) {
    StyleRule r;
    ParseSelector(selector, r.widget, r.cls, r.stateMask);
    rules_.push_back(std::move(r));
    cache_.clear();
    ++version_;
    return RuleBuilder(this, rules_.size() - 1);
}

void StyleSheet::SetProp(size_t ruleIndex, Prop p, StyleValue v) {
    if (ruleIndex >= rules_.size()) return;
    auto& decls = rules_[ruleIndex].decls;
    auto it = std::find_if(decls.begin(), decls.end(), [&](const StyleDecl& d) { return d.prop == p; });
    if (it != decls.end()) it->value = v; else decls.push_back({p, v});
    cache_.clear();
    ++version_;
}

void StyleSheet::Clear() { rules_.clear(); cache_.clear(); ++version_; }

ComputedStyle StyleSheet::Resolve(std::string_view widget,
                                  const std::vector<std::string>& classes,
                                  uint8_t state) const {
    uint32_t key = HashStr(widget);
    for (const auto& c : classes) key = HashStr(c, key ^ 0x9E3779B9u);
    key = key * 31u + state;
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    std::vector<std::pair<int, const StyleRule*>> hits;
    for (const StyleRule& r : rules_) {
        if (!r.widget.empty() && r.widget != widget) continue;
        if (!r.cls.empty() && std::find(classes.begin(), classes.end(), r.cls) == classes.end()) continue;
        if ((r.stateMask & ~static_cast<unsigned>(state)) != 0) continue;
        const int spec = (r.widget.empty() ? 0 : 1) + (r.cls.empty() ? 0 : 2)
                       + 4 * std::popcount(static_cast<unsigned>(r.stateMask));
        hits.emplace_back(spec, &r);
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    ComputedStyle out;
    for (const auto& h : hits)
        for (const StyleDecl& d : h.second->decls) Apply(out, d);

    cache_[key] = out;
    return out;
}

StyleSheet StyleSheet::Dark() {
    using C = Color;
    StyleSheet s;

    s.Rule("*").Foreground(C::FromHex(0xE6E6EBFF)).FontSize(16).Spacing(6).TransitionSpeed(14);

    s.Rule("window")
        .Background(C::FromHex(0x1B1B20F2)).BorderColor(C::FromHex(0x34343EFF)).BorderWidth(1)
        .Rounding(10).Padding(Edges(10))
        .ShadowColor(C::FromHex(0x000000A0)).ShadowBlur(18);
    s.Rule("window-title")
        .Background(C::FromHex(0x24242BFF)).Padding(Edges(6, 10));

    s.Rule("text").Padding(Edges(2, 0));

    s.Rule("button")
        .Background(C::FromHex(0x2E2E38FF)).BorderColor(C::FromHex(0x3C3C48FF)).BorderWidth(1)
        .Rounding(6).Padding(Edges(6, 14));
    s.Rule("button:hover").Background(C::FromHex(0x3A3A48FF));
    s.Rule("button:active").Background(C::FromHex(0x4A6CF7FF));

    s.Rule("checkbox")
        .Background(C::FromHex(0x2A2A33FF)).BorderColor(C::FromHex(0x3C3C48FF)).BorderWidth(1)
        .Rounding(4).Accent(C::FromHex(0x4A6CF7FF));
    s.Rule("checkbox:hover").BorderColor(C::FromHex(0x4A6CF7FF));

    s.Rule("slider")
        .Background(C::FromHex(0x2A2A33FF)).Accent(C::FromHex(0x4A6CF7B0))
        .Rounding(6).Padding(Edges(5, 10));
    s.Rule("slider:hover").Accent(C::FromHex(0x4A6CF7E0));

    return s;
}

} // namespace fluid