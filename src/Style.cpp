#include <FluidUI/Style.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace fluid {

ComputedStyle Lerp(const ComputedStyle& a, const ComputedStyle& b, float t) {
    ComputedStyle o;
    o.background  = Lerp(a.background,  b.background,  t);
    o.foreground  = Lerp(a.foreground,  b.foreground,  t);
    o.accent      = Lerp(a.accent,      b.accent,      t);
    o.borderColor = Lerp(a.borderColor, b.borderColor, t);
    o.shadowColor = Lerp(a.shadowColor, b.shadowColor, t);
    o.outlineColor = Lerp(a.outlineColor, b.outlineColor, t);
    o.borderWidth = Lerp(a.borderWidth, b.borderWidth, t);
    o.outlineWidth = Lerp(a.outlineWidth, b.outlineWidth, t);
    o.outlineEnabled = t < 0.5f ? a.outlineEnabled : b.outlineEnabled;
    o.outlineColorSet = t < 0.5f ? a.outlineColorSet : b.outlineColorSet;
    o.rounding    = Lerp(a.rounding,    b.rounding,    t);
    o.cornerRadii = Lerp(a.cornerRadii, b.cornerRadii, t);
    o.fontSize    = Lerp(a.fontSize,    b.fontSize,    t);
    const bool interpolateWidth = a.width >= 0.f && b.width >= 0.f &&
                                  a.widthPercent == b.widthPercent;
    o.width = interpolateWidth ? Lerp(a.width, b.width, t)
                               : (t < 0.5f ? a.width : b.width);
    o.widthPercent = t < 0.5f ? a.widthPercent : b.widthPercent;
    const bool interpolateHeight = a.height >= 0.f && b.height >= 0.f &&
                                   a.heightPercent == b.heightPercent;
    o.height = interpolateHeight ? Lerp(a.height, b.height, t)
                                 : (t < 0.5f ? a.height : b.height);
    o.heightPercent = t < 0.5f ? a.heightPercent : b.heightPercent;
    o.justifySelf = t < 0.5f ? a.justifySelf : b.justifySelf;
    o.alignSelf = t < 0.5f ? a.alignSelf : b.alignSelf;
    o.textAlign = t < 0.5f ? a.textAlign : b.textAlign;
    o.textVerticalAlign = t < 0.5f ? a.textVerticalAlign : b.textVerticalAlign;
    o.textAlignSet = t < 0.5f ? a.textAlignSet : b.textAlignSet;
    o.textVerticalAlignSet = t < 0.5f ? a.textVerticalAlignSet : b.textVerticalAlignSet;
    o.spacing     = Lerp(a.spacing,     b.spacing,     t);
    o.shadowBlur  = Lerp(a.shadowBlur,  b.shadowBlur,  t);
    o.shadowEnabled = t < 0.5f ? a.shadowEnabled : b.shadowEnabled;
    o.windowCanDock = t < 0.5f ? a.windowCanDock : b.windowCanDock;
    o.windowChildDock = t < 0.5f ? a.windowChildDock : b.windowChildDock;
    o.opacity     = Lerp(a.opacity,     b.opacity,     t);
    o.transitionSpeed = a.transitionSpeed;
    o.padding     = Lerp(a.padding,     b.padding,     t);
    o.backgroundImage = t < 0.5f ? a.backgroundImage : b.backgroundImage;
    o.backgroundImageSize = {
        Lerp(a.backgroundImageSize.x, b.backgroundImageSize.x, t),
        Lerp(a.backgroundImageSize.y, b.backgroundImageSize.y, t)
    };
    o.backgroundFit = t < 0.5f ? a.backgroundFit : b.backgroundFit;
    o.backgroundPosition = {
        Lerp(a.backgroundPosition.x, b.backgroundPosition.x, t),
        Lerp(a.backgroundPosition.y, b.backgroundPosition.y, t)
    };
    o.backgroundGradient = t < 0.5f ? a.backgroundGradient : b.backgroundGradient;
    o.textGradient = t < 0.5f ? a.textGradient : b.textGradient;
    o.accentGradient = t < 0.5f ? a.accentGradient : b.accentGradient;
    o.borderColorGradient = t < 0.5f ? a.borderColorGradient : b.borderColorGradient;
    o.shadowColorGradient = t < 0.5f ? a.shadowColorGradient : b.shadowColorGradient;
    o.outlineColorGradient = t < 0.5f ? a.outlineColorGradient : b.outlineColorGradient;
    return o;
}

namespace {

std::string Lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i])))
            return false;
    }
    return true;
}

uint8_t StateFromName(std::string_view n) {
    if (n == "hover")    return State_Hovered;
    if (n == "active")   return State_Active;
    if (n == "checked")  return State_Checked;
    if (n == "disabled") return State_Disabled;
    return 0;
}

void ParseSelector(std::string_view selector, std::string& widget,
                   std::string& id, std::vector<std::string>& classes,
                   uint8_t& mask) {
    widget.clear();
    id.clear();
    classes.clear();
    mask = 0;
    if (selector == ":root" || selector == "*:root") {
        return;
    }
    const size_t colon = selector.find(':');
    const std::string_view head = selector.substr(0, colon);
    std::string_view states = colon == std::string_view::npos
        ? std::string_view{} : selector.substr(colon + 1);
    size_t position = 0;
    while (position < head.size() && head[position] != '.' && head[position] != '#') {
        ++position;
    }
    const std::string_view type = head.substr(0, position);
    if (type != "*") widget.assign(type);
    while (position < head.size()) {
        const char marker = head[position++];
        const size_t start = position;
        while (position < head.size() && head[position] != '.' && head[position] != '#')
            ++position;
        const std::string_view name = head.substr(start, position - start);
        if (marker == '#') id.assign(name);
        else if (marker == '.') classes.emplace_back(name);
    }
    while (!states.empty()) {
        const size_t c = states.find(':');
        mask |= StateFromName(states.substr(0, c));
        states = (c == std::string_view::npos) ? std::string_view{} : states.substr(c + 1);
    }
}

bool ValidSelector(std::string_view selector) {
    if (selector == ":root" || selector == "*:root") return true;
    if (selector.empty()) return false;
    for (char ch : selector)
        if (std::isspace(static_cast<unsigned char>(ch))) return false;
    const size_t colon = selector.find(':');
    const std::string_view head = selector.substr(0, colon);
    std::string_view states = colon == std::string_view::npos
        ? std::string_view{} : selector.substr(colon + 1);
    auto validName = [](std::string_view name) {
        if (name.empty()) return false;
        for (char ch : name) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_')
                return false;
        }
        return true;
    };
    size_t position = 0;
    while (position < head.size() && head[position] != '.' && head[position] != '#')
        ++position;
    const std::string_view type = head.substr(0, position);
    if (!type.empty() && type != "*" && !validName(type)) return false;
    bool hasId = false;
    while (position < head.size()) {
        const char marker = head[position++];
        const size_t start = position;
        while (position < head.size() && head[position] != '.' && head[position] != '#')
            ++position;
        if (!validName(head.substr(start, position - start))) return false;
        if (marker == '#') {
            if (hasId) return false;
            hasId = true;
        }
    }
    if (type == "*" && head.size() > 1 && head[1] != '.' && head[1] != '#')
        return false;
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

bool ParseLinearGradient(std::string_view text, LinearGradient& gradient);
bool ParseColor(std::string_view text, Color& color);

bool IsSupportedDeclaration(std::string_view name) {
    return name == "background" || name == "background-color" ||
        name == "background-gradient" || name == "color" || name == "text-color" ||
        name == "foreground" || name == "foreground-color" || name == "accent" ||
        name == "border-color" || name == "shadow-color" || name == "outline-color" ||
        name == "outline-width" || name == "outline-style" || name == "outline" ||
        name == "border-width" || name == "border-radius" || name == "rounding" ||
        name == "border-radius-top-left" || name == "border-radius-top-right" ||
        name == "border-radius-bottom-left" || name == "border-radius-bottom-right" ||
        name == "font-size" || name == "gap" || name == "spacing" ||
        name == "width" || name == "height" || name == "align" ||
        name == "alignment" || name == "align-self" || name == "justify-self" ||
        name == "text-align" || name == "vertical-align" ||
        name == "shadow-blur" || name == "shadow-enabled" ||
        name == "opacity" || name == "transition-speed" ||
        name == "window-can-dock" || name == "window-child-dock" ||
        name == "padding" || name == "border" || name == "background-image" ||
        name == "text-gradient" || name == "color-gradient" ||
        name == "foreground-gradient" ||
        name == "accent-gradient" || name == "border-color-gradient" ||
        name == "shadow-color-gradient" || name == "background-size" ||
        name == "background-position";
}

void Apply(ComputedStyle& s, const StyleDecl& d) {
    const float* v = d.value.v;
    const Color col{v[0], v[1], v[2], v[3]};
    switch (d.prop) {
        case Prop::Background:
            s.background = col;
            s.backgroundGradient = {};
            break;
        case Prop::BackgroundReset:
            s.background = col;
            s.backgroundGradient = {};
            s.backgroundImage = 0;
            s.backgroundImageSize = {};
            break;
        case Prop::Foreground:
            s.foreground = col;
            s.textGradient = {};
            break;
        case Prop::Accent:
            s.accent = col;
            s.accentGradient = {};
            break;
        case Prop::BorderColor:
            s.borderColor = col;
            s.borderColorGradient = {};
            break;
        case Prop::ShadowColor:
            s.shadowColor = col;
            s.shadowColorGradient = {};
            break;
        case Prop::OutlineColor:
            s.outlineColor = col;
            s.outlineColorGradient = {};
            s.outlineColorSet = true;
            break;
        case Prop::BorderWidth: s.borderWidth = v[0]; break;
        case Prop::OutlineWidth: s.outlineWidth = v[0]; break;
        case Prop::OutlineStyle: s.outlineEnabled = v[0] != 0.f; break;
        case Prop::Outline:
            s.outlineWidth = v[0];
            s.outlineEnabled = v[1] != 0.f;
            if (d.text == "none") {
                s.outlineColor = {};
                s.outlineColorGradient = {};
                s.outlineColorSet = false;
            } else if (!d.text.empty()) {
                Color outlineColor;
                if (ParseColor(d.text, outlineColor)) {
                    s.outlineColor = outlineColor;
                    s.outlineColorGradient = {};
                    s.outlineColorSet = true;
                } else {
                    s.outlineColor = {};
                    ParseLinearGradient(d.text, s.outlineColorGradient);
                    s.outlineColorSet = true;
                }
            }
            break;
        case Prop::Rounding:
            s.rounding = v[0];
            s.cornerRadii = CornerRadii(v[0]);
            break;
        case Prop::RoundingTopLeft:     s.cornerRadii.topLeft = v[0]; break;
        case Prop::RoundingTopRight:    s.cornerRadii.topRight = v[0]; break;
        case Prop::RoundingBottomRight: s.cornerRadii.bottomRight = v[0]; break;
        case Prop::RoundingBottomLeft:  s.cornerRadii.bottomLeft = v[0]; break;
        case Prop::FontSize:        s.fontSize = v[0]; break;
        case Prop::Width:
            s.width = v[0];
            s.widthPercent = v[1] != 0.f;
            break;
        case Prop::Height:
            s.height = v[0];
            s.heightPercent = v[1] != 0.f;
            break;
        case Prop::Alignment:
            s.justifySelf = static_cast<HorizontalAlignment>(static_cast<uint8_t>(v[0]));
            s.alignSelf = static_cast<VerticalAlignment>(static_cast<uint8_t>(v[1]));
            break;
        case Prop::JustifySelf:
            s.justifySelf = static_cast<HorizontalAlignment>(static_cast<uint8_t>(v[0]));
            break;
        case Prop::AlignSelf:
            s.alignSelf = static_cast<VerticalAlignment>(static_cast<uint8_t>(v[0]));
            break;
        case Prop::TextAlign:
            s.textAlign = static_cast<TextAlignment>(static_cast<uint8_t>(v[0]));
            s.textAlignSet = true;
            break;
        case Prop::VerticalAlign:
            s.textVerticalAlign = static_cast<VerticalAlignment>(static_cast<uint8_t>(v[0]));
            s.textVerticalAlignSet = true;
            break;
        case Prop::Spacing:         s.spacing = v[0]; break;
        case Prop::ShadowBlur:      s.shadowBlur = v[0]; break;
        case Prop::ShadowEnabled:   s.shadowEnabled = v[0] != 0.f; break;
        case Prop::WindowCanDock:   s.windowCanDock = v[0] != 0.f; break;
        case Prop::WindowChildDock: s.windowChildDock = v[0] != 0.f; break;
        case Prop::Opacity:         s.opacity = std::clamp(v[0], 0.f, 1.f); break;
        case Prop::TransitionSpeed: s.transitionSpeed = v[0]; break;
        case Prop::Padding:         s.padding = Edges(v[0], v[1], v[2], v[3]); break;
        case Prop::BackgroundSize:
            s.backgroundFit = static_cast<BackgroundFit>(static_cast<uint8_t>(v[0]));
            break;
        case Prop::BackgroundPosition:
            s.backgroundPosition = {v[0], v[1]};
            break;
        case Prop::BackgroundGradient:
            s.backgroundGradient = {};
            if (!d.text.empty()) ParseLinearGradient(d.text, s.backgroundGradient);
            break;
        case Prop::TextGradient:
        case Prop::ForegroundGradient:
            if (d.text.empty()) s.textGradient = {};
            else ParseLinearGradient(d.text, s.textGradient);
            break;
        case Prop::AccentGradient:
            if (d.text.empty()) s.accentGradient = {};
            else ParseLinearGradient(d.text, s.accentGradient);
            break;
        case Prop::BorderColorGradient:
            if (d.text.empty()) s.borderColorGradient = {};
            else ParseLinearGradient(d.text, s.borderColorGradient);
            break;
        case Prop::ShadowColorGradient:
            if (d.text.empty()) s.shadowColorGradient = {};
            else ParseLinearGradient(d.text, s.shadowColorGradient);
            break;
        case Prop::OutlineColorGradient:
            s.outlineColor = {};
            s.outlineColorSet = true;
            if (d.text.empty()) s.outlineColorGradient = {};
            else ParseLinearGradient(d.text, s.outlineColorGradient);
            break;
        case Prop::BackgroundImage:
            break;
    }
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

std::string NormalizeImageSource(std::string_view source) {
    std::string normalized(Trim(source));
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

bool ParseNumber(std::string_view text, float& value) {
    text = Trim(text);
    if (text.size() >= 2 && EqualsIgnoreCase(text.substr(text.size() - 2), "px"))
        text = Trim(text.substr(0, text.size() - 2));
    std::string owned(text);
    char* end = nullptr;
    value = std::strtof(owned.c_str(), &end);
    return end != owned.c_str() && *end == '\0' && std::isfinite(value);
}

bool ParseDimension(std::string_view text, float& value, bool& percent) {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "auto")) {
        value = -1.f;
        percent = false;
        return true;
    }
    percent = !text.empty() && text.back() == '%';
    if (percent) text.remove_suffix(1);
    if (!ParseNumber(text, value) || value < 0.f) return false;
    if (percent) value /= 100.f;
    return true;
}

bool ParseColor(std::string_view text, Color& color) {
    text = Trim(text);
    const std::string lower = Lowercase(text);
    if (lower == "transparent") { color = {0.f, 0.f, 0.f, 0.f}; return true; }
    if (lower == "black") { color = Color::FromHex(0x000000FF); return true; }
    if (lower == "white") { color = Color::FromHex(0xFFFFFFFF); return true; }
    if (lower == "red") { color = Color::FromHex(0xFF0000FF); return true; }
    if (lower == "green") { color = Color::FromHex(0x008000FF); return true; }
    if (lower == "blue") { color = Color::FromHex(0x0000FFFF); return true; }
    if (lower == "yellow") { color = Color::FromHex(0xFFFF00FF); return true; }
    if (lower == "orange") { color = Color::FromHex(0xFFA500FF); return true; }
    if (lower == "purple") { color = Color::FromHex(0x800080FF); return true; }
    if (lower == "gray" || lower == "grey") { color = Color::FromHex(0x808080FF); return true; }
    if (lower.starts_with("rgb(") || lower.starts_with("rgba(")) {
        const bool hasAlpha = lower.starts_with("rgba(");
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
    if (lower.starts_with("hsl(") || lower.starts_with("hsla(")) {
        const bool hasAlpha = lower.starts_with("hsla(");
        const size_t count = hasAlpha ? 4u : 3u;
        const size_t open = text.find('(');
        if (text.back() != ')' || open == std::string_view::npos) return false;
        std::string_view parts = text.substr(open + 1, text.size() - open - 2);
        float channels[4]{0.f, 0.f, 0.f, 1.f};
        for (size_t i = 0; i < count; ++i) {
            const size_t comma = parts.find(',');
            std::string_view component = Trim(parts.substr(0, comma));
            if (i == 0 && component.ends_with("deg")) component.remove_suffix(3);
            const bool percent = !component.empty() && component.back() == '%';
            if (i > 0 && i < 3 && !percent) return false;
            if (percent) component.remove_suffix(1);
            if (!ParseNumber(component, channels[i])) return false;
            if (i == 0) channels[i] = std::fmod(channels[i], 360.f);
            else if (i < 3) channels[i] = std::clamp(channels[i] / 100.f, 0.f, 1.f);
            else if (percent) channels[i] /= 100.f;
            if (i + 1 < count) {
                if (comma == std::string_view::npos) return false;
                parts.remove_prefix(comma + 1);
            } else if (comma != std::string_view::npos) {
                return false;
            } else {
                parts = {};
            }
        }
        if (!Trim(parts).empty()) return false;
        float hue = channels[0] / 360.f;
        if (hue < 0.f) hue += 1.f;
        const float saturation = channels[1], lightness = channels[2];
        const float chroma = (1.f - std::abs(2.f * lightness - 1.f)) * saturation;
        const float hueSector = hue * 6.f;
        const float x = chroma * (1.f - std::abs(std::fmod(hueSector, 2.f) - 1.f));
        Color rgb;
        if (hueSector < 1.f) rgb = {chroma, x, 0.f, 1.f};
        else if (hueSector < 2.f) rgb = {x, chroma, 0.f, 1.f};
        else if (hueSector < 3.f) rgb = {0.f, chroma, x, 1.f};
        else if (hueSector < 4.f) rgb = {0.f, x, chroma, 1.f};
        else if (hueSector < 5.f) rgb = {x, 0.f, chroma, 1.f};
        else rgb = {chroma, 0.f, x, 1.f};
        const float match = lightness - chroma * 0.5f;
        color = {rgb.r + match, rgb.g + match, rgb.b + match,
                 std::clamp(channels[3], 0.f, 1.f)};
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

bool ParseLinearGradient(std::string_view text, LinearGradient& gradient) {
    text = Trim(text);
    const std::string lower = Lowercase(text);
    size_t prefixLength = 0;
    if (lower.starts_with("linear-gradient(")) prefixLength = 16;
    else if (lower.starts_with("gradient(")) prefixLength = 9;
    else return false;
    if (text.size() <= prefixLength || text.back() != ')') return false;
    std::string_view body = Trim(text.substr(prefixLength, text.size() - prefixLength - 1));
    std::vector<std::string_view> parts;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '(') ++depth;
        else if (body[i] == ')') --depth;
        else if (body[i] == ',' && depth == 0) {
            parts.push_back(Trim(body.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(Trim(body.substr(start)));
    if (parts.size() < 2) return false;

    LinearGradient parsed;
    auto angle = [](std::string_view token, float& degrees) {
        const std::string lowerToken = Lowercase(token);
        if (lowerToken.starts_with("to ")) {
            const std::string_view direction = std::string_view(lowerToken).substr(3);
            if (direction == "top") degrees = 0.f;
            else if (direction == "right") degrees = 90.f;
            else if (direction == "bottom") degrees = 180.f;
            else if (direction == "left") degrees = 270.f;
            else if (direction == "top right" || direction == "right top") degrees = 45.f;
            else if (direction == "bottom right" || direction == "right bottom") degrees = 135.f;
            else if (direction == "bottom left" || direction == "left bottom") degrees = 225.f;
            else if (direction == "top left" || direction == "left top") degrees = 315.f;
            else return false;
            return true;
        }
        if (!lowerToken.ends_with("deg")) return false;
        token.remove_suffix(3);
        return ParseNumber(token, degrees);
    };

    float degrees = 0.f;
    size_t firstStop = 0;
    if (angle(parts.front(), degrees)) {
        parsed.angleDegrees = std::fmod(degrees, 360.f);
        if (parsed.angleDegrees < 0.f) parsed.angleDegrees += 360.f;
        firstStop = 1;
    }
    if (parts.size() - firstStop < 2) return false;

    std::vector<float> positions;
    for (size_t i = firstStop; i < parts.size(); ++i) {
        std::string_view stop = parts[i];
        float position = std::numeric_limits<float>::quiet_NaN();
        if (!stop.empty() && stop.back() == '%') {
            size_t whitespace = stop.find_last_of(" \t\r\n");
            if (whitespace != std::string_view::npos) {
                std::string_view percent = stop.substr(whitespace + 1);
                percent.remove_suffix(1);
                if (!ParseNumber(percent, position)) return false;
                position = std::clamp(position / 100.f, 0.f, 1.f);
                stop = Trim(stop.substr(0, whitespace));
            }
        }
        Color color;
        if (!ParseColor(stop, color)) return false;
        parsed.stops.push_back({color, 0.f});
        positions.push_back(position);
    }
    if (std::isnan(positions.front())) positions.front() = 0.f;
    if (std::isnan(positions.back())) positions.back() = 1.f;
    for (size_t left = 0; left + 1 < positions.size();) {
        size_t right = left + 1;
        while (right < positions.size() && std::isnan(positions[right])) ++right;
        if (right == positions.size()) return false;
        if (positions[right] < positions[left]) return false;
        const float span = positions[right] - positions[left];
        const size_t count = right - left;
        for (size_t i = 1; i < count; ++i)
            positions[left + i] = positions[left] + span *
                (static_cast<float>(i) / static_cast<float>(count));
        left = right;
    }
    for (size_t i = 0; i < parsed.stops.size(); ++i)
        parsed.stops[i].position = positions[i];
    gradient = std::move(parsed);
    return true;
}

bool ParseDeclaration(std::string_view name, std::string_view value, StyleDecl& out) {
    name = Trim(name);
    value = Trim(value);
    auto color = [&](Prop p) {
        Color c;
        if (!ParseColor(value, c)) return false;
        out.prop = p;
        out.value = {{c.r, c.g, c.b, c.a}};
        return true;
    };
    auto colorOrGradient = [&](Prop colorProp, Prop gradientProp) {
        if (color(colorProp)) return true;
        LinearGradient parsed;
        if (!ParseLinearGradient(value, parsed)) return false;
        out.prop = gradientProp;
        out.text = std::string(value);
        return true;
    };
    auto number = [&](Prop p) {
        float v = 0.f;
        if (!ParseNumber(value, v) || v < 0.f) return false;
        if (p == Prop::Opacity) v = std::clamp(v, 0.f, 1.f);
        out.prop = p;
        out.value = {{v, 0.f, 0.f, 0.f}};
        return true;
    };
    if (name == "background" || name == "background-color") {
        if (name == "background" && value == "none") {
            out.prop = Prop::BackgroundReset;
            out.value = {{0.f, 0.f, 0.f, 0.f}};
            return true;
        }
        return colorOrGradient(name == "background" ? Prop::BackgroundReset : Prop::Background,
                               Prop::BackgroundGradient);
    }
    if (name == "background-gradient") {
        if (value == "none") {
            out.prop = Prop::BackgroundGradient;
            return true;
        }
        LinearGradient parsed;
        if (!ParseLinearGradient(value, parsed)) return false;
        out.prop = Prop::BackgroundGradient;
        out.text = std::string(value);
        return true;
    }
    if (name == "color" || name == "text-color" ||
        name == "foreground" || name == "foreground-color")
        return colorOrGradient(Prop::Foreground, Prop::ForegroundGradient);
    if (name == "accent") return colorOrGradient(Prop::Accent, Prop::AccentGradient);
    if (name == "border-color")
        return colorOrGradient(Prop::BorderColor, Prop::BorderColorGradient);
    if (name == "shadow-color")
        return colorOrGradient(Prop::ShadowColor, Prop::ShadowColorGradient);
    if (name == "outline-color")
        return colorOrGradient(Prop::OutlineColor, Prop::OutlineColorGradient);
    if (name == "border-width") return number(Prop::BorderWidth);
    if (name == "outline-width") return number(Prop::OutlineWidth);
    if (name == "outline-style") {
        if (value != "none" && value != "solid") return false;
        out.prop = Prop::OutlineStyle;
        out.value = {{value == "solid" ? 1.f : 0.f, 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "border-radius" || name == "rounding") return number(Prop::Rounding);
    if (name == "border-radius-top-left") return number(Prop::RoundingTopLeft);
    if (name == "border-radius-top-right") return number(Prop::RoundingTopRight);
    if (name == "border-radius-bottom-right") return number(Prop::RoundingBottomRight);
    if (name == "border-radius-bottom-left") return number(Prop::RoundingBottomLeft);
    if (name == "font-size") return number(Prop::FontSize);
    if (name == "width" || name == "height") {
        float dimension = -1.f;
        bool percent = false;
        if (!ParseDimension(value, dimension, percent)) return false;
        out.prop = name == "width" ? Prop::Width : Prop::Height;
        out.value = {{dimension, percent ? 1.f : 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "align" || name == "alignment") {
        HorizontalAlignment horizontal = HorizontalAlignment::Auto;
        VerticalAlignment vertical = VerticalAlignment::Auto;
        if (value == "left" || value == "start") horizontal = HorizontalAlignment::Start;
        else if (value == "right" || value == "end") horizontal = HorizontalAlignment::End;
        else if (value == "center") {
            horizontal = HorizontalAlignment::Center;
            vertical = VerticalAlignment::Center;
        } else if (value == "top") vertical = VerticalAlignment::Start;
        else if (value == "middle") vertical = VerticalAlignment::Center;
        else if (value == "bottom") vertical = VerticalAlignment::End;
        else if (value != "auto") return false;
        out.prop = Prop::Alignment;
        out.value = {{static_cast<float>(horizontal), static_cast<float>(vertical), 0.f, 0.f}};
        return true;
    }
    if (name == "justify-self") {
        HorizontalAlignment alignment;
        if (value == "start" || value == "left" || value == "stretch")
            alignment = HorizontalAlignment::Start;
        else if (value == "center") alignment = HorizontalAlignment::Center;
        else if (value == "end" || value == "right")
            alignment = HorizontalAlignment::End;
        else if (value == "auto") alignment = HorizontalAlignment::Auto;
        else return false;
        out.prop = Prop::JustifySelf;
        out.value = {{static_cast<float>(alignment), 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "align-self") {
        VerticalAlignment alignment;
        if (value == "start" || value == "top" || value == "stretch")
            alignment = VerticalAlignment::Start;
        else if (value == "center" || value == "middle")
            alignment = VerticalAlignment::Center;
        else if (value == "end" || value == "bottom")
            alignment = VerticalAlignment::End;
        else if (value == "auto") alignment = VerticalAlignment::Auto;
        else return false;
        out.prop = Prop::AlignSelf;
        out.value = {{static_cast<float>(alignment), 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "text-align") {
        TextAlignment alignment;
        if (value == "left" || value == "start") alignment = TextAlignment::Start;
        else if (value == "center") alignment = TextAlignment::Center;
        else if (value == "right" || value == "end") alignment = TextAlignment::End;
        else return false;
        out.prop = Prop::TextAlign;
        out.value = {{static_cast<float>(alignment), 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "vertical-align") {
        VerticalAlignment alignment;
        if (value == "top" || value == "text-top" || value == "baseline")
            alignment = VerticalAlignment::Start;
        else if (value == "middle" || value == "center")
            alignment = VerticalAlignment::Center;
        else if (value == "bottom" || value == "text-bottom")
            alignment = VerticalAlignment::End;
        else return false;
        out.prop = Prop::VerticalAlign;
        out.value = {{static_cast<float>(alignment), 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "gap" || name == "spacing") return number(Prop::Spacing);
    if (name == "shadow-blur") return number(Prop::ShadowBlur);
    if (name == "shadow-enabled") {
        if (value != "true" && value != "false") return false;
        out.prop = Prop::ShadowEnabled;
        out.value = {{value == "true" ? 1.f : 0.f, 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "window-can-dock" || name == "window-child-dock") {
        if (value != "true" && value != "false") return false;
        out.prop = name == "window-can-dock" ? Prop::WindowCanDock : Prop::WindowChildDock;
        out.value = {{value == "true" ? 1.f : 0.f, 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "outline") {
        if (value == "none") {
            out.prop = Prop::Outline;
            out.value = {{0.f, 0.f, 0.f, 0.f}};
            out.text = "none";
            return true;
        }
        float zero = 0.f;
        if (ParseNumber(value, zero) && zero == 0.f) {
            out.prop = Prop::Outline;
            out.value = {{0.f, 0.f, 0.f, 0.f}};
            out.text = "none";
            return true;
        }
        std::string_view remaining = value;
        bool hasWidth = false, hasStyle = false, hasColor = false;
        float width = 0.f;
        std::string colorText;
        while (!(remaining = Trim(remaining)).empty()) {
            size_t split = 0;
            int depth = 0;
            for (; split < remaining.size(); ++split) {
                if (remaining[split] == '(') ++depth;
                else if (remaining[split] == ')') --depth;
                else if (depth == 0 &&
                         std::isspace(static_cast<unsigned char>(remaining[split])))
                    break;
            }
            const std::string_view token = remaining.substr(0, split);
            remaining = split == remaining.size() ? std::string_view{}
                                                   : remaining.substr(split);
            float parsedWidth = 0.f;
            if (token == "solid" && !hasStyle) {
                hasStyle = true;
            } else if (ParseNumber(token, parsedWidth) && !hasWidth &&
                       parsedWidth >= 0.f) {
                width = parsedWidth;
                hasWidth = true;
            } else {
                Color parsedColor;
                LinearGradient parsedGradient;
                if (hasColor || (!ParseColor(token, parsedColor) &&
                                 !ParseLinearGradient(token, parsedGradient)))
                    return false;
                colorText = std::string(token);
                hasColor = true;
            }
        }
        if (!hasWidth || !hasStyle || !hasColor) return false;
        out.prop = Prop::Outline;
        out.value = {{width, 1.f, 0.f, 0.f}};
        out.text = std::move(colorText);
        return true;
    }
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
        out.prop = Prop::Padding;
        out.value = {{top, right, bottom, left}};
        return true;
    }
    if (name == "background-image") {
        if (value == "none") {
            out.prop = Prop::BackgroundImage;
            out.text.clear();
            return true;
        }
        LinearGradient parsed;
        if (ParseLinearGradient(value, parsed)) {
            out.prop = Prop::BackgroundGradient;
            out.text = std::string(value);
            return true;
        }
        if (!value.starts_with("url(") || value.back() != ')') return false;
        std::string_view source = Trim(value.substr(4, value.size() - 5));
        if (source.size() >= 2 &&
            ((source.front() == '"' && source.back() == '"') ||
             (source.front() == '\'' && source.back() == '\''))) {
            source.remove_prefix(1);
            source.remove_suffix(1);
        }
        if (source.empty()) return false;
        out.prop = Prop::BackgroundImage;
        out.text = NormalizeImageSource(source);
        return true;
    }
    if (name == "text-gradient" || name == "color-gradient" ||
        name == "foreground-gradient") {
        if (value == "none") {
            out.prop = Prop::TextGradient;
            out.text.clear();
            return true;
        }
        LinearGradient parsed;
        if (!ParseLinearGradient(value, parsed)) return false;
        out.prop = Prop::ForegroundGradient;
        out.text = std::string(value);
        return true;
    }
    if (name == "accent-gradient" || name == "border-color-gradient" ||
        name == "shadow-color-gradient") {
        if (value == "none") {
            out.prop = name == "accent-gradient" ? Prop::AccentGradient
                : name == "border-color-gradient" ? Prop::BorderColorGradient
                : Prop::ShadowColorGradient;
            out.text.clear();
            return true;
        }
        LinearGradient parsed;
        if (!ParseLinearGradient(value, parsed)) return false;
        out.prop = name == "accent-gradient" ? Prop::AccentGradient
            : name == "border-color-gradient" ? Prop::BorderColorGradient
            : Prop::ShadowColorGradient;
        out.text = std::string(value);
        return true;
    }
    if (name == "background-size") {
        BackgroundFit fit;
        if (value == "cover") fit = BackgroundFit::Cover;
        else if (value == "contain") fit = BackgroundFit::Contain;
        else if (value == "stretch" || value == "auto" || value == "100% 100%")
            fit = BackgroundFit::Stretch;
        else return false;
        out.prop = Prop::BackgroundSize;
        out.value = {{static_cast<float>(fit), 0.f, 0.f, 0.f}};
        return true;
    }
    if (name == "background-position") {
        auto parseAxis = [](std::string_view token, bool horizontal, float& position) {
            if (token == "center") { position = 0.5f; return true; }
            if (token == "left" || token == "top") {
                if ((horizontal && token == "left") || (!horizontal && token == "top")) {
                    position = 0.f;
                    return true;
                }
                return false;
            }
            if (token == "right" || token == "bottom") {
                if ((horizontal && token == "right") || (!horizontal && token == "bottom")) {
                    position = 1.f;
                    return true;
                }
                return false;
            }
            if (!token.empty() && token.back() == '%') {
                token.remove_suffix(1);
                float percent = 0.f;
                if (!ParseNumber(token, percent) || percent < 0.f || percent > 100.f) return false;
                position = percent / 100.f;
                return true;
            }
            return false;
        };
        std::string_view tokens[2]{};
        size_t count = 0;
        while (!value.empty() && count < 2) {
            const size_t split = value.find_first_of(" \t\r\n");
            tokens[count++] = Trim(value.substr(0, split));
            value = split == std::string_view::npos ? std::string_view{} : Trim(value.substr(split));
        }
        if (!value.empty() || count == 0) return false;
        float x = 0.5f, y = 0.5f;
        if (count == 1) {
            if (tokens[0] == "top" || tokens[0] == "bottom") {
                if (!parseAxis(tokens[0], false, y)) return false;
            } else if (!parseAxis(tokens[0], true, x)) {
                return false;
            }
        } else if (!parseAxis(tokens[0], true, x) || !parseAxis(tokens[1], false, y)) {
            if (!parseAxis(tokens[0], false, y) || !parseAxis(tokens[1], true, x))
                return false;
        }
        out.prop = Prop::BackgroundPosition;
        out.value = {{x, y, 0.f, 0.f}};
        return true;
    }
    return false;
}

bool ResolveVariables(std::string_view source,
                      const std::unordered_map<std::string, std::string>& variables,
                      std::vector<std::string>& resolving, std::string& resolved) {
    resolved.clear();
    size_t position = 0;
    while (position < source.size()) {
        const size_t variableStart = source.find("var(", position);
        if (variableStart == std::string_view::npos) {
            resolved.append(source.substr(position));
            return true;
        }
        resolved.append(source.substr(position, variableStart - position));

        size_t close = variableStart + 4;
        size_t depth = 1;
        for (; close < source.size() && depth > 0; ++close) {
            if (source[close] == '(') ++depth;
            else if (source[close] == ')') --depth;
        }
        if (depth != 0) return false;
        --close;

        const std::string_view expression =
            source.substr(variableStart + 4, close - variableStart - 4);
        size_t comma = std::string_view::npos;
        depth = 0;
        for (size_t i = 0; i < expression.size(); ++i) {
            if (expression[i] == '(') ++depth;
            else if (expression[i] == ')') --depth;
            else if (expression[i] == ',' && depth == 0) {
                comma = i;
                break;
            }
        }
        const std::string key(Trim(expression.substr(0, comma)));
        const auto value = variables.find(key);
        const bool cyclic = std::find(resolving.begin(), resolving.end(), key) !=
                            resolving.end();
        std::string replacement;
        bool valid = false;
        if (value != variables.end() && !cyclic) {
            resolving.push_back(key);
            valid = ResolveVariables(value->second, variables, resolving, replacement);
            resolving.pop_back();
        }
        if (!valid && comma != std::string_view::npos)
            valid = ResolveVariables(Trim(expression.substr(comma + 1)),
                                     variables, resolving, replacement);
        if (!valid) return false;

        resolved.append(replacement);
        position = close + 1;
    }
    return true;
}

bool ResolveVariables(std::string_view source,
                      const std::unordered_map<std::string, std::string>& variables,
                      std::string& resolved) {
    std::vector<std::string> resolving;
    return ResolveVariables(source, variables, resolving, resolved);
}

using CssBlock = std::pair<std::string_view, std::string_view>;

bool StripComments(std::string_view source, std::string& output) {
    output.clear();
    char quote = 0;
    bool escaped = false;
    for (size_t i = 0; i < source.size(); ++i) {
        const char ch = source[i];
        if (quote != 0) {
            output.push_back(ch);
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            output.push_back(ch);
            continue;
        }
        if (ch == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            const size_t end = source.find("*/", i + 2);
            if (end == std::string_view::npos) return false;
            output.push_back(' ');
            i = end + 1;
            continue;
        }
        output.push_back(ch);
    }
    return true;
}

size_t FindTopLevel(std::string_view source, char delimiter) {
    char quote = 0;
    bool escaped = false;
    int parentheses = 0;
    int brackets = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        const char ch = source[i];
        if (quote != 0) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') quote = ch;
        else if (ch == '\\' && i + 1 < source.size()) ++i;
        else if (ch == '(') ++parentheses;
        else if (ch == ')' && parentheses > 0) --parentheses;
        else if (ch == '[') ++brackets;
        else if (ch == ']' && brackets > 0) --brackets;
        else if (ch == delimiter && parentheses == 0 && brackets == 0) return i;
    }
    return std::string_view::npos;
}

size_t FindMatchingBrace(std::string_view source, size_t open) {
    char quote = 0;
    bool escaped = false;
    int parentheses = 0;
    int brackets = 0;
    int nested = 0;
    for (size_t i = open + 1; i < source.size(); ++i) {
        const char ch = source[i];
        if (quote != 0) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') quote = ch;
        else if (ch == '\\' && i + 1 < source.size()) ++i;
        else if (ch == '(') ++parentheses;
        else if (ch == ')' && parentheses > 0) --parentheses;
        else if (ch == '[') ++brackets;
        else if (ch == ']' && brackets > 0) --brackets;
        else if (parentheses == 0 && brackets == 0 && ch == '{') ++nested;
        else if (parentheses == 0 && brackets == 0 && ch == '}') {
            if (nested == 0) return i;
            --nested;
        }
    }
    return std::string_view::npos;
}

bool ParseCssBlocks(std::string_view source, std::vector<CssBlock>& blocks) {
    blocks.clear();
    while (!(source = Trim(source)).empty()) {
        if (source.front() == '@') {
            const size_t open = FindTopLevel(source, '{');
            const size_t semicolon = FindTopLevel(source, ';');
            if (semicolon != std::string_view::npos &&
                (open == std::string_view::npos || semicolon < open)) {
                source.remove_prefix(semicolon + 1);
                continue;
            }
            if (open == std::string_view::npos) return false;
            const size_t close = FindMatchingBrace(source, open);
            if (close == std::string_view::npos) return false;
            source.remove_prefix(close + 1);
            continue;
        }

        const size_t open = FindTopLevel(source, '{');
        if (open == std::string_view::npos) return false;
        const size_t close = FindMatchingBrace(source, open);
        if (close == std::string_view::npos) return false;
        blocks.emplace_back(Trim(source.substr(0, open)),
                            source.substr(open + 1, close - open - 1));
        source.remove_prefix(close + 1);
    }
    return true;
}

std::vector<std::string_view> SplitSelectors(std::string_view selectors) {
    std::vector<std::string_view> result;
    while (!(selectors = Trim(selectors)).empty()) {
        const size_t comma = FindTopLevel(selectors, ',');
        result.push_back(Trim(selectors.substr(0, comma)));
        if (comma == std::string_view::npos) break;
        selectors.remove_prefix(comma + 1);
    }
    return result;
}

void CollectVariables(const std::vector<CssBlock>& blocks,
                      std::unordered_map<std::string, std::string>& variables) {
    for (const CssBlock& block : blocks) {
        bool global = false;
        for (std::string_view selector : SplitSelectors(block.first))
            global = global || selector == ":root" || selector == "*:root" ||
                     selector == "*";
        std::string_view declarations = block.second;
        while (!Trim(declarations).empty()) {
            const size_t semi = FindTopLevel(declarations, ';');
            const std::string_view declaration = Trim(declarations.substr(0, semi));
            const size_t colon = FindTopLevel(declaration, ':');
            if (global && colon != std::string_view::npos) {
                const std::string_view name = Trim(declaration.substr(0, colon));
                if (name.starts_with("--")) {
                    variables[std::string(name)] = std::string(Trim(declaration.substr(colon + 1)));
                }
            }
            if (semi == std::string_view::npos) break;
            declarations.remove_prefix(semi + 1);
        }
    }
}

} // namespace

StyleSheet::RuleBuilder StyleSheet::Rule(std::string_view selector) {
    StyleRule r;
    ParseSelector(selector, r.widget, r.id, r.classes, r.stateMask);
    rules_.push_back(std::move(r));
    cache_.clear();
    ++version_;
    return RuleBuilder(this, rules_.size() - 1);
}

void StyleSheet::SetProp(size_t ruleIndex, Prop p, StyleValue v, std::string text,
                         bool important) {
    if (ruleIndex >= rules_.size()) return;
    auto& decls = rules_[ruleIndex].decls;
    auto it = std::find_if(decls.begin(), decls.end(), [&](const StyleDecl& d) { return d.prop == p; });
    if (it != decls.end()) {
        if (it->important && !important) return;
        it->value = v;
        it->text = std::move(text);
        it->important = important;
    } else {
        decls.push_back({p, v, std::move(text), important});
    }
    cache_.clear();
    ++version_;
}

void StyleSheet::RegisterImage(std::string_view source, GPUImageId image, Vec2 size) {
    const std::string key = NormalizeImageSource(source);
    if (image == 0) {
        images_.erase(key);
    } else {
        images_[key] = {image, size};
    }
    cache_.clear();
    ++version_;
}

void StyleSheet::Clear() {
    rules_.clear();
    variables_.clear();
    cache_.clear();
    ++version_;
}

bool StyleSheet::LoadCSS(std::string_view css, std::string* error) {
    std::string source;
    if (!StripComments(css, source)) {
        if (error) *error = "Unterminated CSS comment";
        return false;
    }
    std::vector<CssBlock> blocks;
    if (!ParseCssBlocks(source, blocks)) {
        if (error) *error = "Malformed CSS rule";
        return false;
    }
    std::unordered_map<std::string, std::string> variables = variables_;
    CollectVariables(blocks, variables);
    StyleSheet candidate = *this;
    candidate.rules_.erase(
        std::remove_if(candidate.rules_.begin(), candidate.rules_.end(),
                       [](const StyleRule& rule) { return rule.fromCSS; }),
        candidate.rules_.end());
    candidate.cache_.clear();
    ++candidate.version_;
    for (const CssBlock& block : blocks) {
        std::vector<StyleDecl> declarations;
        std::string_view body = block.second;
        while (!Trim(body).empty()) {
            const size_t semi = FindTopLevel(body, ';');
            const std::string_view decl = Trim(body.substr(0, semi));
            const size_t colon = FindTopLevel(decl, ':');
            if (!decl.empty() && colon != std::string_view::npos) {
                std::string name(Trim(decl.substr(0, colon)));
                if (!name.starts_with("--")) {
                    std::transform(name.begin(), name.end(), name.begin(),
                        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                }
                const std::string_view property = name;
                const bool supported = IsSupportedDeclaration(property);
                std::string value;
                bool important = false;
                const bool customProperty = name.starts_with("--");
                if (!customProperty && supported &&
                    ResolveVariables(decl.substr(colon + 1), variables, value)) {
                    const size_t bang = value.rfind('!');
                    if (bang != std::string::npos &&
                        EqualsIgnoreCase(Trim(value.substr(bang)), "!important")) {
                        important = true;
                        value = std::string(Trim(std::string_view(value).substr(0, bang)));
                    }
                    std::vector<StyleDecl> parsed;
                    if (property == "border") {
                        std::string_view parts = Trim(value);
                        bool valid = true, foundWidth = false, foundColor = false;
                        while (!(parts = Trim(parts)).empty()) {
                            size_t split = 0;
                            int depth = 0;
                            for (; split < parts.size(); ++split) {
                                if (parts[split] == '(') ++depth;
                                else if (parts[split] == ')') --depth;
                                else if (depth == 0 &&
                                         std::isspace(static_cast<unsigned char>(parts[split])))
                                    break;
                            }
                            const std::string_view token = parts.substr(0, split);
                            if (token == "solid") {
                            } else if (token == "none") {
                                parsed.push_back({Prop::BorderWidth, StyleValue{{0.f, 0.f, 0.f, 0.f}}});
                                foundWidth = true;
                            } else if (Color color; ParseColor(token, color)) {
                                parsed.push_back({Prop::BorderColor,
                                                  StyleValue{{color.r, color.g, color.b, color.a}}});
                                foundColor = true;
                            } else if (LinearGradient gradient;
                                       ParseLinearGradient(token, gradient)) {
                                parsed.push_back({Prop::BorderColorGradient, {}, std::string(token)});
                                foundColor = true;
                            } else {
                                float width = 0.f;
                                if (!ParseNumber(token, width) || width < 0.f) {
                                    valid = false;
                                    break;
                                }
                                parsed.push_back({Prop::BorderWidth, StyleValue{{width, 0.f, 0.f, 0.f}}});
                                foundWidth = true;
                            }
                            parts = split == parts.size() ? std::string_view{}
                                                          : parts.substr(split + 1);
                        }
                        if (!valid || (!foundWidth && !foundColor)) parsed.clear();
                    } else {
                        StyleDecl parsedValue{};
                        if (ParseDeclaration(property, value, parsedValue))
                            parsed.push_back(std::move(parsedValue));
                    }
                    for (StyleDecl& parsedDeclaration : parsed)
                        parsedDeclaration.important = important;
                    declarations.insert(declarations.end(), parsed.begin(), parsed.end());
                }
            }
            if (semi == std::string_view::npos) break;
            body.remove_prefix(semi + 1);
        }
        if (declarations.empty()) continue;
        for (const std::string_view selector : SplitSelectors(block.first)) {
            if (!ValidSelector(selector)) continue;
            auto rule = candidate.Rule(selector);
            for (const auto& declaration : declarations)
                rule.Set(declaration.prop, declaration.value, declaration.text,
                         declaration.important);
            candidate.rules_.back().fromCSS = true;
        }
    }
    candidate.variables_ = std::move(variables);
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}

bool StyleSheet::LoadCSSFile(const std::string& path, std::string* error) {
    return LoadCSSFile(std::filesystem::path(path), error);
}

bool StyleSheet::LoadCSSFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "Could not open stylesheet: " + path.string();
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        if (error) *error = "Could not read stylesheet: " + path.string();
        return false;
    }
    return LoadCSS(contents.str(), error);
}

ComputedStyle StyleSheet::Resolve(std::string_view widget,
                                  const std::vector<std::string>& classes,
                                  uint8_t state, std::string_view id) const {
    uint32_t key = HashStr(widget);
    for (const auto& c : classes) key = HashStr(c, key ^ 0x9E3779B9u);
    key = HashStr(id, key ^ 0x85EBCA6Bu);
    key = key * 31u + state;
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    std::vector<std::pair<int, const StyleRule*>> hits;
    for (const StyleRule& r : rules_) {
        if (!r.widget.empty() && r.widget != widget) continue;
        if (!r.id.empty() && r.id != id) continue;
        if (!std::all_of(r.classes.begin(), r.classes.end(), [&](const std::string& cls) {
                return std::find(classes.begin(), classes.end(), cls) != classes.end();
            })) continue;
        if ((r.stateMask & ~static_cast<unsigned>(state)) != 0) continue;
        const int spec = (!r.id.empty() ? 100 : 0) +
                         static_cast<int>(r.classes.size()) * 10 +
                         10 * std::popcount(static_cast<unsigned>(r.stateMask)) +
                         (r.widget.empty() ? 0 : 1);
        hits.emplace_back(spec, &r);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        if (a.second->builtIn != b.second->builtIn) return a.second->builtIn;
        return a.first < b.first;
    });

    ComputedStyle out;
    for (bool important : {false, true}) {
        for (const auto& h : hits) {
            for (const StyleDecl& d : h.second->decls) {
                if (d.important != important) continue;
                if (d.prop == Prop::BackgroundImage) {
                    if (d.text.empty()) {
                        out.backgroundImage = 0;
                        out.backgroundImageSize = {};
                    } else {
                        const auto image = images_.find(NormalizeImageSource(d.text));
                        if (image != images_.end()) {
                            out.backgroundImage = image->second.id;
                            out.backgroundImageSize = image->second.size;
                        } else {
                            out.backgroundImage = 0;
                            out.backgroundImageSize = {};
                        }
                    }
                } else {
                    Apply(out, d);
                }
            }
        }
    }

    cache_[key] = out;
    return out;
}

StyleSheet StyleSheet::Dark() {
    using C = Color;
    StyleSheet s;

    s.Rule("*:disabled").Opacity(0.55f);
    s.Rule("*").Foreground(C::FromHex(0xE7EDF7FF)).FontSize(15).Spacing(8).TransitionSpeed(16);

    s.Rule("window")
        .Background(C::FromHex(0x101722F8)).BorderColor(C::FromHex(0x334155FF)).BorderWidth(1)
        .Rounding(12).Padding(Edges(12))
        .ShadowColor(C::FromHex(0x020617A0)).ShadowBlur(18);
    s.Rule("window-title")
        .Background(C::FromHex(0x1A2637FF)).Foreground(C::FromHex(0xF1F5FCFF))
        .Padding(Edges(7, 12)).FontSize(15);
    s.Rule("window-close")
        .Background({0.f, 0.f, 0.f, 0.f}).Foreground(C::FromHex(0xAEBBD0FF))
        .FontSize(16);
    s.Rule("window-close:hover")
        .Background({0.f, 0.f, 0.f, 0.f}).Foreground(C::FromHex(0xFFFFFFFF));
    s.Rule("main-menu-bar").Background(C::FromHex(0x151C28FF))
        .BorderColor(C::FromHex(0x30425CFF)).BorderWidth(1).Padding(Edges(4, 8));
    s.Rule("menu").Background({0.f, 0.f, 0.f, 0.f})
        .Foreground(C::FromHex(0xDCE6F5FF)).Padding(Edges(4, 10)).Rounding(5);
    s.Rule("menu:hover").Background(C::FromHex(0x2C3D54FF));
    s.Rule("menu:checked").Background(C::FromHex(0x2C3D54FF));
    s.Rule("menu-popup").Background(C::FromHex(0x182231FF))
        .BorderColor(C::FromHex(0x3A4B64FF)).BorderWidth(1).Rounding(8)
        .Padding(Edges(5)).ShadowColor(C::FromHex(0x00000088)).ShadowBlur(14);
    s.Rule("menu-item").Background({0.f, 0.f, 0.f, 0.f})
        .Foreground(C::FromHex(0xDCE6F5FF)).Padding(Edges(5, 10)).Rounding(4);
    s.Rule("menu-item:hover")
        .Background(C::FromHex(0x315A91FF)).Foreground(C::FromHex(0xFFFFFFFF));
    s.Rule("menu-item:checked")
        .Background(C::FromHex(0x315A91FF)).Foreground(C::FromHex(0xFFFFFFFF));
    s.Rule("context-menu").Background(C::FromHex(0x182231FF))
        .BorderColor(C::FromHex(0x3A4B64FF)).BorderWidth(1).Rounding(8)
        .Padding(Edges(5)).ShadowColor(C::FromHex(0x00000088)).ShadowBlur(14);
    s.Rule("context-menu-item").Background({0.f, 0.f, 0.f, 0.f})
        .Foreground(C::FromHex(0xDCE6F5FF)).Padding(Edges(5, 10)).Rounding(4);
    s.Rule("context-menu-item:hover")
        .Background(C::FromHex(0x315A91FF)).Foreground(C::FromHex(0xFFFFFFFF));
    s.Rule("hint").Background(C::FromHex(0x182231FF))
        .Foreground(C::FromHex(0xDCE6F5FF)).BorderColor(C::FromHex(0x3A4B64FF))
        .BorderWidth(1).Rounding(7).Padding(Edges(6, 10))
        .ShadowColor(C::FromHex(0x000000AA)).ShadowBlur(12);
    s.Rule("notification").Background(C::FromHex(0x182231FF))
        .Foreground(C::FromHex(0xF1F5FCFF)).BorderColor(C::FromHex(0x5A91F7FF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 14))
        .ShadowColor(C::FromHex(0x000000CC)).ShadowBlur(18);
    s.Rule("window-scrollbar")
        .Background(C::FromHex(0x263346FF)).Accent(C::FromHex(0x6885AFFF));
    s.Rule("container-scrollbar")
        .Background(C::FromHex(0x263346FF)).Accent(C::FromHex(0x6885AFFF));
    s.Rule("popup").Background(C::FromHex(0x182231FF))
        .BorderColor(C::FromHex(0x3A4B64FF)).BorderWidth(1).Rounding(8)
        .Padding(Edges(8)).ShadowColor(C::FromHex(0x000000AA)).ShadowBlur(14);
    s.Rule("input-text").Background(C::FromHex(0x0C121BFF))
        .Foreground(C::FromHex(0xF1F5FCFF)).BorderColor(C::FromHex(0x526783FF))
        .BorderWidth(1).Rounding(5).Padding(Edges(6, 8));
    s.Rule("input-text:active").BorderColor(C::FromHex(0x5A91F7FF));
    s.Rule("selectable").Foreground(C::FromHex(0xDCE6F5FF))
        .Padding(Edges(5, 8));
    s.Rule("selectable:hover").Background(C::FromHex(0x253449FF));
    s.Rule("selectable:checked").Background(C::FromHex(0x2F5E9AFF));
    s.Rule("tree-node").Foreground(C::FromHex(0xDCE6F5FF))
        .Padding(Edges(4, 8));
    s.Rule("tree-node:hover").Background(C::FromHex(0x253449FF));
    s.Rule("collapsing-header").Background(C::FromHex(0x1B2839FF))
        .Foreground(C::FromHex(0xF1F5FCFF)).Padding(Edges(6, 8));
    s.Rule("collapsing-header:hover").Background(C::FromHex(0x253449FF));
    s.Rule("separator").Foreground(C::FromHex(0x526783FF)).BorderWidth(1);
    s.Rule("dock-preview").Background(C::FromHex(0x4D83F7FF)).Accent(C::FromHex(0x73A5FFFF))
        .Rounding(14);
    s.Rule("dock-splitter").Background(C::FromHex(0x8999B0AA)).Rounding(2);
    s.Rule("dock-splitter:hover").Background(C::FromHex(0x5A91F7FF));
    s.Rule("dock-splitter:active").Background(C::FromHex(0x82B2FFFF));

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
    s.Rule("empty-widget").Background(C::FromHex(0x111A27FF))
        .BorderColor(C::FromHex(0x34445AFF)).BorderWidth(1).Rounding(8);
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
    s.Rule("dock-tab-bar").Background(C::FromHex(0x121A26FF))
        .Padding(Edges(2, 4)).Spacing(2);
    s.Rule("dock-tab").Background(C::FromHex(0x1B2737FF))
        .Foreground(C::FromHex(0xAEBBD0FF)).Padding(Edges(4, 10));
    s.Rule("dock-tab:hover").Background(C::FromHex(0x2A3A50FF));
    s.Rule("dock-tab:checked").Background(C::FromHex(0x2F5E9AFF))
        .Foreground(C::FromHex(0xFFFFFFFF));
    s.Rule("dropdown")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x3D526FFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 12));
    s.Rule("dropdown:hover").Background(C::FromHex(0x304762FF));
    s.Rule("progress-bar").Background(C::FromHex(0x111A27FF))
        .BorderColor(C::FromHex(0x34445AFF)).BorderWidth(1).Rounding(6)
        .Accent(C::FromHex(0x5A91F7FF)).Padding(Edges(2, 6));
    s.Rule("tile-container").Background(C::FromHex(0x111A27FF))
        .BorderColor(C::FromHex(0x34445AFF)).BorderWidth(1).Rounding(8)
        .Padding(Edges(6)).Spacing(6);
    s.Rule("tile").Background(C::FromHex(0x1B2839FF))
        .BorderColor(C::FromHex(0x344D6AFF)).BorderWidth(1).Rounding(7)
        .Accent(C::FromHex(0x5A91F7FF)).Padding(Edges(6));
    s.Rule("tile:hover").BorderColor(C::FromHex(0x7EA8E9FF));
    s.Rule("tile:checked").Background(C::FromHex(0x2F5E9AFF))
        .BorderColor(C::FromHex(0x6190E8FF));
    s.Rule("console-output")
        .Background(C::FromHex(0x000000FF)).BorderColor(C::FromHex(0x34445AFF))
        .BorderWidth(1).Rounding(5).Padding(Edges(6, 8));
    s.Rule("console-output-normal").Foreground(C::FromHex(0xE2E8F0FF));
    s.Rule("console-output-warning").Foreground(C::FromHex(0xFFC857FF));
    s.Rule("console-output-error").Foreground(C::FromHex(0xFF6B6BFF));
    s.Rule("console-input")
        .Background(C::FromHex(0x0C121BFF)).BorderColor(C::FromHex(0x526783FF))
        .BorderWidth(1).Rounding(5).Padding(Edges(6, 8));
    s.Rule("image-button")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x3D526FFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 12));
    s.Rule("image-button:hover").Background(C::FromHex(0x304762FF));
    s.Rule("interactive-image")
        .Background(C::FromHex(0x223147FF)).BorderColor(C::FromHex(0x3D526FFF))
        .BorderWidth(1).Rounding(8).Padding(Edges(8, 12));
    s.Rule("interactive-image:hover").Background(C::FromHex(0x304762FF));
    s.Rule("text-label").Spacing(7);
    s.Rule("image-label").Spacing(7);

    for (StyleRule& rule : s.rules_) rule.builtIn = true;
    return s;
}

} // namespace fluid