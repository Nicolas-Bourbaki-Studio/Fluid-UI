#include <FluidUI/DrawList.h>

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

CornerRadii NormalizeRadii(Rect r, CornerRadii radii) {
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
    return radii;
}

int ArcSegments(float radius) {
    return static_cast<int>(std::clamp(radius * 0.6f + 3.f, 4.f, 64.f));
}

void BuildRoundedRect(std::vector<Vec2>& out, Rect r, CornerRadii radii) {
    out.clear();
    radii = NormalizeRadii(r, radii);

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
        Arc(out, center, radius, start, end, ArcSegments(radius));
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

void BuildRoundedRectRing(std::vector<Vec2>& outer, std::vector<Vec2>& inner,
                          Rect r, CornerRadii radii, float borderWidth) {
    outer.clear();
    inner.clear();
    radii = NormalizeRadii(r, radii);
    const Rect innerRect{r.x + borderWidth, r.y + borderWidth,
                         r.w - borderWidth * 2.f, r.h - borderWidth * 2.f};
    const CornerRadii innerRadii = ExpandRadii(radii, -borderWidth);

    auto addArc = [&](Vec2 outerCenter, float outerRadius, Vec2 innerCenter,
                      float innerRadius, float start, float end,
                      Vec2 outerCorner, Vec2 innerCorner) {
        if (outerRadius < 0.5f) {
            outer.push_back(outerCorner);
            inner.push_back(innerCorner);
            return;
        }
        const int segments = ArcSegments(outerRadius);
        for (int i = 0; i <= segments; ++i) {
            const float angle = start + (end - start) *
                (static_cast<float>(i) / static_cast<float>(segments));
            const float x = std::cos(angle);
            const float y = std::sin(angle);
            outer.push_back({outerCenter.x + x * outerRadius,
                             outerCenter.y + y * outerRadius});
            inner.push_back({innerCenter.x + x * innerRadius,
                             innerCenter.y + y * innerRadius});
        }
    };

    addArc({r.x + r.w - radii.topRight, r.y + radii.topRight}, radii.topRight,
           {innerRect.x + innerRect.w - innerRadii.topRight,
            innerRect.y + innerRadii.topRight}, innerRadii.topRight,
           -kPi * 0.5f, 0.f, {r.x + r.w, r.y},
           {innerRect.x + innerRect.w, innerRect.y});
    addArc({r.x + r.w - radii.bottomRight, r.y + r.h - radii.bottomRight},
           radii.bottomRight,
           {innerRect.x + innerRect.w - innerRadii.bottomRight,
            innerRect.y + innerRect.h - innerRadii.bottomRight},
           innerRadii.bottomRight, 0.f, kPi * 0.5f,
           {r.x + r.w, r.y + r.h},
           {innerRect.x + innerRect.w, innerRect.y + innerRect.h});
    addArc({r.x + radii.bottomLeft, r.y + r.h - radii.bottomLeft},
           radii.bottomLeft,
           {innerRect.x + innerRadii.bottomLeft,
            innerRect.y + innerRect.h - innerRadii.bottomLeft},
           innerRadii.bottomLeft, kPi * 0.5f, kPi,
           {r.x, r.y + r.h}, {innerRect.x, innerRect.y + innerRect.h});
    addArc({r.x + radii.topLeft, r.y + radii.topLeft}, radii.topLeft,
           {innerRect.x + innerRadii.topLeft,
            innerRect.y + innerRadii.topLeft},
           innerRadii.topLeft, kPi, kPi * 1.5f,
           {r.x, r.y}, {innerRect.x, innerRect.y});
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
    rotationCenter_ = {};
    rotationCos_ = 1.f;
    rotationSin_ = 0.f;
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

void DrawList::SetRotation(Rect bounds, float degrees) {
    constexpr float radiansPerDegree = 3.14159265358979f / 180.f;
    const float radians = degrees * radiansPerDegree;
    rotationCenter_ = {bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f};
    rotationCos_ = std::cos(radians);
    rotationSin_ = std::sin(radians);
}

void DrawList::AddVertex(Vec2 position, Vec2 uv, uint32_t color) {
    const float x = position.x - rotationCenter_.x;
    const float y = position.y - rotationCenter_.y;
    const Vec2 rotated{rotationCenter_.x + x * rotationCos_ - y * rotationSin_,
                       rotationCenter_.y + x * rotationSin_ + y * rotationCos_};
    vtx_.push_back({rotated, uv, color});
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
    BringLayerToFrontImpl(layer, nullptr);
}

void DrawList::BringLayerToFront(const Layer& layer, Layer& trackedLayer) {
    BringLayerToFrontImpl(layer, &trackedLayer);
}

void DrawList::BringLayerToFront(const Layer& layer, Layer& trackedLayer,
                                 Layer& secondTrackedLayer) {
    BringLayerToFrontImpl(layer, &trackedLayer, &secondTrackedLayer);
}

void DrawList::BringLayerToFront(Layer& layer,
                                 const std::vector<Layer*>& trackedLayers) {
    if (layer.vertexStart > layer.vertexEnd || layer.vertexEnd > vtx_.size() ||
        layer.indexStart > layer.indexEnd || layer.indexEnd > idx_.size() ||
        layer.commandStart > layer.commandEnd || layer.commandEnd > cmds_.size())
        return;
    const size_t vertexCount = layer.vertexEnd - layer.vertexStart;
    const size_t indexCount = layer.indexEnd - layer.indexStart;
    if (vertexCount == 0 || indexCount == 0) return;
    BringLayerToFrontImpl(layer, nullptr, nullptr, &trackedLayers);
    layer.vertexStart = vtx_.size() - vertexCount;
    layer.vertexEnd = vtx_.size();
    layer.indexStart = idx_.size() - indexCount;
    layer.indexEnd = idx_.size();
    layer.commandStart = cmds_.size() - (layer.commandEnd - layer.commandStart);
    layer.commandEnd = cmds_.size();
}

void DrawList::BringLayerToFrontImpl(const Layer& layer, Layer* trackedLayer,
                                     Layer* secondTrackedLayer,
                                     const std::vector<Layer*>* trackedLayers) {
    if (layer.vertexStart > layer.vertexEnd || layer.vertexEnd > vtx_.size() ||
        layer.indexStart > layer.indexEnd || layer.indexEnd > idx_.size() ||
        layer.commandStart > layer.commandEnd || layer.commandEnd > cmds_.size()) return;

    const size_t vertexCount = layer.vertexEnd - layer.vertexStart;
    const size_t indexCount = layer.indexEnd - layer.indexStart;
    if (vertexCount == 0 || indexCount == 0) return;

    auto updateTrackedRange = [](size_t& trackedStart, size_t& trackedEnd,
                                 size_t movedStart, size_t movedEnd, size_t total) {
        const size_t movedCount = movedEnd - movedStart;
        if (trackedStart >= movedStart && trackedEnd <= movedEnd) {
            const size_t shift = total - movedEnd;
            trackedStart += shift;
            trackedEnd += shift;
        } else if (trackedStart >= movedEnd) {
            trackedStart -= movedCount;
            trackedEnd -= movedCount;
        }
    };
    auto updateTrackedLayer = [&](Layer* tracked) {
        if (!tracked) return;
        updateTrackedRange(tracked->vertexStart, tracked->vertexEnd,
                           layer.vertexStart, layer.vertexEnd, vtx_.size());
        updateTrackedRange(tracked->indexStart, tracked->indexEnd,
                           layer.indexStart, layer.indexEnd, idx_.size());
        updateTrackedRange(tracked->commandStart, tracked->commandEnd,
                           layer.commandStart, layer.commandEnd, cmds_.size());
    };
    updateTrackedLayer(trackedLayer);
    if (secondTrackedLayer != trackedLayer)
        updateTrackedLayer(secondTrackedLayer);
    if (trackedLayers) {
        for (Layer* tracked : *trackedLayers) {
            if (tracked != &layer && tracked != trackedLayer &&
                tracked != secondTrackedLayer)
                updateTrackedLayer(tracked);
        }
    }

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
    for (int i = 0; i < n; ++i) AddVertex(p[i], {}, col);
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
    AddBorderRing(r, {}, &gradient, opacity, borderWidth, rounding);
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
        AddVertex(points[i], {}, color.ToU32());
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

void DrawList::AddBorder(Rect r, Color color, float borderWidth, float rounding) {
    AddBorder(r, color, borderWidth, CornerRadii(rounding));
}

void DrawList::AddBorder(Rect r, Color color, float borderWidth, CornerRadii rounding) {
    AddBorderRing(r, color, nullptr, 1.f, borderWidth, rounding);
}

void DrawList::AddBorderRing(Rect r, Color color, const LinearGradient* gradient,
                             float opacity, float borderWidth, CornerRadii rounding) {
    if (borderWidth <= 0.f || r.w <= 0.f || r.h <= 0.f ||
        (gradient ? gradient->Empty() : color.a <= 0.f))
        return;

    borderWidth = std::min(borderWidth, std::min(r.w, r.h) * 0.5f);
    std::vector<Vec2> outer;
    std::vector<Vec2> inner;
    BuildRoundedRectRing(outer, inner, r, rounding, borderWidth);
    if (outer.size() != inner.size() || outer.size() < 3) return;

    PrepareCmd(0);
    const uint32_t base = static_cast<uint32_t>(vtx_.size());
    auto addVertex = [&](Vec2 position) {
        Color vertexColor = gradient ? SampleGradient(*gradient, position, r) : color;
        if (gradient) vertexColor.a *= opacity;
        AddVertex(position, {}, vertexColor.ToU32());
    };
    for (Vec2 position : outer) addVertex(position);
    for (Vec2 position : inner) addVertex(position);

    const uint32_t count = static_cast<uint32_t>(outer.size());
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t next = (i + 1) % count;
        const uint32_t innerIndex = base + count + i;
        const uint32_t nextInnerIndex = base + count + next;
        idx_.insert(idx_.end(), {base + i, base + next, nextInnerIndex,
                                 base + i, nextInnerIndex, innerIndex});
    }
    FinishPrimitive();
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
    AddVertex({r.x, r.y}, {uv.x, uv.y}, col);
    AddVertex({r.x + r.w, r.y}, {uv.x + uv.w, uv.y}, col);
    AddVertex({r.x + r.w, r.y + r.h}, {uv.x + uv.w, uv.y + uv.h}, col);
    AddVertex({r.x, r.y + r.h}, {uv.x, uv.y + uv.h}, col);
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
            AddVertex(outer.pos, outer.uv, outer.col);
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