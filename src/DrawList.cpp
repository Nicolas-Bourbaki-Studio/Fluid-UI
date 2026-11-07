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

void BuildRoundedRect(std::vector<Vec2>& out, Rect r, float rad) {
    out.clear();
    rad = std::min({rad, r.w * 0.5f, r.h * 0.5f});
    if (rad < 0.5f) {
        out = {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
        return;
    }
    const int seg = std::clamp(static_cast<int>(rad * 0.6f) + 3, 4, 12);
    Arc(out, {r.x + r.w - rad, r.y + rad},       rad, -kPi * 0.5f, 0.f,        seg);
    Arc(out, {r.x + r.w - rad, r.y + r.h - rad}, rad, 0.f,         kPi * 0.5f, seg);
    Arc(out, {r.x + rad,       r.y + r.h - rad}, rad, kPi * 0.5f,  kPi,        seg);
    Arc(out, {r.x + rad,       r.y + rad},       rad, kPi,         kPi * 1.5f, seg);
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
    if (r.w <= 0.f || r.h <= 0.f) return;
    std::vector<Vec2> pts;
    BuildRoundedRect(pts, r, rounding);
    AddConvexFill(pts.data(), static_cast<int>(pts.size()), c);
}

void DrawList::AddBox(Rect r, Color bg, Color border, float bw, float rounding) {
    if (bw > 0.f && border.a > 0.f) {
        AddRectFilled(r, border, rounding);
        AddRectFilled({r.x + bw, r.y + bw, r.w - 2 * bw, r.h - 2 * bw}, bg, std::max(0.f, rounding - bw));
    } else {
        AddRectFilled(r, bg, rounding);
    }
}

void DrawList::AddShadow(Rect r, Color c, float blur, float rounding) {
    if (blur <= 0.f || c.a <= 0.f) return;
    constexpr int steps = 6;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i + 1) / steps;
        Color cc = c;
        cc.a = c.a * (1.f - t) * 0.35f;
        AddRectFilled(r.Expanded(blur * t), cc, rounding + blur * t);
    }
}

void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col) {
    const Vec2 p[3] = {a, b, c};
    AddConvexFill(p, 3, col);
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
    FinishPrimitive();
}

} // namespace fluid