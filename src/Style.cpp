#include "FluidUI/Style.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

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

std::string_view Trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

bool ParseNumber(std::string_view text, float& value) {
    text = Trim(text);
    if (text.size() >= 2 && text.substr(text.size() - 2) == "px")
        text = Trim(text.substr(0, text.size() - 2));
    std::string owned(text);
    char* end = nullptr;
    value = std::strtof(owned.c_str(), &end);
    return end != owned.c_str() && *end == '\0';
}

bool ParseColor(std::string_view text, Color& color) {
    text = Trim(text);
    if (text.empty() || text.front() != '#') return false;
    text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) return false;
    uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    color = text.size() == 6 ? Color::FromHex((value << 8) | 0xFFu) : Color::FromHex(value);
    return true;
}

bool ParseDeclaration(std::string_view name, std::string_view value,
                      Prop& prop, StyleValue& out) {
    name = Trim(name);
    value = Trim(value);
    auto color = [&](Prop p) {
        Color c;
        if (!ParseColor(value, c)) return false;
        prop = p;
        out = {{c.r, c.g, c.b, c.a}};
        return true;
    };
    auto number = [&](Prop p) {
        float v = 0.f;
        if (!ParseNumber(value, v)) return false;
        prop = p;
        out = {{v, 0.f, 0.f, 0.f}};
        return true;
    };
    if (name == "background") return color(Prop::Background);
    if (name == "color" || name == "foreground") return color(Prop::Foreground);
    if (name == "accent") return color(Prop::Accent);
    if (name == "border-color") return color(Prop::BorderColor);
    if (name == "shadow-color") return color(Prop::ShadowColor);
    if (name == "border-width") return number(Prop::BorderWidth);
    if (name == "border-radius" || name == "rounding") return number(Prop::Rounding);
    if (name == "font-size") return number(Prop::FontSize);
    if (name == "gap" || name == "spacing") return number(Prop::Spacing);
    if (name == "shadow-blur") return number(Prop::ShadowBlur);
    if (name == "opacity") return number(Prop::Opacity);
    if (name == "transition-speed") return number(Prop::TransitionSpeed);
    if (name == "padding") {
        float values[4]{};
        size_t count = 0;
        while (!value.empty() && count < 4) {
            const size_t split = value.find_first_of(" \t\r\n");
            const std::string_view token = Trim(value.substr(0, split));
            if (!ParseNumber(token, values[count++])) return false;
            value = split == std::string_view::npos ? std::string_view{} : Trim(value.substr(split));
        }
        if (!value.empty() || count == 0) return false;
        float top = values[0], right = values[0], bottom = values[0], left = values[0];
        if (count == 2) { right = left = values[1]; }
        if (count == 3) { right = left = values[1]; bottom = values[2]; }
        if (count == 4) { right = values[1]; bottom = values[2]; left = values[3]; }
        prop = Prop::Padding;
        out = {{top, right, bottom, left}};
        return true;
    }
    return false;
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

bool StyleSheet::LoadCSS(std::string_view css, std::string* error) {
    std::string source(css);
    for (size_t start = 0; (start = source.find("/*", start)) != std::string::npos;) {
        const size_t end = source.find("*/", start + 2);
        if (end == std::string::npos) {
            if (error) *error = "Unterminated CSS comment";
            return false;
        }
        source.erase(start, end + 2 - start);
    }

    StyleSheet candidate = *this;
    std::string_view rest(source);
    while (!(rest = Trim(rest)).empty()) {
        const size_t open = rest.find('{');
        if (open == std::string_view::npos) {
            if (error) *error = "Expected '{' after CSS selector";
            return false;
        }
        const std::string_view selectors = Trim(rest.substr(0, open));
        const size_t close = rest.find('}', open + 1);
        if (selectors.empty() || close == std::string_view::npos) {
            if (error) *error = "Invalid CSS rule";
            return false;
        }
        std::string_view body = rest.substr(open + 1, close - open - 1);
        std::vector<std::pair<Prop, StyleValue>> declarations;
        while (!Trim(body).empty()) {
            const size_t semi = body.find(';');
            const std::string_view decl = Trim(body.substr(0, semi));
            if (!decl.empty()) {
                const size_t colon = decl.find(':');
                Prop prop{};
                StyleValue value{};
                if (colon == std::string_view::npos ||
                    !ParseDeclaration(decl.substr(0, colon), decl.substr(colon + 1), prop, value)) {
                    if (error) *error = "Invalid or unsupported CSS declaration: " + std::string(decl);
                    return false;
                }
                declarations.emplace_back(prop, value);
            }
            if (semi == std::string_view::npos) break;
            body.remove_prefix(semi + 1);
        }
        std::string_view remainingSelectors = selectors;
        while (!remainingSelectors.empty()) {
            const size_t comma = remainingSelectors.find(',');
            const std::string_view selector = Trim(remainingSelectors.substr(0, comma));
            if (selector.empty()) {
                if (error) *error = "Empty CSS selector";
                return false;
            }
            auto rule = candidate.Rule(selector);
            for (const auto& declaration : declarations)
                rule.Set(declaration.first, declaration.second);
            if (comma == std::string_view::npos) break;
            remainingSelectors.remove_prefix(comma + 1);
        }
        rest.remove_prefix(close + 1);
    }
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}

bool StyleSheet::LoadCSSFile(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "Could not open stylesheet: " + path;
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        if (error) *error = "Could not read stylesheet: " + path;
        return false;
    }
    return LoadCSS(contents.str(), error);
}

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

    s.Rule("toggle")
        .Background(C::FromHex(0x383843FF)).Accent(C::FromHex(0x4A6CF7FF))
        .Foreground(C::FromHex(0xF5F5FAFF)).Spacing(8);
    s.Rule("radio")
        .Background(C::FromHex(0x2A2A33FF)).BorderColor(C::FromHex(0x737381FF))
        .BorderWidth(1).Accent(C::FromHex(0x4A6CF7FF)).Spacing(8);
    s.Rule("container")
        .Background(C::FromHex(0x202027FF)).BorderColor(C::FromHex(0x34343EFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8)).Spacing(8);
    s.Rule("collapsible-container")
        .Background(C::FromHex(0x202027FF)).BorderColor(C::FromHex(0x34343EFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8)).Spacing(8);
    s.Rule("horizontal-list")
        .Background(C::FromHex(0x202027FF)).BorderColor(C::FromHex(0x34343EFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8)).Spacing(8);
    s.Rule("vertical-list")
        .Background(C::FromHex(0x202027FF)).BorderColor(C::FromHex(0x34343EFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8)).Spacing(8);
    s.Rule("tab")
        .Background(C::FromHex(0x2A2A33FF)).BorderColor(C::FromHex(0x3C3C48FF))
        .BorderWidth(1).Rounding(5).Padding(Edges(5, 10));
    s.Rule("tab:checked").Background(C::FromHex(0x4A6CF7FF));
    s.Rule("dropdown")
        .Background(C::FromHex(0x2E2E38FF)).BorderColor(C::FromHex(0x3C3C48FF))
        .BorderWidth(1).Rounding(6).Padding(Edges(6, 12));
    s.Rule("dropdown:hover").Background(C::FromHex(0x3A3A48FF));
    s.Rule("image-button")
        .Background(C::FromHex(0x2E2E38FF)).BorderColor(C::FromHex(0x3C3C48FF))
        .BorderWidth(1).Rounding(6).Padding(Edges(6, 12));
    s.Rule("image-button:hover").Background(C::FromHex(0x3A3A48FF));
    s.Rule("text-label, image-label").Spacing(6);

    return s;
}

} // namespace fluid