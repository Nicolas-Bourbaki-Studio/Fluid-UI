#include "FluidUI/Style.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
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

void ParseSelector(std::string_view s, std::string& widget,
                   std::vector<std::string>& classes, uint8_t& mask) {
    if (s == ":root") {
        widget.clear();
        classes.clear();
        mask = 0;
        return;
    }
    const size_t colon = s.find(':');
    std::string_view head = s.substr(0, colon);
    std::string_view states = (colon == std::string_view::npos) ? std::string_view{} : s.substr(colon + 1);

    const size_t dot = head.find('.');
    widget = std::string(head.substr(0, dot));
    if (widget == "*") widget.clear();
    classes.clear();
    if (dot != std::string_view::npos) {
        std::string_view remaining = head.substr(dot + 1);
        while (!remaining.empty()) {
            const size_t next = remaining.find('.');
            classes.emplace_back(remaining.substr(0, next));
            remaining = next == std::string_view::npos ? std::string_view{} : remaining.substr(next + 1);
        }
    }

    mask = 0;
    while (!states.empty()) {
        const size_t c = states.find(':');
        mask |= StateFromName(states.substr(0, c));
        states = (c == std::string_view::npos) ? std::string_view{} : states.substr(c + 1);
    }
}

bool ValidSelector(std::string_view selector) {
    if (selector == ":root") return true;
    if (selector.empty()) return false;
    for (char ch : selector)
        if (std::isspace(static_cast<unsigned char>(ch))) return false;
    const size_t colon = selector.find(':');
    std::string_view head = selector.substr(0, colon);
    std::string_view states = colon == std::string_view::npos
        ? std::string_view{} : selector.substr(colon + 1);
    const size_t dot = head.find('.');
    std::string_view widget = head.substr(0, dot);
    if (widget == "*") widget = {};
    auto validName = [](std::string_view name) {
        if (name.empty()) return false;
        for (char ch : name) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_')
                return false;
        }
        return true;
    };
    if (!widget.empty() && !validName(widget)) return false;
    if (dot != std::string_view::npos) {
        std::string_view classes = head.substr(dot + 1);
        while (!classes.empty()) {
            const size_t next = classes.find('.');
            if (!validName(classes.substr(0, next))) return false;
            if (next == std::string_view::npos) break;
            classes.remove_prefix(next + 1);
        }
    }
    while (!states.empty()) {
        const size_t next = states.find(':');
        const std::string_view state = states.substr(0, next);
        if (state != "hover" && state != "active" && state != "checked" && state != "disabled")
            return false;
        if (next != std::string_view::npos && next + 1 == states.size()) return false;
        if (next == std::string_view::npos) break;
        states.remove_prefix(next + 1);
    }
    return true;
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
    return end != owned.c_str() && *end == '\0' && std::isfinite(value);
}

bool ParseColor(std::string_view text, Color& color) {
    text = Trim(text);
    if (text == "transparent") { color = {0.f, 0.f, 0.f, 0.f}; return true; }
    if (text == "black") { color = Color::FromHex(0x000000FF); return true; }
    if (text == "white") { color = Color::FromHex(0xFFFFFFFF); return true; }
    if (text == "red") { color = Color::FromHex(0xFF0000FF); return true; }
    if (text == "green") { color = Color::FromHex(0x008000FF); return true; }
    if (text == "blue") { color = Color::FromHex(0x0000FFFF); return true; }
    if (text == "yellow") { color = Color::FromHex(0xFFFF00FF); return true; }
    if (text == "orange") { color = Color::FromHex(0xFFA500FF); return true; }
    if (text == "purple") { color = Color::FromHex(0x800080FF); return true; }
    if (text == "gray" || text == "grey") { color = Color::FromHex(0x808080FF); return true; }
    if (text.starts_with("rgb(") || text.starts_with("rgba(")) {
        const bool hasAlpha = text.starts_with("rgba(");
        const size_t open = text.find('('), close = text.rfind(')');
        if (close != text.size() - 1 || close <= open + 1) return false;
        std::string_view parts = text.substr(open + 1, close - open - 1);
        float channels[4]{0.f, 0.f, 0.f, 1.f};
        size_t count = 0;
        while (!parts.empty() && count < 4) {
            const size_t comma = parts.find(',');
            std::string_view component = Trim(parts.substr(0, comma));
            const bool percent = !component.empty() && component.back() == '%';
            if (percent) component.remove_suffix(1);
            float value = 0.f;
            if (!ParseNumber(component, value)) return false;
            channels[count] = count == 3
                ? (percent ? value / 100.f : value)
                : (percent ? value / 100.f : value / 255.f);
            ++count;
            if (comma == std::string_view::npos) {
                parts = {};
                break;
            }
            parts.remove_prefix(comma + 1);
        }
        if (count != (hasAlpha ? 4u : 3u) || !Trim(parts).empty()) return false;
        color = {std::clamp(channels[0], 0.f, 1.f), std::clamp(channels[1], 0.f, 1.f),
                 std::clamp(channels[2], 0.f, 1.f), std::clamp(channels[3], 0.f, 1.f)};
        return true;
    }
    if (text.empty() || text.front() != '#') return false;
    text.remove_prefix(1);
    if (text.size() == 3 || text.size() == 4) {
        uint32_t value = 0;
        for (char digit : text) {
            uint32_t nibble = 0;
            const auto parsed = std::from_chars(&digit, &digit + 1, nibble, 16);
            if (parsed.ec != std::errc{}) return false;
            value = (value << 8) | (nibble * 17u);
        }
        if (text.size() == 3) value = (value << 8) | 0xFFu;
        color = Color::FromHex(value);
        return true;
    }
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
    if (name == "background" || name == "background-color") return color(Prop::Background);
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

bool ResolveVariables(std::string_view source,
                      const std::unordered_map<std::string, std::string>& variables,
                      std::string& resolved) {
    resolved.assign(source);
    for (size_t position = 0; (position = resolved.find("var(", position)) != std::string::npos;) {
        const size_t close = resolved.find(')', position + 4);
        if (close == std::string::npos) return false;
        const std::string_view expression(resolved.data() + position + 4,
                                          close - position - 4);
        const size_t comma = expression.find(',');
        const std::string key(Trim(expression.substr(0, comma)));
        auto value = variables.find(key);
        std::string replacement;
        if (value != variables.end()) {
            replacement = value->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(Trim(expression.substr(comma + 1)));
        } else {
            return false;
        }
        resolved.replace(position, close - position + 1, replacement);
        position += replacement.size();
    }
    return true;
}

void CollectVariables(std::string_view source,
                      std::unordered_map<std::string, std::string>& variables) {
    while (!(source = Trim(source)).empty()) {
        const size_t open = source.find('{');
        if (open == std::string_view::npos) break;
        const size_t close = source.find('}', open + 1);
        if (close == std::string_view::npos) break;
        std::string_view declarations = source.substr(open + 1, close - open - 1);
        std::string_view selectorList = Trim(source.substr(0, open));
        bool global = false;
        while (!selectorList.empty()) {
            const size_t comma = selectorList.find(',');
            const std::string_view selector = Trim(selectorList.substr(0, comma));
            global = global || selector == ":root" || selector == "*";
            if (comma == std::string_view::npos) break;
            selectorList.remove_prefix(comma + 1);
        }
        while (!Trim(declarations).empty()) {
            const size_t semi = declarations.find(';');
            const std::string_view declaration = Trim(declarations.substr(0, semi));
            const size_t colon = declaration.find(':');
            if (global && colon != std::string_view::npos) {
                const std::string_view name = Trim(declaration.substr(0, colon));
                if (name.starts_with("--")) {
                    variables[std::string(name)] = std::string(Trim(declaration.substr(colon + 1)));
                }
            }
            if (semi == std::string_view::npos) break;
            declarations.remove_prefix(semi + 1);
        }
        source.remove_prefix(close + 1);
    }
}

} // namespace

StyleSheet::RuleBuilder StyleSheet::Rule(std::string_view selector) {
    StyleRule r;
    ParseSelector(selector, r.widget, r.classes, r.stateMask);
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

    std::unordered_map<std::string, std::string> variables;
    CollectVariables(source, variables);
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
                const std::string_view name = colon == std::string_view::npos
                    ? std::string_view{} : Trim(decl.substr(0, colon));
                std::string value;
                if (colon == std::string_view::npos ||
                    (!name.starts_with("--") &&
                     !ResolveVariables(decl.substr(colon + 1), variables, value))) {
                    if (error) *error = "Invalid CSS variable in declaration: " + std::string(decl);
                    return false;
                }
                if (!name.starts_with("--")) {
                    std::vector<std::pair<Prop, StyleValue>> parsed;
                    if (name == "border") {
                        std::string_view parts = Trim(value);
                        bool valid = true, foundWidth = false, foundColor = false;
                        while (!parts.empty()) {
                            const size_t split = parts.find_first_of(" \t\r\n");
                            const std::string_view token = Trim(parts.substr(0, split));
                            if (token == "solid") {
                            } else if (token == "none") {
                                parsed.emplace_back(Prop::BorderWidth, StyleValue{{0.f, 0.f, 0.f, 0.f}});
                                foundWidth = true;
                            } else if (Color color; ParseColor(token, color)) {
                                parsed.emplace_back(Prop::BorderColor,
                                                    StyleValue{{color.r, color.g, color.b, color.a}});
                                foundColor = true;
                            } else {
                                float width = 0.f;
                                if (!ParseNumber(token, width)) { valid = false; break; }
                                parsed.emplace_back(Prop::BorderWidth, StyleValue{{width, 0.f, 0.f, 0.f}});
                                foundWidth = true;
                            }
                            if (split == std::string_view::npos) break;
                            parts.remove_prefix(split + 1);
                        }
                        if (!valid || (!foundWidth && !foundColor)) parsed.clear();
                    } else {
                        Prop prop{};
                        StyleValue parsedValue{};
                        if (ParseDeclaration(name, value, prop, parsedValue))
                            parsed.emplace_back(prop, parsedValue);
                    }
                    if (parsed.empty()) {
                        if (error) *error = "Invalid or unsupported CSS declaration: " + std::string(decl);
                        return false;
                    }
                    declarations.insert(declarations.end(), parsed.begin(), parsed.end());
                }
            }
            if (semi == std::string_view::npos) break;
            body.remove_prefix(semi + 1);
        }
        std::string_view remainingSelectors = selectors;
        while (!remainingSelectors.empty()) {
            const size_t comma = remainingSelectors.find(',');
            const std::string_view selector = Trim(remainingSelectors.substr(0, comma));
            if (!ValidSelector(selector)) {
                if (error) *error = "Invalid or unsupported CSS selector: " + std::string(selector);
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
        if (!std::all_of(r.classes.begin(), r.classes.end(), [&](const std::string& cls) {
                return std::find(classes.begin(), classes.end(), cls) != classes.end();
            })) continue;
        if ((r.stateMask & ~static_cast<unsigned>(state)) != 0) continue;
        const int spec = (r.widget.empty() ? 0 : 1) + static_cast<int>(r.classes.size()) * 2
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

    s.Rule("*").Foreground(C::FromHex(0xE7EDF7FF)).FontSize(15).Spacing(8).TransitionSpeed(16);

    s.Rule("window")
        .Background(C::FromHex(0x101722F8)).BorderColor(C::FromHex(0x334155FF)).BorderWidth(1)
        .Rounding(12).Padding(Edges(12))
        .ShadowColor(C::FromHex(0x020617A0)).ShadowBlur(18);
    s.Rule("window-title")
        .Background(C::FromHex(0x1A2637FF)).Foreground(C::FromHex(0xF1F5FCFF))
        .Padding(Edges(7, 12)).FontSize(15);
    s.Rule("window-close")
        .Background(C::FromHex(0x29374AFF)).Foreground(C::FromHex(0xF1F5FCFF))
        .Rounding(6).FontSize(15);
    s.Rule("window-close:hover").Background(C::FromHex(0xD94B5AFF));
    s.Rule("window-scrollbar")
        .Background(C::FromHex(0x263346FF)).Accent(C::FromHex(0x6885AFFF));
    s.Rule("dock-preview").Background(C::FromHex(0x4D83F7FF)).Accent(C::FromHex(0x73A5FFFF))
        .Rounding(14);

    s.Rule("text").Padding(Edges(2, 0)).Foreground(C::FromHex(0xCFD9E8FF));

    s.Rule("button")
        .Background(C::FromHex(0x253449FF)).BorderColor(C::FromHex(0x3D526FFF)).BorderWidth(1)
        .Rounding(8).Padding(Edges(8, 15));
    s.Rule("button:hover").Background(C::FromHex(0x304762FF)).BorderColor(C::FromHex(0x5C7DA6FF));
    s.Rule("button:active").Background(C::FromHex(0x386FE0FF));

    s.Rule("checkbox")
        .Background(C::FromHex(0x1B2839FF)).BorderColor(C::FromHex(0x526783FF)).BorderWidth(1)
        .Rounding(5).Accent(C::FromHex(0x5A91F7FF));
    s.Rule("checkbox:hover").BorderColor(C::FromHex(0x7EA8E9FF));

    s.Rule("slider")
        .Background(C::FromHex(0x24364FFF)).Accent(C::FromHex(0x5A91F7FF))
        .Rounding(7).Padding(Edges(5, 10));
    s.Rule("slider:hover").Accent(C::FromHex(0x82B2FFFF));

    s.Rule("toggle")
        .Background(C::FromHex(0x35465DFF)).Accent(C::FromHex(0x5A91F7FF))
        .Foreground(C::FromHex(0xF1F5FCFF)).Spacing(9);
    s.Rule("radio")
        .Background(C::FromHex(0x1B2839FF)).BorderColor(C::FromHex(0x607995FF))
        .BorderWidth(1).Accent(C::FromHex(0x5A91F7FF)).Spacing(9);
    s.Rule("container")
        .Background(C::FromHex(0x172231FF)).BorderColor(C::FromHex(0x2C4058FF))
        .BorderWidth(1).Rounding(9).Padding(Edges(10)).Spacing(9);
    s.Rule("collapsible-container")
        .Background(C::FromHex(0x172231FF)).BorderColor(C::FromHex(0x2C4058FF))
        .BorderWidth(1).Rounding(9).Padding(Edges(10)).Spacing(9);
    s.Rule("horizontal-list")
        .Background(C::FromHex(0x172231FF)).BorderColor(C::FromHex(0x2C4058FF))
        .BorderWidth(1).Rounding(9).Padding(Edges(10)).Spacing(9);
    s.Rule("vertical-list")
        .Background(C::FromHex(0x172231FF)).BorderColor(C::FromHex(0x2C4058FF))
        .BorderWidth(1).Rounding(9).Padding(Edges(10)).Spacing(9);
    s.Rule("tab")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x344D6AFF))
        .BorderWidth(1).Rounding(7).Padding(Edges(7, 11));
    s.Rule("tab:checked").Background(C::FromHex(0x386FE0FF)).BorderColor(C::FromHex(0x6190E8FF));
    s.Rule("dropdown")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x3D526FFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 12));
    s.Rule("dropdown:hover").Background(C::FromHex(0x304762FF));
    s.Rule("image-button")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x3D526FFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 12));
    s.Rule("image-button:hover").Background(C::FromHex(0x304762FF));
    s.Rule("text-label").Spacing(7);
    s.Rule("image-label").Spacing(7);

    return s;
}

} // namespace fluid