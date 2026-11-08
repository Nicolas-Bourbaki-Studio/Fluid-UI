#include "FluidUI/DrawList.h"

#include <algorithm>
#include <cmath>

namespace fluid {

namespace {

constexpr float kPi = 3.14159265358979f;

void Arc(std::vector<Vec2>& out, Vec2 c, float r, float a0, float a1, int seg) {
    for (int i = 0; i <= seg; ++i) {
        const float a = a0 + (a1 - a0) * (static_cast<float>(i) / static_cast<float>(seg));
        out.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

CornerRadii ExpandRadii(CornerRadii radii, float amount) {
    return {std::max(0.f, radii.topLeft + amount),
            std::max(0.f, radii.topRight + amount),
            std::max(0.f, radii.bottomRight + amount),
            std::max(0.f, radii.bottomLeft + amount)};
}

void BuildRoundedRect(std::vector<Vec2>& out, Rect r, CornerRadii radii) {
    out.clear();
    radii.topLeft = std::max(0.f, radii.topLeft);
    radii.topRight = std::max(0.f, radii.topRight);
    radii.bottomRight = std::max(0.f, radii.bottomRight);
    radii.bottomLeft = std::max(0.f, radii.bottomLeft);
    float scale = 1.f;
    auto constrain = [&](float extent, float sum) {
        if (sum > extent && sum > 0.f) scale = std::min(scale, extent / sum);
    };
    constrain(r.w, radii.topLeft + radii.topRight);
    constrain(r.w, radii.bottomLeft + radii.bottomRight);
    constrain(r.h, radii.topLeft + radii.bottomLeft);
    constrain(r.h, radii.topRight + radii.bottomRight);
    radii.topLeft *= scale;
    radii.topRight *= scale;
    radii.bottomRight *= scale;
    radii.bottomLeft *= scale;

    if (radii.topLeft < 0.5f && radii.topRight < 0.5f &&
        radii.bottomRight < 0.5f && radii.bottomLeft < 0.5f) {
        out = {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
        return;
    }
    auto addArc = [&](Vec2 center, float radius, float start, float end, Vec2 squareCorner) {
        if (radius < 0.5f) {
            out.push_back(squareCorner);
            return;
        }
        const int seg = std::clamp(static_cast<int>(radius * 0.6f) + 3, 4, 12);
        Arc(out, center, radius, start, end, seg);
    };
    addArc({r.x + r.w - radii.topRight, r.y + radii.topRight}, radii.topRight,
           -kPi * 0.5f, 0.f, {r.x + r.w, r.y});
    addArc({r.x + r.w - radii.bottomRight, r.y + r.h - radii.bottomRight},
           radii.bottomRight, 0.f, kPi * 0.5f, {r.x + r.w, r.y + r.h});
    addArc({r.x + radii.bottomLeft, r.y + r.h - radii.bottomLeft}, radii.bottomLeft,
           kPi * 0.5f, kPi, {r.x, r.y + r.h});
    addArc({r.x + radii.topLeft, r.y + radii.topLeft}, radii.topLeft,
           kPi, kPi * 1.5f, {r.x, r.y});
}

Color SampleGradient(const LinearGradient& gradient, Vec2 point, Rect bounds) {
    if (gradient.Empty()) return {};
    const float radians = gradient.angleDegrees * (kPi / 180.f);
    const Vec2 direction{std::sin(radians), -std::cos(radians)};
    const Vec2 center{bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f};
    const float halfExtent = std::abs(direction.x) * bounds.w * 0.5f +
                            std::abs(direction.y) * bounds.h * 0.5f;
    const float projection = (point.x - center.x) * direction.x +
                             (point.y - center.y) * direction.y;
    const float t = halfExtent > 0.f
        ? std::clamp((projection + halfExtent) / (halfExtent * 2.f), 0.f, 1.f)
        : 0.f;
    if (t <= gradient.stops.front().position) return gradient.stops.front().color;
    for (size_t i = 1; i < gradient.stops.size(); ++i) {
        const GradientStop& previous = gradient.stops[i - 1];
        const GradientStop& current = gradient.stops[i];
        if (t <= current.position) {
            const float span = current.position - previous.position;
            const float local = span > 0.f
                ? (t - previous.position) / span : 1.f;
            return Lerp(previous.color, current.color, local);
        }
    }
    return gradient.stops.back().color;
}

} // namespace

void DrawList::Clear() {
    vtx_.clear(); idx_.clear(); cmds_.clear(); clipStack_.clear();
    breakCommand_ = false;
}

Rect DrawList::CurrentClip() const {
    return clipStack_.empty() ? Rect{-16384.f, -16384.f, 32768.f, 32768.f} : clipStack_.back();
}

void DrawList::PushClipRect(Rect r) {
    clipStack_.push_back(clipStack_.empty() ? r : r.Intersect(clipStack_.back()));
}

void DrawList::PopClipRect() {
    if (!clipStack_.empty()) clipStack_.pop_back();
}

DrawList::Layer DrawList::BeginLayer() {
    breakCommand_ = true;
    return {vtx_.size(), idx_.size(), cmds_.size(), 0, 0, 0};
}

void DrawList::EndLayer(Layer& layer) {
    breakCommand_ = true;
    layer.vertexEnd = vtx_.size();
    layer.indexEnd = idx_.size();
    layer.commandEnd = cmds_.size();
}

void DrawList::BringLayerToFront(const Layer& layer) {
    if (layer.vertexStart > layer.vertexEnd || layer.vertexEnd > vtx_.size() ||
        layer.indexStart > layer.indexEnd || layer.indexEnd > idx_.size() ||
        layer.commandStart > layer.commandEnd || layer.commandEnd > cmds_.size()) return;

    const size_t vertexCount = layer.vertexEnd - layer.vertexStart;
    const size_t indexCount = layer.indexEnd - layer.indexStart;
    if (vertexCount == 0 || indexCount == 0) return;

    const size_t trailingVertices = vtx_.size() - layer.vertexEnd;
    const size_t trailingIndices = idx_.size() - layer.indexEnd;
    std::rotate(vtx_.begin() + static_cast<std::ptrdiff_t>(layer.vertexStart),
                vtx_.begin() + static_cast<std::ptrdiff_t>(layer.vertexEnd), vtx_.end());
    std::rotate(idx_.begin() + static_cast<std::ptrdiff_t>(layer.indexStart),
                idx_.begin() + static_cast<std::ptrdiff_t>(layer.indexEnd), idx_.end());
    std::rotate(cmds_.begin() + static_cast<std::ptrdiff_t>(layer.commandStart),
                cmds_.begin() + static_cast<std::ptrdiff_t>(layer.commandEnd), cmds_.end());

    const std::ptrdiff_t vertexShift = static_cast<std::ptrdiff_t>(trailingVertices);
    for (uint32_t& index : idx_) {
        const size_t oldIndex = index;
        if (oldIndex >= layer.vertexStart && oldIndex < layer.vertexEnd)
            index = static_cast<uint32_t>(static_cast<std::ptrdiff_t>(oldIndex) + vertexShift);
        else if (oldIndex >= layer.vertexEnd)
            index = static_cast<uint32_t>(oldIndex - vertexCount);
    }

    for (DrawCmd& command : cmds_) {
        if (command.idxOffset >= layer.indexStart && command.idxOffset < layer.indexEnd)
            command.idxOffset = static_cast<uint32_t>(command.idxOffset + trailingIndices);
        else if (command.idxOffset >= layer.indexEnd)
            command.idxOffset = static_cast<uint32_t>(command.idxOffset - indexCount);
    }
}

void DrawList::PrepareCmd(GPUImageId imageId) {
    const Rect clip = CurrentClip();
    if (breakCommand_ || cmds_.empty() ||
        !(cmds_.back().clip == clip) || cmds_.back().texture != imageId) {
        DrawCmd c;
        c.clip = clip;
        c.texture = imageId;
        c.idxOffset = static_cast<uint32_t>(idx_.size());
        cmds_.push_back(c);
    }
    breakCommand_ = false;
}

void DrawList::FinishPrimitive() {
    cmds_.back().idxCount = static_cast<uint32_t>(idx_.size()) - cmds_.back().idxOffset;
}

void DrawList::AddConvexFill(const Vec2* p, int n, Color c) {
    if (n < 3 || c.a <= 0.f) return;
    PrepareCmd(0);
    const uint32_t col = c.ToU32();
    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    for (int i = 0; i < n; ++i) vtx_.push_back({p[i], {0.f, 0.f}, col});
    for (int i = 1; i < n - 1; ++i) {
        idx_.push_back(base);
        idx_.push_back(base + i);
        idx_.push_back(base + i + 1);
    }
    FinishPrimitive();
}

void DrawList::AddRectFilled(Rect r, Color c, float rounding) {
    AddRectFilled(r, c, CornerRadii(rounding));
}

void DrawList::AddRectFilled(Rect r, Color c, CornerRadii rounding) {
    if (r.w <= 0.f || r.h <= 0.f) return;
    std::vector<Vec2> pts;
    BuildRoundedRect(pts, r, rounding);
    AddConvexFill(pts.data(), static_cast<int>(pts.size()), c);
}

void DrawList::AddRectFilledGradient(Rect r, const LinearGradient& gradient,
                                     float opacity, float rounding) {
    AddRectFilledGradient(r, gradient, opacity, CornerRadii(rounding));
}

void DrawList::AddRectFilledGradient(Rect r, const LinearGradient& gradient,
                                     float opacity, CornerRadii rounding) {
    if (r.w <= 0.f || r.h <= 0.f || gradient.Empty()) return;
    std::vector<Vec2> pts;
    BuildRoundedRect(pts, r, rounding);
    AddConvexFillGradient(pts.data(), static_cast<int>(pts.size()), gradient, r, opacity);
}

void DrawList::AddShadowGradient(Rect r, const LinearGradient& gradient, float opacity,
                                 float blur, float rounding) {
    AddShadowGradient(r, gradient, opacity, blur, CornerRadii(rounding));
}

void DrawList::AddShadowGradient(Rect r, const LinearGradient& gradient, float opacity,
                                 float blur, CornerRadii rounding) {
    if (blur <= 0.f || gradient.Empty()) return;
    constexpr int steps = 6;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i + 1) / steps;
        const float fade = (1.f - t) * 0.35f;
        AddRectFilledGradient(r.Expanded(blur * t), gradient, opacity * fade,
                              ExpandRadii(rounding, blur * t));
    }
}

void DrawList::AddBorderGradient(Rect r, const LinearGradient& gradient, float opacity,
                                 float borderWidth, float rounding) {
    AddBorderGradient(r, gradient, opacity, borderWidth, CornerRadii(rounding));
}

void DrawList::AddBorderGradient(Rect r, const LinearGradient& gradient, float opacity,
                                 float borderWidth, CornerRadii rounding) {
    if (borderWidth <= 0.f || gradient.Empty()) return;
    const float horizontalInset = std::min(borderWidth, r.w * 0.5f);
    const float verticalInset = std::min(borderWidth, r.h * 0.5f);
    AddRectFilledGradient({r.x, r.y, r.w, borderWidth}, gradient, opacity,
                          {rounding.topLeft, rounding.topRight, 0.f, 0.f});
    AddRectFilledGradient({r.x, r.y + r.h - borderWidth, r.w, borderWidth},
                          gradient, opacity,
                          {0.f, 0.f, rounding.bottomRight, rounding.bottomLeft});
    const float sideHeight = std::max(0.f, r.h - 2.f * borderWidth);
    AddRectFilledGradient({r.x, r.y + borderWidth, horizontalInset, sideHeight},
                          gradient, opacity);
    AddRectFilledGradient({r.x + r.w - horizontalInset, r.y + borderWidth,
                           horizontalInset, sideHeight}, gradient, opacity);
}

void DrawList::AddConvexFillGradient(const Vec2* points, int count,
                                    const LinearGradient& gradient, Rect bounds,
                                    float opacity) {
    if (count < 3 || gradient.Empty()) return;
    PrepareCmd(0);
    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    for (int i = 0; i < count; ++i) {
        Color color = SampleGradient(gradient, points[i], bounds);
        color.a *= opacity;
        vtx_.push_back({points[i], {0.f, 0.f}, color.ToU32()});
    }
    for (int i = 1; i < count - 1; ++i) {
        idx_.push_back(base);
        idx_.push_back(base + static_cast<uint32_t>(i));
        idx_.push_back(base + static_cast<uint32_t>(i + 1));
    }
    FinishPrimitive();
}

void DrawList::AddBox(Rect r, Color bg, Color border, float bw, float rounding) {
    AddBox(r, bg, border, bw, CornerRadii(rounding));
}

void DrawList::AddBox(Rect r, Color bg, Color border, float bw, CornerRadii rounding) {
    if (bw > 0.f && border.a > 0.f) {
        AddRectFilled(r, border, rounding);
        AddRectFilled({r.x + bw, r.y + bw, r.w - 2 * bw, r.h - 2 * bw},
                      bg, ExpandRadii(rounding, -bw));
    } else {
        AddRectFilled(r, bg, rounding);
    }
}

void DrawList::AddShadow(Rect r, Color c, float blur, float rounding) {
    AddShadow(r, c, blur, CornerRadii(rounding));
}

void DrawList::AddShadow(Rect r, Color c, float blur, CornerRadii rounding) {
    if (blur <= 0.f || c.a <= 0.f) return;
    constexpr int steps = 6;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i + 1) / steps;
        Color cc = c;
        cc.a = c.a * (1.f - t) * 0.35f;
        AddRectFilled(r.Expanded(blur * t), cc, ExpandRadii(rounding, blur * t));
    }
}

void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col) {
    const Vec2 p[3] = {a, b, c};
    AddConvexFill(p, 3, col);
}

void DrawList::AddTriangleFilledGradient(Vec2 a, Vec2 b, Vec2 c,
                                         const LinearGradient& gradient, float opacity) {
    const Vec2 points[] = {a, b, c};
    const float left = std::min({a.x, b.x, c.x});
    const float top = std::min({a.y, b.y, c.y});
    const float right = std::max({a.x, b.x, c.x});
    const float bottom = std::max({a.y, b.y, c.y});
    AddConvexFillGradient(points, 3, gradient,
                          {left, top, right - left, bottom - top}, opacity);
}

void DrawList::AddQuad(Rect r, Rect uv, Color c, GPUImageId imageId) {
    PrepareCmd(imageId);
    const uint32_t col = c.ToU32();
    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    vtx_.push_back({{r.x,       r.y},       {uv.x,        uv.y},        col});
    vtx_.push_back({{r.x + r.w, r.y},       {uv.x + uv.w, uv.y},        col});
    vtx_.push_back({{r.x + r.w, r.y + r.h}, {uv.x + uv.w, uv.y + uv.h}, col});
    vtx_.push_back({{r.x,       r.y + r.h}, {uv.x,        uv.y + uv.h}, col});
    const uint32_t i[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t k : i) idx_.push_back(base + k);

    if (imageId != 0 && r.w > 0.f && r.h > 0.f) {
        constexpr float fringe = 1.f;
        const uint32_t fringeBase = static_cast<uint32_t>(vtx_.size());
        const Vec2 outerPositions[] = {
            {r.x - fringe, r.y - fringe},
            {r.x + r.w + fringe, r.y - fringe},
            {r.x + r.w + fringe, r.y + r.h + fringe},
            {r.x - fringe, r.y + r.h + fringe}
        };
        for (size_t corner = 0; corner < 4; ++corner) {
            Vertex outer = vtx_[base + static_cast<uint32_t>(corner)];
            outer.pos = outerPositions[corner];
            outer.col &= 0x00FFFFFFu;
            vtx_.push_back(outer);
        }
        for (uint32_t edge = 0; edge < 4; ++edge) {
            const uint32_t next = (edge + 1) % 4;
            const uint32_t inner = base + edge;
            const uint32_t innerNext = base + next;
            const uint32_t outer = fringeBase + edge;
            const uint32_t outerNext = fringeBase + next;
            idx_.insert(idx_.end(),
                        {inner, innerNext, outerNext, inner, outerNext, outer});
        }
    }
    FinishPrimitive();
}

void DrawList::ApplyLinearGradient(size_t vertexStart, size_t vertexEnd, Rect bounds,
                                   const LinearGradient& gradient, float opacity) {
    if (gradient.Empty()) return;
    vertexEnd = std::min(vertexEnd, vtx_.size());
    vertexStart = std::min(vertexStart, vertexEnd);
    for (size_t i = vertexStart; i < vertexEnd; ++i) {
        const Color original{
            static_cast<float>(vtx_[i].col & 0xFFu) / 255.f,
            static_cast<float>((vtx_[i].col >> 8) & 0xFFu) / 255.f,
            static_cast<float>((vtx_[i].col >> 16) & 0xFFu) / 255.f,
            static_cast<float>((vtx_[i].col >> 24) & 0xFFu) / 255.f
        };
        Color color = SampleGradient(gradient, vtx_[i].pos, bounds);
        color.a *= original.a * opacity;
        vtx_[i].col = color.ToU32();
    }
}

} // namespace fluid