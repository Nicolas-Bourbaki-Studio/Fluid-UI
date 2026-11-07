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
    GPUImageId texture = 0; // 0 = untextured; otherwise caller-owned image ID for the renderer to bind
    uint32_t idxOffset = 0;
    uint32_t idxCount = 0;
};

// Backend-agnostic geometry. Triangles are NOT guaranteed a particular winding: disable culling.
class FLUID_API DrawList {
public:
    struct Layer {
        size_t vertexStart = 0, indexStart = 0, commandStart = 0;
        size_t vertexEnd = 0, indexEnd = 0, commandEnd = 0;
    };

    void Clear();

    void PushClipRect(Rect r);
    void PopClipRect();
    Layer BeginLayer();
    void EndLayer(Layer& layer);
    void BringLayerToFront(const Layer& layer);

    void AddRectFilled(Rect r, Color c, float rounding = 0.f);
    void AddBox(Rect r, Color bg, Color border, float borderWidth, float rounding);
    void AddShadow(Rect r, Color c, float blur, float rounding);
    void AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col);
    // Textured quad using a caller-owned GPU image ID; the rendering backend resolves/binds it.
    void AddQuad(Rect r, Rect uv, Color c, GPUImageId imageId);

    const std::vector<Vertex>&  Vertices()  const { return vtx_; }
    const std::vector<uint32_t>& Indices()  const { return idx_; }
    const std::vector<DrawCmd>& Commands()  const { return cmds_; }

private:
    Rect CurrentClip() const;
    void PrepareCmd(GPUImageId imageId);
    void FinishPrimitive();
    void AddConvexFill(const Vec2* pts, int n, Color c);

    std::vector<Vertex> vtx_;
    std::vector<uint32_t> idx_;
    std::vector<DrawCmd> cmds_;
    std::vector<Rect> clipStack_;
    bool breakCommand_ = false;
};

} // namespace fluid
