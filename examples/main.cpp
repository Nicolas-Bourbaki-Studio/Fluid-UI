#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <wincodec.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <FluidUI/FluidUI.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
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
static std::string gTextInput;
static std::array<bool, InputState::KeyCount> gKeysDown{};
static std::array<bool, InputState::KeyCount> gKeysPressed{};
static bool gBackspacePressed = false;
static bool gEnterPressed = false;
static bool gSelectAllPressed = false;
static bool gCopyPressed = false;
static bool gPastePressed = false;

static void OnScroll(GLFWwindow*, double, double yOffset) {
    gMouseWheel += static_cast<float>(yOffset);
}

static void OnChar(GLFWwindow*, unsigned int codepoint) {
    if (codepoint <= 0x7Fu) {
        gTextInput.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        gTextInput.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        gTextInput.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        gTextInput.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        gTextInput.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        gTextInput.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        gTextInput.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        gTextInput.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        gTextInput.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        gTextInput.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

static void OnKey(GLFWwindow*, int key, int, int action, int mods) {
    if (key >= 0 && static_cast<size_t>(key) < InputState::KeyCount) {
        if (action == GLFW_PRESS) {
            gKeysDown[static_cast<size_t>(key)] = true;
            gKeysPressed[static_cast<size_t>(key)] = true;
        } else if (action == GLFW_RELEASE) {
            gKeysDown[static_cast<size_t>(key)] = false;
        }
    }
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_BACKSPACE) gBackspacePressed = true;
    if (key == GLFW_KEY_ENTER) gEnterPressed = true;
    if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) gSelectAllPressed = true;
    if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) gCopyPressed = true;
    if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) gPastePressed = true;
}

static bool SelectCssFile(HWND owner, std::filesystem::path& path, std::string& error) {
    std::vector<wchar_t> filename(32768, L'\0');
    const wchar_t filter[] =
        L"CSS files (*.css)\0*.css\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrDefExt = L"css";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        path = std::filesystem::path(filename.data());
        error.clear();
        return true;
    }
    const DWORD dialogError = CommDlgExtendedError();
    error = dialogError == 0 ? std::string{}
        : "CSS file picker failed with error " + std::to_string(dialogError);
    return false;
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

static bool SvgDimension(const std::string& svg, std::string_view name, float& value) {
    const size_t root = svg.find("<svg");
    const size_t rootEnd = root == std::string::npos ? root : svg.find('>', root);
    if (root == std::string::npos || rootEnd == std::string::npos) return false;
    const std::string key = std::string(name) + "=";
    size_t position = svg.find(key, root);
    if (position == std::string::npos || position >= rootEnd) return false;
    position += key.size();
    if (position >= rootEnd || (svg[position] != '"' && svg[position] != '\'')) return false;
    const char quote = svg[position++];
    char* end = nullptr;
    value = std::strtof(svg.c_str() + position, &end);
    return end != svg.c_str() + position && end < svg.c_str() + rootEnd &&
           std::isfinite(value) && value > 0.f;
}

static bool SvgPixelSize(const std::string& svg, int& width, int& height) {
    float widthValue = 0.f, heightValue = 0.f;
    const bool hasWidth = SvgDimension(svg, "width", widthValue);
    const bool hasHeight = SvgDimension(svg, "height", heightValue);
    if (!hasWidth || !hasHeight) {
        const size_t root = svg.find("<svg");
        const size_t viewBox = root == std::string::npos ? root : svg.find("viewBox=", root);
        const size_t rootEnd = root == std::string::npos ? root : svg.find('>', root);
        if (viewBox != std::string::npos && viewBox < rootEnd) {
            const size_t valueStart = svg.find_first_of("\"'", viewBox + 8);
            if (valueStart != std::string::npos && valueStart < rootEnd) {
                char* end = nullptr;
                const char* value = svg.c_str() + valueStart + 1;
                float values[4]{};
                bool parsed = true;
                for (float& part : values) {
                    while (*value == ' ' || *value == '\t' || *value == '\r' ||
                           *value == '\n' || *value == ',')
                        ++value;
                    part = std::strtof(value, &end);
                    if (end == value || !std::isfinite(part)) { parsed = false; break; }
                    value = end;
                }
                if (parsed && values[2] > 0.f && values[3] > 0.f) {
                    if (!hasWidth) widthValue = values[2];
                    if (!hasHeight) heightValue = values[3];
                }
            }
        }
    }
    if (!hasWidth && widthValue <= 0.f) widthValue = 256.f;
    if (!hasHeight && heightValue <= 0.f) heightValue = 256.f;
    if (widthValue <= 0.f || heightValue <= 0.f) return false;
    const double widthDouble = widthValue, heightDouble = heightValue;
    const double scale = std::min({1.0, 4096.0 / widthDouble, 4096.0 / heightDouble,
        std::sqrt(16777216.0 / (widthDouble * heightDouble))});
    width = std::max(1, static_cast<int>(std::lround(widthDouble * scale)));
    height = std::max(1, static_cast<int>(std::lround(heightDouble * scale)));
    return true;
}

static bool RasterizeSvg(const std::filesystem::path& path, int& width, int& height,
                         std::vector<unsigned char>& pixels, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open SVG image: " + path.string();
        return false;
    }
    const std::string svg{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
    if (svg.empty() || !SvgPixelSize(svg, width, height)) {
        error = "SVG image is empty or has invalid dimensions: " + path.string();
        return false;
    }
    if (svg.size() > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
        error = "SVG image is too large to decode: " + path.string();
        return false;
    }

    ID2D1Factory1* factory = nullptr;
    ID3D11Device* d3dDevice = nullptr;
    IDXGIDevice* dxgiDevice = nullptr;
    ID2D1Device* d2dDevice = nullptr;
    ID2D1DeviceContext* baseD2dContext = nullptr;
    ID2D1DeviceContext5* d2dContext = nullptr;
    ID2D1Bitmap1* targetBitmap = nullptr;
    ID2D1Bitmap1* readbackBitmap = nullptr;
    ID2D1SvgDocument* document = nullptr;
    IStream* stream = nullptr;
    auto release = [&]() {
        if (stream) stream->Release();
        if (document) document->Release();
        if (readbackBitmap) readbackBitmap->Release();
        if (targetBitmap) targetBitmap->Release();
        if (d2dContext) d2dContext->Release();
        if (baseD2dContext) baseD2dContext->Release();
        if (d2dDevice) d2dDevice->Release();
        if (dxgiDevice) dxgiDevice->Release();
        if (d3dDevice) d3dDevice->Release();
        if (factory) factory->Release();
    };
    auto fail = [&](const char* reason, HRESULT result) {
        char message[160];
        std::snprintf(message, sizeof(message), "%s (HRESULT 0x%08lX)",
                      reason, static_cast<unsigned long>(result));
        error = message;
        release();
        return false;
    };

    D2D1_FACTORY_OPTIONS factoryOptions{};
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), &factoryOptions, reinterpret_cast<void**>(&factory));
    if (FAILED(result)) return fail("Could not create Direct2D factory for SVG", result);

    D3D_FEATURE_LEVEL featureLevel{};
    result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &featureLevel, nullptr);
    if (FAILED(result))
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &d3dDevice, &featureLevel, nullptr);
    if (FAILED(result)) return fail("Could not create Direct3D device for SVG", result);

    result = d3dDevice->QueryInterface(__uuidof(IDXGIDevice),
                                       reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(result)) return fail("Could not get DXGI device for SVG", result);
    result = factory->CreateDevice(dxgiDevice, &d2dDevice);
    if (FAILED(result)) return fail("Could not create Direct2D device for SVG", result);
    result = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                             &baseD2dContext);
    if (FAILED(result)) return fail("Could not create Direct2D context for SVG", result);
    result = baseD2dContext->QueryInterface(__uuidof(ID2D1DeviceContext5),
                                             reinterpret_cast<void**>(&d2dContext));
    if (FAILED(result)) return fail("Direct2D SVG rendering requires Windows 10 or later", result);

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, svg.size());
    if (!memory) return fail("Could not allocate SVG stream", E_OUTOFMEMORY);
    void* memoryData = GlobalLock(memory);
    if (!memoryData) {
        GlobalFree(memory);
        return fail("Could not lock SVG stream", E_OUTOFMEMORY);
    }
    std::memcpy(memoryData, svg.data(), svg.size());
    GlobalUnlock(memory);
    result = CreateStreamOnHGlobal(memory, TRUE, &stream);
    if (FAILED(result)) {
        GlobalFree(memory);
        return fail("Could not create SVG stream", result);
    }
    result = d2dContext->CreateSvgDocument(stream,
        D2D1_SIZE_F{static_cast<float>(width), static_cast<float>(height)}, &document);
    if (FAILED(result)) return fail("Could not decode SVG document", result);

    D2D1_BITMAP_PROPERTIES1 bitmapProperties{};
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.dpiX = 96.f;
    bitmapProperties.dpiY = 96.f;
    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    result = d2dContext->CreateBitmap(D2D1_SIZE_U{static_cast<UINT>(width),
        static_cast<UINT>(height)}, nullptr, 0, &bitmapProperties, &targetBitmap);
    if (FAILED(result)) return fail("Could not create SVG render target", result);

    d2dContext->SetTarget(targetBitmap);
    d2dContext->BeginDraw();
    d2dContext->Clear(D2D1_COLOR_F{0.f, 0.f, 0.f, 0.f});
    d2dContext->DrawSvgDocument(document);
    result = d2dContext->EndDraw();
    if (FAILED(result)) return fail("Could not render SVG document", result);

    D2D1_BITMAP_PROPERTIES1 readbackProperties = bitmapProperties;
    readbackProperties.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
    result = d2dContext->CreateBitmap(D2D1_SIZE_U{static_cast<UINT>(width),
        static_cast<UINT>(height)}, nullptr, 0, &readbackProperties, &readbackBitmap);
    if (FAILED(result)) return fail("Could not create SVG readback bitmap", result);
    result = readbackBitmap->CopyFromBitmap(nullptr, targetBitmap, nullptr);
    d2dContext->SetTarget(nullptr);
    if (FAILED(result)) return fail("Could not copy rendered SVG pixels", result);
    D2D1_MAPPED_RECT mapped{};
    result = readbackBitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(result)) return fail("Could not read rendered SVG pixels", result);

    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const auto* source = mapped.bits + static_cast<size_t>(height - 1 - y) * mapped.pitch;
        unsigned char* destination = pixels.data() +
            static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            const unsigned char blue = source[x * 4];
            const unsigned char green = source[x * 4 + 1];
            const unsigned char red = source[x * 4 + 2];
            const unsigned char alpha = source[x * 4 + 3];
            destination[x * 4] = alpha == 0 ? 0 : static_cast<unsigned char>(
                std::min(255u, (static_cast<unsigned int>(red) * 255u + alpha / 2u) / alpha));
            destination[x * 4 + 1] = alpha == 0 ? 0 : static_cast<unsigned char>(
                std::min(255u, (static_cast<unsigned int>(green) * 255u + alpha / 2u) / alpha));
            destination[x * 4 + 2] = alpha == 0 ? 0 : static_cast<unsigned char>(
                std::min(255u, (static_cast<unsigned int>(blue) * 255u + alpha / 2u) / alpha));
            destination[x * 4 + 3] = alpha;
        }
    }
    readbackBitmap->Unmap();
    release();
    error.clear();
    return true;
}

static GLuint CreateTextureFromSvg(const std::filesystem::path& path, int& width, int& height,
                                   std::string& error) {
    std::vector<unsigned char> pixels;
    if (!RasterizeSvg(path, width, height, pixels, error)) return 0;
    GLuint texture = 0;
    GLint previousTexture = 0, previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGenTextures(1, &texture);
    if (texture == 0) {
        error = "OpenGL did not allocate a texture for the SVG image";
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLint clampToEdge = 0x812F;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum uploadError = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        error = "OpenGL texture upload failed for SVG image";
        return 0;
    }
    error.clear();
    return texture;
}

static GLuint CreateTextureFromFile(const std::filesystem::path& path, int& width, int& height,
                                    std::string& error) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".svg") return CreateTextureFromSvg(path, width, height, error);
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
        return CreateTextureFromImage(path, width, height, error);
    error = "Unsupported image format '" + extension + "' for " + path.string();
    return 0;
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
    glfwSetCharCallback(win, OnChar);
    glfwSetKeyCallback(win, OnKey);

    Context ui;
    ui.Style() = StyleSheet::Dark();
    std::string cssError;
    const std::filesystem::path themePath =
        std::filesystem::path(FLUIDUI_EXAMPLE_SOURCE_DIR) / "theme.css";
    if (!ui.Style().LoadCSSFile(themePath, &cssError)) {
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
    const GLuint iconTexture = CreateTextureFromFile(
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
    const GLuint folderIconTexture = CreateTextureFromFile(
        repositoryPath / "src" / "Icons" / "FolderIcn.svg",
        folderIconWidth, folderIconHeight, assetError);
    const GLuint emptyIconTexture = folderIconTexture != 0 ? CreateTextureFromFile(
        repositoryPath / "src" / "Icons" / "EmptyIcn.png",
        emptyIconWidth, emptyIconHeight, assetError) : 0;
    const GLuint productImageTexture = emptyIconTexture != 0 ? CreateTextureFromFile(
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
    const auto registerThemeImages = [&](StyleSheet& style) {
        style.RegisterImage(iconImageSource, static_cast<GPUImageId>(iconTexture),
                            {static_cast<float>(iconImageWidth),
                             static_cast<float>(iconImageHeight)});
        style.RegisterImage("../src/Icons/FolderIcn.svg",
                            static_cast<GPUImageId>(folderIconTexture),
                            {static_cast<float>(folderIconWidth),
                             static_cast<float>(folderIconHeight)});
        style.RegisterImage("../src/Icons/EmptyIcn.png",
                            static_cast<GPUImageId>(emptyIconTexture),
                            {static_cast<float>(emptyIconWidth),
                             static_cast<float>(emptyIconHeight)});
        style.RegisterImage("../src/Images/Pr.png",
                            static_cast<GPUImageId>(productImageTexture),
                            {static_cast<float>(productImageWidth),
                             static_cast<float>(productImageHeight)});
    };
    registerThemeImages(ui.Style());
    std::filesystem::path activeThemePath = themePath;
    std::filesystem::file_time_type activeThemeWriteTime{};
    std::error_code themeTimestampError;
    activeThemeWriteTime =
        std::filesystem::last_write_time(activeThemePath, themeTimestampError);
    bool hasThemeWriteTime = !themeTimestampError;
    std::error_code themeWatchError = themeTimestampError;
    bool autoReloadTheme = true;
    std::string themeStatus = themeTimestampError
        ? "CSS loaded, but automatic reload is unavailable: " +
              themeTimestampError.message()
        : "Loaded " + activeThemePath.string();
    const auto loadTheme = [&](const std::filesystem::path& path) {
        StyleSheet candidate = StyleSheet::Dark();
        std::string error;
        if (!candidate.LoadCSSFile(path, &error)) {
            themeStatus = "Could not load CSS: " + error;
            return false;
        }
        registerThemeImages(candidate);
        ui.Style() = std::move(candidate);
        activeThemePath = path;
        std::error_code timestampError;
        activeThemeWriteTime = std::filesystem::last_write_time(path, timestampError);
        hasThemeWriteTime = !timestampError;
        themeWatchError = timestampError;
        themeStatus = "Loaded " + activeThemePath.string();
        if (timestampError)
            themeStatus += "; automatic reload unavailable: " +
                           timestampError.message();
        return true;
    };
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
    const std::vector<std::string_view> fileTiles{
        "Assets", "Documents", "Images", "Projects"};
    const std::vector<Context::TabItem> textTabs{{"Overview", {}}, {"Details", {}}, {"Preview", {}}};
    const std::vector<Context::TabItem> iconTabs{
        {"Home", "H", false}, {"Files", "F", false}, {"Tools", "T", false}};
    const std::vector<Context::TabItem> mixedTabs{{"One", "1"}, {"Two", "2"}, {"Three", "3"}};

    bool enabled = true, dockingEnabled = true, expanded = true, showDanger = false;
    bool networkConnected = false;
    int radioValue = 1, sizeChoice = 1, iconChoice = 0, mixedChoice = 2, topTab = 0;
    float volume = 0.62f, dragValue = 0.35f;
    float colorValue[3] = {0.25f, 0.55f, 0.9f};
    int selectedTile = 0;
    int lmbClickCount = 0;
    int selectedItem = 0;
    std::string consoleCommand, textInputValue, droppedPayload;
    std::vector<Context::ConsoleLine> consoleOutput{
        {"Fluid UI console ready.", Context::ConsoleTextType::Normal},
        {"Type a command and press Enter.", Context::ConsoleTextType::Normal},
        {"Select text and press Ctrl+C.", Context::ConsoleTextType::Normal},
        {"Warning: sample warning message.", Context::ConsoleTextType::Warning},
        {"Error: sample error message.", Context::ConsoleTextType::Error}
    };
    constexpr std::string_view componentWindow = "Components##main";
    constexpr std::string_view tabsWindow = "Tabs and panels##tabs";
    constexpr std::string_view managerWindow = "Window manager##manager";
    constexpr std::string_view settingsWindow = "Settings##settings";
    constexpr std::string_view advancedWindow = "Advanced controls##advanced";
    constexpr std::string_view themeWindow = "Theme settings##theme-settings";
    double last = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (autoReloadTheme) {
            std::error_code timestampError;
            const auto writeTime =
                std::filesystem::last_write_time(activeThemePath, timestampError);
            if (timestampError) {
                if (themeWatchError != timestampError) {
                    themeStatus = "Automatic CSS reload failed: " +
                                  timestampError.message();
                    themeWatchError = timestampError;
                }
                hasThemeWriteTime = false;
            } else if (!hasThemeWriteTime || writeTime != activeThemeWriteTime) {
                activeThemeWriteTime = writeTime;
                hasThemeWriteTime = true;
                themeWatchError.clear();
                loadTheme(activeThemePath);
            }
        }
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        double mx, my; glfwGetCursorPos(win, &mx, &my);
        const double now = glfwGetTime();

        InputState in;
        in.mousePos = {static_cast<float>(mx), static_cast<float>(my)};
        in.mouseDown[0] = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        in.mouseDown[1] = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        in.keysDown = gKeysDown;
        in.keysPressed = gKeysPressed;
        in.mouseWheel = gMouseWheel;
        gMouseWheel = 0.f;
        in.deltaTime = static_cast<float>(now - last);
        in.displaySize = {static_cast<float>(w), static_cast<float>(h)};
        const std::string frameTextInput = std::move(gTextInput);
        gTextInput.clear();
        in.textInput = frameTextInput;
        in.keyBackspace = gBackspacePressed;
        in.keyEnter = gEnterPressed;
        in.keySelectAll = gSelectAllPressed;
        in.keyCopy = gCopyPressed;
        in.keyPaste = gPastePressed;
        in.keyShift = in.keysDown[GLFW_KEY_LEFT_SHIFT] ||
                      in.keysDown[GLFW_KEY_RIGHT_SHIFT];
        in.keyControl = in.keysDown[GLFW_KEY_LEFT_CONTROL] ||
                        in.keysDown[GLFW_KEY_RIGHT_CONTROL];
        if (const char* clipboard = glfwGetClipboardString(win))
            in.clipboardText = clipboard;
        in.setClipboardText = [win](const std::string& text) {
            glfwSetClipboardString(win, text.c_str());
        };
        gKeysPressed.fill(false);
        gBackspacePressed = gEnterPressed = gSelectAllPressed = gCopyPressed =
            gPastePressed = false;
        last = now;
        ui.NewFrame(in);
        ui.DockWindows(dockingEnabled);

        if (ui.BeginMainMenuBar()) {
            if (ui.BeginMenu("File")) {
                if (ui.MenuItem("New demo window"))
                    ui.SetWindowOpen(componentWindow, true);
                if (ui.MenuItem("Settings..."))
                    ui.SetWindowOpen(settingsWindow, true);
                if (ui.MenuItem("Advanced controls"))
                    ui.SetWindowOpen(advancedWindow, true);
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
                if (ui.BeginMenu("Theme")) {
                    if (ui.MenuItem("Theme settings..."))
                        ui.SetWindowOpen(themeWindow, true);
                    if (ui.MenuItem("Reload CSS"))
                        loadTheme(activeThemePath);
                    ui.EndMenu();
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
            ui.ProgressBar("Loading sample: 62%", 0.62f, {0, 28});
            if (ui.TileContainer("Project files", fileTiles, &selectedTile, {0, 150},
                                 static_cast<GPUImageId>(folderIconTexture),
                                 {static_cast<float>(folderIconWidth),
                                  static_cast<float>(folderIconHeight)})) {}
            ui.Button("LMB_Start / LMB_End demo");
            if (ui.LMB_Start()) {
                ++lmbClickCount;
                ui.LMB_End();
            }
            ui.Text("Left-click activations: " + std::to_string(lmbClickCount));
            ui.Button("Hover for a hint");
            ui.Hint("Hints appear while the previous widget is hovered.");
            if (ui.Button("Show notification"))
                ui.Notification("This is a demo notification.");
            ui.Button("RMB_Start / RMB_End test");
            if (ui.RMB_Start()) {
                if (ui.RMB_Item("Refresh"))
                    consoleOutput.push_back(
                        {"Refreshed.", Context::ConsoleTextType::Normal});
                if (ui.RMB_Item("Clear console")) consoleOutput.clear();
                ui.RMB_End();
            }
            ui.ConsoleOutput("Console log", consoleOutput, {0, 66});
            if (ui.ConsoleInput("Console command", &consoleCommand)) {
                consoleOutput.push_back(
                    {"> " + consoleCommand, Context::ConsoleTextType::Normal});
                consoleCommand.clear();
            }

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
            ui.Text("Drag a title bar and choose a top, bottom, left, right, or center target.");
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

        if (ui.BeginWindow(advancedWindow, {210, 100, 440, 620})) {
            ui.TextWrapped("Additional controls and wrapper helpers from the attached project.");
            ui.BeginDisabled();
            ui.Button("Disabled button");
            ui.EndDisabled();

            ui.BeginPropertyGrid("advanced-properties");
            ui.PropertyRow("Numeric value");
            if (ui.DragFloat("Drag to adjust##value", &dragValue, 0.01f, 0.f, 1.f)) {}
            ui.EndPropertyGrid();
            ui.ColorEdit3("Editable color", colorValue);
            ui.Text("Double-click the drag control to edit its value.");
            if (ui.InputText("Text input", &textInputValue)) {}
            if (ui.Selectable("Selectable row", selectedItem == 0)) selectedItem = 0;
            if (ui.Selectable("Second row", selectedItem == 1)) selectedItem = 1;

            if (ui.TreeNode("Tree node", Context::TreeNode_DefaultOpen)) {
                ui.Text("Nested tree content");
                ui.TreePop();
            }
            if (ui.CollapsingHeader("Collapsing header")) {
                ui.Text("Header content is visible.");
            }

            if (ui.Button("Open popup")) ui.OpenPopup("Advanced popup");
            if (ui.BeginPopup("Advanced popup")) {
                ui.Text("This popup is independently layered.");
                if (ui.Button("Close popup")) ui.CloseCurrentPopup();
                ui.EndPopup();
            }
            if (ui.Button("Right-click for popup")) {}
            if (ui.BeginPopupContextItem("Item context popup")) {
                ui.Text("Context popup contents");
                if (ui.Button("Dismiss")) ui.CloseCurrentPopup();
                ui.EndPopup();
            }
            if (ui.Button("Open modal")) ui.OpenPopup("Advanced modal");
            if (ui.BeginPopupModal("Advanced modal")) {
                ui.Text("Modal popup");
                if (ui.Button("Close modal")) ui.CloseCurrentPopup();
                ui.EndPopup();
            }

            if (ui.Button("Drag payload")) {}
            if (ui.BeginDragSource()) {
                ui.SetDragPayload("demo-text", "Fluid UI drag-and-drop");
                ui.EndDragSource();
            }
            if (ui.Button("Drop payload here")) {}
            if (ui.BeginDropTarget()) {
                ui.AcceptDrop("demo-text", droppedPayload);
                ui.EndDropTarget();
            }
            if (!droppedPayload.empty()) ui.Text(droppedPayload);

            ui.TextDisabled("Disabled explanatory text");
            if (ui.Button("Scroll window to bottom")) ui.ScrollToBottom();
            ui.Text("Virtualized rows:");
            ui.ClippedList(100, [&](int row) {
                ui.Text("Virtual row " + std::to_string(row));
            }, 24.f);
            ui.EndWindow();
        }

        if (ui.BeginWindow(themeWindow, {560, 90, 520, 250})) {
            ui.Text("Active stylesheet##ThemeTextTest");
            ui.TextWrapped(activeThemePath.string());
            if (ui.Button("Load CSS...")) {
                std::filesystem::path selectedPath;
                std::string dialogError;
                if (SelectCssFile(glfwGetWin32Window(win), selectedPath, dialogError))
                    loadTheme(selectedPath);
                else if (!dialogError.empty())
                    themeStatus = dialogError;
            }
            ui.SameLine();
            if (ui.Button("Reload CSS")) loadTheme(activeThemePath);
            ui.Toggle("Auto Reload", &autoReloadTheme);
            ui.TextWrapped(themeStatus);
            ui.EndWindow();
        }

        const DrawList& dl = ui.EndFrame();
        if (in.keyCopy) {
            const std::string selectedText = ui.SelectedConsoleText();
            if (!selectedText.empty())
                glfwSetClipboardString(win, selectedText.c_str());
        }
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
