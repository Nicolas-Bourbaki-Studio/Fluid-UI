#include <GLFW/glfw3.h>
#include "../src/FluidUI/FluidUI.h"
#include "../third_party/font8x8_basic.h"

#include <cstdio>
#include <filesystem>
#include <vector>

using namespace fluid;

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

static GLuint CreateDemoFontTexture() {
    constexpr int width = 128, height = 64;
    std::vector<unsigned char> pixels(width * height * 4, 0);
    for (int glyph = 0; glyph < 128; ++glyph) {
        const int originX = (glyph % 16) * 8;
        const int originY = (glyph / 16) * 8;
        for (int y = 0; y < 8; ++y) {
            const unsigned char row = static_cast<unsigned char>(font8x8_basic[glyph][y]);
            for (int x = 0; x < 8; ++x) {
                const int offset = ((originY + y) * width + originX + x) * 4;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
                pixels[offset + 3] = (row & (1u << x)) ? 255 : 0;
            }
        }
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

class DemoTextBackend final : public ITextBackend {
public:
    explicit DemoTextBackend(uint64_t texture) : texture_(texture) {}

    Vec2 Measure(std::string_view text, float fontSize) override {
        return {static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.2f};
    }

    void Emit(DrawList& draw, Vec2 pos, std::string_view text, float fontSize, Color color) override {
        const float advance = fontSize * 0.55f;
        const float lineStart = pos.x;
        for (unsigned char ch : text) {
            if (ch == '\n') { pos.x = lineStart; pos.y += fontSize * 1.2f; continue; }
            const unsigned int glyph = ch < 128 ? ch : static_cast<unsigned int>('?');
            const float u = static_cast<float>((glyph % 16) * 8) / 128.f;
            const float v = static_cast<float>((glyph / 16) * 8) / 64.f;
            draw.AddQuad({pos.x, pos.y, advance, fontSize},
                         {u, v, 8.f / 128.f, 8.f / 64.f}, color, texture_);
            pos.x += advance;
        }
    }

private:
    uint64_t texture_;
};

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(1600, 900, "Fluid UI component and docking demo", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

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
    ui.Style().Rule("button.danger").Background(Color::FromHex(0x8E2B2BFF)).Rounding(2);
    ui.Style().Rule("button.danger:hover").Background(Color::FromHex(0xC0392BFF));
    ui.Style().Rule("button.danger:active").Background(Color::FromHex(0xFF5544FF));

    const GLuint demoTexture = CreateDemoTexture();
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
    double last = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        double mx, my; glfwGetCursorPos(win, &mx, &my);
        const double now = glfwGetTime();

        InputState in;
        in.mousePos = {static_cast<float>(mx), static_cast<float>(my)};
        in.mouseDown[0] = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        in.deltaTime = static_cast<float>(now - last);
        in.displaySize = {static_cast<float>(w), static_cast<float>(h)};
        last = now;
        ui.NewFrame(in);

        if (ui.BeginWindow("Components##main", {24, 24, 500, 720})) {
            ui.Text("Core controls");
            ui.PushClass("primary");
            if (ui.Button("Button")) {}
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
            ui.ImageLabel("Image label", demoTexture, {28, 28});
            if (ui.ImageButton("Image button", demoTexture, {36, 28})) {}
            if (ui.Dropdown("Text menu", choices, &sizeChoice)) {}
            if (ui.Dropdown("Icon menu", choices, &iconChoice, "+", false)) {}
            if (ui.Dropdown("Icon and text menu", choices, &mixedChoice, "*")) {}

            if (ui.BeginContainer("Basic container", {0, 62})) {
                ui.Text("A padded, clipped container.");
                ui.EndContainer();
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
            ui.EndWindow();
        }

        if (ui.BeginWindow("Tabs and collapse##tabs", {548, 24, 550, 720})) {
            ui.Text("Configurable tab positions");
            if (ui.BeginTabContainer("Top tabs", {0, 145}, textTabs, &topTab,
                                     Context::TabPosition::Top)) {
                ui.Text("Top tabs with text labels.");
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

        if (ui.BeginWindow("Dynamic docking##help", {1120, 24, 300, 235})) {
            ui.Text("Dynamic docking");
            ui.Text("Drag this title bar to a screen edge.");
            ui.Text("Left / right and top / bottom");
            ui.Text("snap to resizable half-screen areas.");
            ui.Text("Resize the window to see the dock");
            ui.Text("layout adapt to the display.");
            ui.Toggle("Enable snapping", &showAdvanced);
            ui.DockWindows(showAdvanced);
            ui.EndWindow();
        }

        const DrawList& dl = ui.EndFrame();
        glClearColor(0.08f, 0.08f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderGL(dl, w, h);
        glfwSwapBuffers(win);
    }
    glDeleteTextures(1, &demoTexture);
    glDeleteTextures(1, &fontTexture);
    glfwDestroyWindow(win);
    glfwTerminate();
}
