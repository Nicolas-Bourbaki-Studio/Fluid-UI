#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <GLFW/glfw3.h>
#include "../src/FluidUI/FluidUI.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace fluid;

constexpr GLenum kGlMultisample = 0x809D;

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
        if (c.texture) {
            glDisable(kGlMultisample);
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(c.texture));
        } else {
            glEnable(kGlMultisample);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glDrawElements(GL_TRIANGLES, (GLsizei)c.idxCount, GL_UNSIGNED_INT, idx.data() + c.idxOffset);
    }
    glDisable(kGlMultisample);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_SCISSOR_TEST);
}

static GLuint CreateTextureFromImage(const std::filesystem::path& path, int& width, int& height,
                                     std::string& error) {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        char message[96];
        std::snprintf(message, sizeof(message), "COM initialization failed (HRESULT 0x%08lX)",
                      static_cast<unsigned long>(result));
        error = message;
        return 0;
    }

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
        if (FAILED(result)) {
            char message[96];
            std::snprintf(message, sizeof(message), "WIC could not decode the image (HRESULT 0x%08lX)",
                          static_cast<unsigned long>(result));
            error = message;
        } else {
            error = "Image dimensions are empty or exceed OpenGL limits";
        }
        return 0;
    }

    const uint64_t pixelBytes = static_cast<uint64_t>(imageWidth) * imageHeight * 4u;
    if (pixelBytes > std::numeric_limits<UINT>::max()) {
        release();
        error = "Decoded image is too large for the WIC pixel buffer";
        return 0;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(pixelBytes));
    result = converter->CopyPixels(nullptr, imageWidth * 4,
                                   static_cast<UINT>(pixels.size()), pixels.data());
    release();
    if (FAILED(result)) {
        char message[96];
        std::snprintf(message, sizeof(message), "WIC could not copy decoded pixels (HRESULT 0x%08lX)",
                      static_cast<unsigned long>(result));
        error = message;
        return 0;
    }

    GLuint texture = 0;
    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGenTextures(1, &texture);
    if (texture == 0) {
        error = "OpenGL did not allocate a texture for the decoded image";
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLint clampToEdge = 0x812F;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(imageWidth),
                 static_cast<GLsizei>(imageHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum uploadError = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    if (uploadError != GL_NO_ERROR) {
        char message[96];
        std::snprintf(message, sizeof(message), "OpenGL texture upload failed (error 0x%04X)",
                      static_cast<unsigned int>(uploadError));
        error = message;
        glDeleteTextures(1, &texture);
        return 0;
    }
    width = static_cast<int>(imageWidth);
    height = static_cast<int>(imageHeight);
    error.clear();
    return texture;
}

struct DemoFontAtlas {
    static constexpr int Width = 512;
    static constexpr int Height = 512;
    static constexpr int FirstCharacter = 32;
    static constexpr int CharacterCount = 96;
    static constexpr float BakeSize = 32.f;

    GLuint texture = 0;
    std::array<stbtt_bakedchar, CharacterCount> glyphs{};
};

static bool CreateOpenSansFontAtlas(const std::filesystem::path& path,
                                   DemoFontAtlas& atlas, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open Open Sans font: " + path.string();
        return false;
    }
    const std::istreambuf_iterator<char> fontBegin(file);
    const std::istreambuf_iterator<char> fontEnd;
    std::vector<unsigned char> fontData(fontBegin, fontEnd);
    if (file.bad()) {
        error = "Could not read Open Sans font: " + path.string();
        return false;
    }
    if (fontData.empty() || fontData.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "Open Sans font file is empty or too large";
        return false;
    }

    std::vector<unsigned char> coverage(DemoFontAtlas::Width * DemoFontAtlas::Height);
    const int packedRows = stbtt_BakeFontBitmap(
        fontData.data(), 0, DemoFontAtlas::BakeSize, coverage.data(),
        DemoFontAtlas::Width, DemoFontAtlas::Height, DemoFontAtlas::FirstCharacter,
        DemoFontAtlas::CharacterCount, atlas.glyphs.data());
    if (packedRows <= 0) {
        error = "Could not bake Open Sans glyphs into the font atlas";
        return false;
    }

    std::vector<unsigned char> pixels(coverage.size() * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = coverage[i];
    }
    glGenTextures(1, &atlas.texture);
    if (atlas.texture == 0) {
        error = "OpenGL did not allocate a texture for the Open Sans font";
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, atlas.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLint clampToEdge = 0x812F;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DemoFontAtlas::Width, DemoFontAtlas::Height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    const GLenum uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &atlas.texture);
        atlas.texture = 0;
        char message[96];
        std::snprintf(message, sizeof(message), "OpenGL font texture upload failed (error 0x%04X)",
                      static_cast<unsigned int>(uploadError));
        error = message;
        return false;
    }
    return true;
}

class DemoTextBackend final : public ITextBackend {
public:
    explicit DemoTextBackend(const DemoFontAtlas& atlas)
        : texture_(atlas.texture), glyphs_(atlas.glyphs) {}

    Vec2 Measure(std::string_view text, float fontSize) override {
        const float scale = fontSize / DemoFontAtlas::BakeSize;
        float lineWidth = 0.f, maxWidth = 0.f;
        size_t lines = 1;
        for (unsigned char ch : text) {
            if (ch == '\n') {
                maxWidth = std::max(maxWidth, lineWidth);
                lineWidth = 0.f;
                ++lines;
                continue;
            }
            const unsigned int glyph = GlyphIndex(ch);
            lineWidth += glyphs_[glyph].xadvance * scale;
        }
        maxWidth = std::max(maxWidth, lineWidth);
        return {maxWidth, static_cast<float>(lines) * fontSize * 1.2f};
    }

    void Emit(DrawList& draw, Vec2 pos, std::string_view text, float fontSize, Color color) override {
        const float scale = fontSize / DemoFontAtlas::BakeSize;
        float penX = 0.f, lineOffset = 0.f;
        for (unsigned char ch : text) {
            if (ch == '\n') {
                penX = 0.f;
                lineOffset += fontSize * 1.2f;
                continue;
            }
            stbtt_aligned_quad quad{};
            float glyphY = 0.f;
            stbtt_GetBakedQuad(glyphs_.data(), DemoFontAtlas::Width, DemoFontAtlas::Height,
                               static_cast<int>(GlyphIndex(ch)), &penX, &glyphY, &quad, 1);
            const float baseline = pos.y + fontSize + lineOffset;
            const float width = (quad.x1 - quad.x0) * scale;
            const float height = (quad.y1 - quad.y0) * scale;
            if (width > 0.f && height > 0.f) {
                draw.AddQuad({pos.x + quad.x0 * scale, baseline + quad.y0 * scale,
                              width, height},
                             {quad.s0, quad.t0, quad.s1 - quad.s0, quad.t1 - quad.t0},
                             color, texture_);
            }
        }
    }

private:
    static unsigned int GlyphIndex(unsigned char ch) {
        const unsigned int character = ch >= DemoFontAtlas::FirstCharacter &&
            ch < DemoFontAtlas::FirstCharacter + DemoFontAtlas::CharacterCount
            ? ch : static_cast<unsigned int>('?');
        return character - DemoFontAtlas::FirstCharacter;
    }

    GPUImageId texture_;
    std::array<stbtt_bakedchar, DemoFontAtlas::CharacterCount> glyphs_;
};

int main(int argc, char** argv) {
    std::filesystem::path layoutPath =
        std::filesystem::current_path() / "fluidui-layout.ini";
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--layout" || i + 1 >= argc) {
            std::fprintf(stderr, "Usage: %s [--layout <path>]\n", argv[0]);
            return 1;
        }
        layoutPath = std::filesystem::path(argv[++i]);
        if (layoutPath.empty()) {
            std::fprintf(stderr, "Layout file path must not be empty\n");
            return 1;
        }
    }

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* win = glfwCreateWindow(1600, 900, "Fluid UI component and docking demo", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetScrollCallback(win, OnScroll);

    Context ui;
    ui.Style() = StyleSheet::Dark();
    std::string cssError;
    const std::filesystem::path themePath =
        std::filesystem::path(FLUIDUI_EXAMPLE_SOURCE_DIR) / "theme.css";
    if (!ui.Style().LoadCSSFile(themePath.string(), &cssError)) {
        std::fprintf(stderr, "%s\n", cssError.c_str());
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    const std::string iconImageSource = "../src/Images/Icon.jpg";
    const std::filesystem::path iconImagePath =
        themePath.parent_path().parent_path() / "src" / "Images" / "Icon.jpg";
    int iconImageWidth = 0, iconImageHeight = 0;
    std::string imageError;
    const GLuint iconTexture = CreateTextureFromImage(
        iconImagePath, iconImageWidth, iconImageHeight, imageError);
    if (iconTexture == 0) {
        std::fprintf(stderr, "Could not load background image '%s': %s\n",
                     iconImagePath.string().c_str(), imageError.c_str());
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    ui.Style().RegisterImage(iconImageSource, static_cast<GPUImageId>(iconTexture),
                             {static_cast<float>(iconImageWidth),
                              static_cast<float>(iconImageHeight)});
    const std::filesystem::path repositoryPath = themePath.parent_path().parent_path();
    int folderIconWidth = 0, folderIconHeight = 0;
    int emptyIconWidth = 0, emptyIconHeight = 0;
    int productImageWidth = 0, productImageHeight = 0;
    std::string assetError;
    const GLuint folderIconTexture = CreateTextureFromImage(
        repositoryPath / "src" / "Icons" / "FolderIcn.png",
        folderIconWidth, folderIconHeight, assetError);
    const GLuint emptyIconTexture = folderIconTexture != 0 ? CreateTextureFromImage(
        repositoryPath / "src" / "Icons" / "EmptyIcn.png",
        emptyIconWidth, emptyIconHeight, assetError) : 0;
    const GLuint productImageTexture = emptyIconTexture != 0 ? CreateTextureFromImage(
        repositoryPath / "src" / "Images" / "Pr.png",
        productImageWidth, productImageHeight, assetError) : 0;
    if (folderIconTexture == 0 || emptyIconTexture == 0 || productImageTexture == 0) {
        std::fprintf(stderr, "Could not load demo image asset: %s\n", assetError.c_str());
        if (folderIconTexture) glDeleteTextures(1, &folderIconTexture);
        if (emptyIconTexture) glDeleteTextures(1, &emptyIconTexture);
        if (productImageTexture) glDeleteTextures(1, &productImageTexture);
        glDeleteTextures(1, &iconTexture);
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    DemoFontAtlas fontAtlas;
    std::string fontError;
    const std::filesystem::path fontPath =
        themePath.parent_path().parent_path() / "src" / "Fonts" / "OpenSans.ttf";
    if (!CreateOpenSansFontAtlas(fontPath, fontAtlas, fontError)) {
        std::fprintf(stderr, "%s\n", fontError.c_str());
        glDeleteTextures(1, &folderIconTexture);
        glDeleteTextures(1, &emptyIconTexture);
        glDeleteTextures(1, &productImageTexture);
        glDeleteTextures(1, &iconTexture);
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    DemoTextBackend demoText(fontAtlas);
    ui.SetTextBackend(&demoText);
    std::error_code layoutPathError;
    const bool layoutExists = std::filesystem::exists(layoutPath, layoutPathError);
    bool layoutLoadFailed = false;
    if (layoutPathError) {
        std::fprintf(stderr, "Could not check layout path '%s': %s\n",
                     layoutPath.string().c_str(), layoutPathError.message().c_str());
        layoutLoadFailed = true;
    } else if (layoutExists) {
        std::string layoutError;
        if (!ui.LoadLayout(layoutPath.string(), &layoutError)) {
            std::fprintf(stderr, "%s\n", layoutError.c_str());
            layoutLoadFailed = true;
        } else {
            std::fprintf(stdout, "Loaded window layout from '%s'\n",
                         layoutPath.string().c_str());
        }
    }
    std::array<GLFWcursor*, 5> cursors{
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR),
        glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR),
        glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR),
        glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR),
        glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR)
    };
    GLFWcursor* currentCursor = nullptr;
    const std::vector<std::string_view> choices{"Small", "Medium", "Large"};
    const std::vector<Context::TabItem> textTabs{{"Overview", {}}, {"Details", {}}, {"Preview", {}}};
    const std::vector<Context::TabItem> iconTabs{
        {"Home", "H", false}, {"Files", "F", false}, {"Tools", "T", false}};
    const std::vector<Context::TabItem> mixedTabs{{"One", "1"}, {"Two", "2"}, {"Three", "3"}};

    bool enabled = true, dockingEnabled = true, expanded = true, showDanger = false;
    bool networkConnected = false;
    int radioValue = 1, sizeChoice = 1, iconChoice = 0, mixedChoice = 2, topTab = 0;
    float volume = 0.62f;
    constexpr std::string_view componentWindow = "Components##main";
    constexpr std::string_view tabsWindow = "Tabs and panels##tabs";
    constexpr std::string_view managerWindow = "Window manager##manager";
    constexpr std::string_view settingsWindow = "Settings##settings";
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
        ui.DockWindows(dockingEnabled);

        if (ui.BeginMainMenuBar()) {
            if (ui.BeginMenu("File")) {
                if (ui.MenuItem("New demo window"))
                    ui.SetWindowOpen(componentWindow, true);
                if (ui.MenuItem("Settings..."))
                    ui.SetWindowOpen(settingsWindow, true);
                if (ui.MenuItem("Exit")) glfwSetWindowShouldClose(win, GLFW_TRUE);
                ui.EndMenu();
            }
            if (ui.BeginMenu("Layout")) {
                std::string layoutError;
                if (ui.MenuItem("Save layout")) {
                    if (!ui.SaveLayout(layoutPath.string(), &layoutError))
                        std::fprintf(stderr, "%s\n", layoutError.c_str());
                    else
                        std::fprintf(stdout, "Saved window layout to '%s'\n",
                                     layoutPath.string().c_str());
                }
                if (ui.MenuItem("Load layout")) {
                    if (!ui.LoadLayout(layoutPath.string(), &layoutError))
                        std::fprintf(stderr, "%s\n", layoutError.c_str());
                    else
                        std::fprintf(stdout, "Loaded window layout from '%s'\n",
                                     layoutPath.string().c_str());
                }
                ui.EndMenu();
            }
            if (ui.BeginMenu("Edit")) {
                if (ui.MenuItem("Preferences..."))
                    ui.SetWindowOpen(settingsWindow, true);
                if (ui.MenuItem("Restore demo windows")) {
                    ui.SetWindowOpen(componentWindow, true);
                    ui.SetWindowOpen(tabsWindow, true);
                    ui.SetWindowOpen(managerWindow, true);
                }
                ui.EndMenu();
            }
            if (ui.BeginMenu("View")) {
                if (ui.MenuItem("Components")) ui.SetWindowOpen(componentWindow, true);
                if (ui.MenuItem("Tabs and panels")) ui.SetWindowOpen(tabsWindow, true);
                if (ui.MenuItem("Window manager")) ui.SetWindowOpen(managerWindow, true);
                if (ui.MenuItem("Settings")) ui.SetWindowOpen(settingsWindow, true);
                ui.EndMenu();
            }
            if (ui.BeginMenu("Project")) {
                if (ui.MenuItem("Project settings..."))
                    ui.SetWindowOpen(settingsWindow, true);
                if (ui.MenuItem("Window manager"))
                    ui.SetWindowOpen(managerWindow, true);
                ui.EndMenu();
            }

            if (ui.BeginMenu("Connect engine to Roblox Studio")) {
                if (ui.MenuItem("Connect to Roblox Studio")) networkConnected = true;
                if (ui.MenuItem("EDIT API KEY"))
                ui.EndMenu();
            }
            ui.EndMainMenuBar();
        }

        if (ui.BeginWindow(componentWindow, {24, 40, 500, 720})) {
            ui.Text("Core controls");
            ui.Text("CSS background image and gradients");
            ui.PushClass("brand");
            ui.EmptyWidget("CSS background image", {160, 120});
            ui.PopClass();
            ui.SameLine();
            ui.PushClass("icon-card");
            ui.ImageWidget("Icon image card", static_cast<GPUImageId>(iconTexture), {112, 112});
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
            ui.TextLabelLeft("Text with leading icon",
                             static_cast<GPUImageId>(folderIconTexture), {30, 30});
            ui.TextLabelRight("Text with trailing icon",
                              static_cast<GPUImageId>(emptyIconTexture), {30, 30});
            ui.Image(static_cast<GPUImageId>(productImageTexture), {24, 24});
            ui.ImageLabel("Image label", static_cast<GPUImageId>(productImageTexture), {28, 28});
            if (ui.ImageButton("Image button",
                               static_cast<GPUImageId>(productImageTexture), {36, 28})) {}
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
            ui.Text("Drag a title bar and choose a 3x3 dock target.");
            ui.Text("Double-click a docked title bar to undock.");
            ui.Text("A live preview shows the snap area.");
            ui.Text("Drop on the compass center to create a tabbed window group.");
            ui.Text("Docked windows resize with the display.");
            ui.Text("Resize floating windows from any edge or corner.");
            ui.Text("Click a window to bring it to the front.");
            ui.Text("Docked panels resize to share available space.");
            ui.Toggle("Enable docking", &dockingEnabled);
            if (ui.Button("Show components")) ui.SetWindowOpen(componentWindow, true);
            if (ui.Button("Show tabs and panels")) ui.SetWindowOpen(tabsWindow, true);
            if (ui.Button("Hide components")) ui.SetWindowOpen(componentWindow, false);
            if (ui.Button("Hide tabs and panels")) ui.SetWindowOpen(tabsWindow, false);
            ui.EndWindow();
        }

        if (ui.BeginWindow(settingsWindow, {360, 180, 480, 300})) {
            ui.Text("General");
            ui.Toggle("Enable docking", &dockingEnabled);
            ui.Text("Font: Open Sans");
            ui.Text("Project: Fluid-UI demo");
            ui.Text(networkConnected ? "Network status: Connected"
                                    : "Network status: Disconnected");
            if (ui.Button("Restore demo windows")) {
                ui.SetWindowOpen(componentWindow, true);
                ui.SetWindowOpen(tabsWindow, true);
                ui.SetWindowOpen(managerWindow, true);
            }
            ui.EndWindow();
        }

        const DrawList& dl = ui.EndFrame();
        GLFWcursor* requestedCursor = nullptr;
        switch (ui.Cursor()) {
            case CursorShape::Arrow: requestedCursor = cursors[0]; break;
            case CursorShape::ResizeHorizontal: requestedCursor = cursors[1]; break;
            case CursorShape::ResizeVertical: requestedCursor = cursors[2]; break;
            case CursorShape::ResizeDiagonalNWSE: requestedCursor = cursors[3]; break;
            case CursorShape::ResizeDiagonalNESW: requestedCursor = cursors[4]; break;
        }
        if (requestedCursor != currentCursor) {
            glfwSetCursor(win, requestedCursor);
            currentCursor = requestedCursor;
        }
        glClearColor(0.08f, 0.08f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderGL(dl, w, h);
        glfwSwapBuffers(win);
    }
    if (!layoutLoadFailed) {
        std::string layoutError;
        if (!ui.SaveLayout(layoutPath.string(), &layoutError))
            std::fprintf(stderr, "%s\n", layoutError.c_str());
        else
            std::fprintf(stdout, "Saved window layout to '%s'\n",
                         layoutPath.string().c_str());
    }
    glDeleteTextures(1, &iconTexture);
    glDeleteTextures(1, &folderIconTexture);
    glDeleteTextures(1, &emptyIconTexture);
    glDeleteTextures(1, &productImageTexture);
    glDeleteTextures(1, &fontAtlas.texture);
    for (GLFWcursor* cursor : cursors)
        if (cursor) glfwDestroyCursor(cursor);
    glfwDestroyWindow(win);
    glfwTerminate();
}
