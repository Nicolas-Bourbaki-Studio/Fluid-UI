#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <GLFW/glfw3.h>
#include "../src/FluidUI/FluidUI.h"
#include "../third_party/font8x8_basic.h"

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace fluid;

static float gMouseWheel = 0.f;

static void OnScroll(GLFWwindow*, double, double yOffset) {
    gMouseWheel += static_cast<float>(yOffset);
}

static void RenderGL(const DrawList& dl, int fbW, int fbH) {
    glViewport(0, 0, fbW, fbH);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);

    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, fbW, fbH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    const auto& v = dl.Vertices();
    const auto& idx = dl.Indices();
    if (v.empty()) return;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    const char* base = reinterpret_cast<const char*>(v.data());
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), base + offsetof(Vertex, pos));
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), base + offsetof(Vertex, uv));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), base + offsetof(Vertex, col));

    for (const DrawCmd& c : dl.Commands()) {
        if (c.idxCount == 0) continue;
        glScissor((GLint)c.clip.x, fbH - (GLint)(c.clip.y + c.clip.h),
                  (GLsizei)c.clip.w, (GLsizei)c.clip.h);
        if (c.texture) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, (GLuint)c.texture); }
        else           { glDisable(GL_TEXTURE_2D); }
        glDrawElements(GL_TRIANGLES, (GLsizei)c.idxCount, GL_UNSIGNED_INT, idx.data() + c.idxOffset);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_SCISSOR_TEST);
}

static GLuint CreateDemoTexture() {
    const unsigned char pixels[] = {
        0x4A, 0x6C, 0xF7, 0xFF, 0xE6, 0x6B, 0x5C, 0xFF,
        0xE6, 0x6B, 0x5C, 0xFF, 0x4A, 0x6C, 0xF7, 0xFF
    };
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

static GLuint CreateTextureFromPng(const std::filesystem::path& path, int& width, int& height) {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) return 0;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    auto release = [&]() {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
        if (uninitialize) CoUninitialize();
    };

    result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
        result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    UINT imageWidth = 0, imageHeight = 0;
    if (SUCCEEDED(result)) result = frame->GetSize(&imageWidth, &imageHeight);
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result))
        result = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
    if (FAILED(result) || imageWidth == 0 || imageHeight == 0 ||
        imageWidth > static_cast<UINT>(std::numeric_limits<GLsizei>::max() / 4) ||
        imageHeight > static_cast<UINT>(std::numeric_limits<GLsizei>::max())) {
        release();
        return 0;
    }

    const uint64_t pixelBytes = static_cast<uint64_t>(imageWidth) * imageHeight * 4u;
    if (pixelBytes > std::numeric_limits<UINT>::max()) {
        release();
        return 0;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(pixelBytes));
    result = converter->CopyPixels(nullptr, imageWidth * 4,
                                   static_cast<UINT>(pixels.size()), pixels.data());
    release();
    if (FAILED(result)) return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLint clampToEdge = 0x812F;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(imageWidth),
                 static_cast<GLsizei>(imageHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    width = static_cast<int>(imageWidth);
    height = static_cast<int>(imageHeight);
    return texture;
}

static GLuint CreateDemoFontTexture() {
    constexpr int glyphWidth = 32, glyphHeight = 32;
    constexpr int width = 16 * glyphWidth, height = 8 * glyphHeight;
    std::vector<unsigned char> pixels(width * height * 4, 0);
    for (int glyph = 0; glyph < 128; ++glyph) {
        const int originX = (glyph % 16) * glyphWidth;
        const int originY = (glyph / 16) * glyphHeight;
        auto coverage = [&](int x, int y) {
            if (x < 0 || x >= 8 || y < 0 || y >= 8) return 0.f;
            const unsigned char row = static_cast<unsigned char>(font8x8_basic[glyph][y]);
            return (row & (1u << x)) ? 1.f : 0.f;
        };
        for (int y = 0; y < glyphHeight; ++y) {
            const float sourceY = (static_cast<float>(y) + 0.5f) * 8.f / glyphHeight - 0.5f;
            const int y0 = static_cast<int>(std::floor(sourceY));
            const float fy = sourceY - static_cast<float>(y0);
            for (int x = 0; x < glyphWidth; ++x) {
                const float sourceX = (static_cast<float>(x) + 0.5f) * 8.f / glyphWidth - 0.5f;
                const int x0 = static_cast<int>(std::floor(sourceX));
                const float fx = sourceX - static_cast<float>(x0);
                const float top = coverage(x0, y0) * (1.f - fx) + coverage(x0 + 1, y0) * fx;
                const float bottom = coverage(x0, y0 + 1) * (1.f - fx) +
                                     coverage(x0 + 1, y0 + 1) * fx;
                const unsigned char alpha = static_cast<unsigned char>(
                    std::lround((top * (1.f - fy) + bottom * fy) * 255.f));
                const int offset = ((originY + y) * width + originX + x) * 4;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
                pixels[offset + 3] = alpha;
            }
        }
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

class DemoTextBackend final : public ITextBackend {
public:
    explicit DemoTextBackend(GPUImageId texture) : texture_(texture) {}

    Vec2 Measure(std::string_view text, float fontSize) override {
        return {static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.2f};
    }

    void Emit(DrawList& draw, Vec2 pos, std::string_view text, float fontSize, Color color) override {
        const float advance = fontSize * 0.55f;
        constexpr float cellWidth = 32.f, cellHeight = 32.f;
        constexpr float atlasWidth = 512.f, atlasHeight = 256.f;
        const float lineStart = pos.x;
        for (unsigned char ch : text) {
            if (ch == '\n') { pos.x = lineStart; pos.y += fontSize * 1.2f; continue; }
            const unsigned int glyph = ch < 128 ? ch : static_cast<unsigned int>('?');
            const float u = (static_cast<float>((glyph % 16) * 32) + 0.5f) / atlasWidth;
            const float v = (static_cast<float>((glyph / 16) * 32) + 0.5f) / atlasHeight;
            draw.AddQuad({pos.x, pos.y, advance, fontSize},
                         {u, v, (cellWidth - 1.f) / atlasWidth,
                          (cellHeight - 1.f) / atlasHeight}, color, texture_);
            pos.x += advance;
        }
    }

private:
    GPUImageId texture_;
};

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(1600, 900, "Fluid UI component and docking demo", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetScrollCallback(win, OnScroll);

    Context ui;
    ui.Style() = StyleSheet::Dark();
    std::string cssError;
    const std::filesystem::path themePath = std::filesystem::path(__FILE__).parent_path() / "theme.css";
    if (!ui.Style().LoadCSSFile(themePath.string(), &cssError)) {
        std::fprintf(stderr, "%s\n", cssError.c_str());
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    const std::string brandImageSource = "../Github Assets/Images/FluidUI Brush.png";
    const std::filesystem::path brandImagePath =
        themePath.parent_path().parent_path() / "Github Assets" / "Images" / "FluidUI Brush.png";
    int brandImageWidth = 0, brandImageHeight = 0;
    const GLuint brandTexture = CreateTextureFromPng(
        brandImagePath, brandImageWidth, brandImageHeight);
    if (brandTexture == 0) {
        std::fprintf(stderr, "Could not load background image: %s\n", brandImagePath.string().c_str());
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    ui.Style().RegisterImage(brandImageSource, static_cast<GPUImageId>(brandTexture),
                             {static_cast<float>(brandImageWidth),
                              static_cast<float>(brandImageHeight)});
    const std::vector<std::string> brandClasses{"brand"};
    const ComputedStyle brandStyle =
        ui.Style().Resolve("empty-widget", brandClasses, State_None);
    if (brandStyle.backgroundImage != static_cast<GPUImageId>(brandTexture)) {
        std::fprintf(stderr, "CSS background-image did not resolve to the registered texture\n");
        glDeleteTextures(1, &brandTexture);
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    const std::vector<std::string> gradientClasses{"gradient-demo"};
    const std::vector<std::string> gradientTextClasses{"gradient-text"};
    const ComputedStyle gradientStyle =
        ui.Style().Resolve("empty-widget", gradientClasses, State_None);
    const ComputedStyle gradientTextStyle =
        ui.Style().Resolve("text", gradientTextClasses, State_None);
    if (gradientStyle.backgroundGradient.stops.size() != 3 ||
        gradientTextStyle.textGradient.stops.size() != 3) {
        std::fprintf(stderr, "CSS background or text gradient did not resolve\n");
        glDeleteTextures(1, &brandTexture);
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    const GLuint demoTexture = CreateDemoTexture();
    const GPUImageId demoImageId = static_cast<GPUImageId>(demoTexture);
    const GLuint fontTexture = CreateDemoFontTexture();
    DemoTextBackend demoText(fontTexture);
    ui.SetTextBackend(&demoText);
    const std::vector<std::string_view> choices{"Small", "Medium", "Large"};
    const std::vector<Context::TabItem> textTabs{{"Overview", {}}, {"Details", {}}, {"Preview", {}}};
    const std::vector<Context::TabItem> iconTabs{
        {"Home", "H", false}, {"Files", "F", false}, {"Tools", "T", false}};
    const std::vector<Context::TabItem> mixedTabs{{"One", "1"}, {"Two", "2"}, {"Three", "3"}};

    bool enabled = true, showAdvanced = true, expanded = true, showDanger = false;
    int radioValue = 1, sizeChoice = 1, iconChoice = 0, mixedChoice = 2, topTab = 0;
    float volume = 0.62f;
    constexpr std::string_view componentWindow = "Components##main";
    constexpr std::string_view tabsWindow = "Tabs and panels##tabs";
    constexpr std::string_view managerWindow = "Window manager##manager";
    double last = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        double mx, my; glfwGetCursorPos(win, &mx, &my);
        const double now = glfwGetTime();

        InputState in;
        in.mousePos = {static_cast<float>(mx), static_cast<float>(my)};
        in.mouseDown[0] = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        in.mouseWheel = gMouseWheel;
        gMouseWheel = 0.f;
        in.deltaTime = static_cast<float>(now - last);
        in.displaySize = {static_cast<float>(w), static_cast<float>(h)};
        last = now;
        ui.NewFrame(in);
        ui.DockWindows(showAdvanced);

        if (ui.BeginMainMenuBar()) {
            if (ui.BeginMenu("File")) {
                if (ui.MenuItem("Exit")) glfwSetWindowShouldClose(win, GLFW_TRUE);
                ui.EndMenu();
            }
            if (ui.BeginMenu("View")) {
                if (ui.MenuItem("Components")) ui.SetWindowOpen(componentWindow, true);
                if (ui.MenuItem("Tabs and panels")) ui.SetWindowOpen(tabsWindow, true);
                if (ui.MenuItem("Window manager")) ui.SetWindowOpen(managerWindow, true);
                if (ui.BeginMenu("More")) {
                    if (ui.MenuItem("Reset component window"))
                        ui.SetWindowOpen(componentWindow, true);
                    ui.EndMenu();
                }
                ui.EndMenu();
            }
            if (ui.MenuItem("About")) {}
            ui.EndMainMenuBar();
        }

        if (ui.BeginWindow(componentWindow, {24, 40, 500, 720})) {
            ui.Text("Core controls");
            ui.Text("CSS background image and gradients");
            ui.PushClass("brand");
            ui.EmptyWidget("CSS background image", {360, 120});
            ui.PopClass();
            ui.PushClass("gradient-demo");
            ui.EmptyWidget("CSS background gradient", {360, 56});
            ui.PopClass();
            ui.PushClass("gradient-text");
            ui.Text("CSS text gradient");
            ui.PopClass();
            ui.PushClass("gradient-demo");
            if (ui.Button("Gradient button")) {}
            ui.PopClass();
            if (ui.Button("Show window manager")) ui.SetWindowOpen(managerWindow, true);
            ui.PushClass("primary");
            ui.PushClass("emphasis");
            if (ui.Button("Button")) {}
            ui.PopClass();
            ui.PopClass();
            ui.SameLine();
            ui.PushClass("danger");
            if (ui.Button("Danger")) {}
            ui.PopClass();
            ui.Toggle("Toggle", &enabled);
            ui.Checkbox("Checkbox", &showDanger);
            ui.Radio("Radio option A", &radioValue, 0);
            ui.Radio("Radio option B", &radioValue, 1);
            ui.ValueSlider("Volume", &volume, 0.f, 1.f);
            ui.TextLabel("Text label");
            ui.TextLabelLeft("Text with leading icon", "*");
            ui.TextLabelRight("Text with trailing icon", ">");
            ui.Image(demoImageId, {24, 24});
            ui.ImageLabel("Image label", demoImageId, {28, 28});
            if (ui.ImageButton("Image button", demoImageId, {36, 28})) {}
            if (ui.Dropdown("Text menu", choices, &sizeChoice)) {}
            if (ui.Dropdown("Icon menu", choices, &iconChoice, "+", false)) {}
            if (ui.Dropdown("Icon and text menu", choices, &mixedChoice, "*")) {}

            if (ui.BeginChild("Child demo", {0, 62})) {
                ui.Text("A wrapper-friendly, padded child region.");
                ui.EndChild();
            }
            if (ui.BeginList("Horizontal list", {0, 52}, true)) {
                ui.Button("First");
                ui.Button("Second");
                ui.Button("Third");
                ui.EndContainer();
            }
            if (ui.BeginList("Vertical list", {0, 100}, false)) {
                ui.Text("Vertical list item one");
                ui.Text("Vertical list item two");
                ui.Text("Vertical list item three");
                ui.EndContainer();
            }
            ui.Text("Scroll test - use the mouse wheel or drag the scrollbar");
            for (int i = 1; i <= 14; ++i) {
                char row[48];
                std::snprintf(row, sizeof(row), "Scrollable sample row %02d", i);
                ui.Text(row);
            }
            ui.EndWindow();
        }

        if (ui.BeginWindow(tabsWindow, {548, 40, 550, 720})) {
            ui.Text("Configurable tab positions");
            if (ui.Button("Show window manager")) ui.SetWindowOpen(managerWindow, true);
            if (ui.BeginTabContainer("Top tabs", {0, 145}, textTabs, &topTab,
                                     Context::TabPosition::Top)) {
                if (topTab == 0) ui.Text("Overview: text-only tab labels.");
                else if (topTab == 1) ui.Text("Details: selected tab state updates immediately.");
                else ui.Text("Preview: try resizing or docking this window.");
                ui.EndContainer();
            }
            if (ui.BeginTabContainer("Left tabs", {0, 145}, iconTabs, &iconChoice,
                                     Context::TabPosition::Left)) {
                ui.Text("Left tabs with icon labels.");
                ui.EndContainer();
            }
            if (ui.BeginTabContainer("Bottom tabs", {0, 145}, mixedTabs, &mixedChoice,
                                     Context::TabPosition::Bottom)) {
                ui.Text("Bottom tabs with icon and text labels.");
                ui.EndContainer();
            }
            if (ui.BeginCollapsible("Collapsible container", &expanded, {0, 116})) {
                ui.Text("This content can be shown or hidden.");
                ui.Button("Nested action");
                ui.EndContainer();
            }
            ui.EndWindow();
        }

        if (ui.BeginWindow(managerWindow, {1120, 40, 390, 350})) {
            ui.Text("Window management");
            ui.Text("Drag a title bar to a screen edge.");
            ui.Text("A live preview shows the snap area.");
            ui.Text("Docked windows resize with the display.");
            ui.Text("Drag the bottom-right corner to resize.");
            ui.Text("Click a window to bring it to the front.");
            ui.Text("Docked panels resize to share available space.");
            ui.Toggle("Enable docking", &showAdvanced);
            if (ui.Button("Show components")) ui.SetWindowOpen(componentWindow, true);
            if (ui.Button("Show tabs and panels")) ui.SetWindowOpen(tabsWindow, true);
            if (ui.Button("Hide components")) ui.SetWindowOpen(componentWindow, false);
            if (ui.Button("Hide tabs and panels")) ui.SetWindowOpen(tabsWindow, false);
            ui.EndWindow();
        }

        const DrawList& dl = ui.EndFrame();
        glClearColor(0.08f, 0.08f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderGL(dl, w, h);
        glfwSwapBuffers(win);
    }
    glDeleteTextures(1, &brandTexture);
    glDeleteTextures(1, &demoTexture);
    glDeleteTextures(1, &fontTexture);
    glfwDestroyWindow(win);
    glfwTerminate();
}
