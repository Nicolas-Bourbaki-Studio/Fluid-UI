#pragma once
#include "Export.h"
#include "Types.h"
#include <vector>

namespace fluid {

struct Vertex {
    Vec2 pos;
    Vec2 uv;
    uint32_t col; // see Color::ToU32()
};

struct DrawCmd {
    Rect clip;
    uint64_t texture = 0;   // 0 = untextured (backend should bind a 1x1 white texture or disable texturing)
    uint32_t idxOffset = 0;
    uint32_t idxCount = 0;
};

// Backend-agnostic geometry. Triangles are NOT guaranteed a particular winding: disable culling.
class FLUID_API DrawList {
public:
    void Clear();

    void PushClipRect(Rect r);
    void PopClipRect();

    void AddRectFilled(Rect r, Color c, float rounding = 0.f);
    void AddBox(Rect r, Color bg, Color border, float borderWidth, float rounding);
    void AddShadow(Rect r, Color c, float blur, float rounding);
    void AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col);
    // Textured quad: used by text backends to emit glyphs.
    void AddQuad(Rect r, Rect uv, Color c, uint64_t texture);

    const std::vector<Vertex>&  Vertices()  const { return vtx_; }
    const std::vector<uint32_t>& Indices()  const { return idx_; }
    const std::vector<DrawCmd>& Commands()  const { return cmds_; }

private:
    Rect CurrentClip() const;
    void PrepareCmd(uint64_t texture);
    void FinishPrimitive();
    void AddConvexFill(const Vec2* pts, int n, Color c);

    std::vector<Vertex> vtx_;
    std::vector<uint32_t> idx_;
    std::vector<DrawCmd> cmds_;
    std::vector<Rect> clipStack_;
};

} // namespace fluid
