#include <GLFW/glfw3.h>
#include "../src/FluidUI/FluidUI.h"

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

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Fluid UI demo", nullptr, nullptr);
    if (!win) return 1;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    Context ui;
    ui.Style() = StyleSheet::Dark();

    // Customize anything: a new "danger" variant of buttons
    ui.Style().Rule("button.danger").Background(Color::FromHex(0x8E2B2BFF)).Rounding(2);
    ui.Style().Rule("button.danger:hover").Background(Color::FromHex(0xC0392BFF));
    ui.Style().Rule("button.danger:active").Background(Color::FromHex(0xFF5544FF));

    bool vsync = true;
    float volume = 0.5f;
    double last = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        int w, h; glfwGetFramebufferSize(win, &w, &h);
        double mx, my; glfwGetCursorPos(win, &mx, &my);
        const double now = glfwGetTime();

        InputState in;
        in.mousePos = {(float)mx, (float)my};
        in.mouseDown[0] = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        in.deltaTime = (float)(now - last);
        in.displaySize = {(float)w, (float)h};
        last = now;

        ui.NewFrame(in);
        if (ui.BeginWindow("Settings", {40, 40, 320, 260})) {
            ui.Text("Hello from Fluid UI");
            if (ui.Button("Apply")) { /* ... */ }
            ui.SameLine();
            ui.PushClass("danger");
            if (ui.Button("Reset")) { /* ... */ }
            ui.PopClass();
            ui.Checkbox("VSync", &vsync);
            ui.SliderFloat("Volume", &volume, 0.f, 1.f);
            ui.EndWindow();
        }
        const DrawList& dl = ui.EndFrame();

        glClearColor(0.08f, 0.08f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderGL(dl, w, h);
        glfwSwapBuffers(win);
    }
    glfwTerminate();
}
