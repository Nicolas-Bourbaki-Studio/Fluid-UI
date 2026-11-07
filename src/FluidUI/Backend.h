#pragma once
#include "DrawList.h"
#include "Types.h"
#include <string_view>

namespace fluid {

struct InputState {
    Vec2  mousePos;
    bool  mouseDown[3] = {false, false, false};
    float mouseWheel = 0.f;
    float deltaTime = 1.f / 60.f;
    Vec2  displaySize;
};

// Implement with stb_truetype / FreeType / the platform's text API.
// Measure() is used for layout; Emit() must append glyph quads via DrawList::AddQuad.
class ITextBackend {
public:
    virtual ~ITextBackend() = default;
    virtual Vec2 Measure(std::string_view text, float fontSize) = 0;
    virtual void Emit(DrawList& dl, Vec2 pos, std::string_view text, float fontSize, Color color) = 0;
};

// Fallback so layout works before you have fonts: estimates width, draws nothing.
class BasicTextBackend final : public ITextBackend {
public:
    Vec2 Measure(std::string_view t, float size) override {
        return {static_cast<float>(t.size()) * size * 0.55f, size * 1.2f};
    }
    void Emit(DrawList&, Vec2, std::string_view, float, Color) override {}
};

// Optional convenience interface for rendering backends (OpenGL, Vulkan, Metal, D3D...).
// Render() resolves each nonzero DrawCmd::texture as a caller-owned GPUImageId.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual void Render(const DrawList& dl, Vec2 displaySize) = 0;
    virtual GPUImageId CreateTexture(const void* rgba8, int w, int h) = 0;
    virtual void DestroyTexture(GPUImageId id) = 0;
};

} // namespace fluid