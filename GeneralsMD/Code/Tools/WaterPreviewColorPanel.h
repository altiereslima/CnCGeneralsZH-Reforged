// Small, dependency-free color editor for the standalone water preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "PosixDevice9.h"
#include "WaterPreviewPreset.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace WaterPreviewColor {

class Panel {
public:
    Values values;
    Values defaults;
    bool visible = true;
    bool saveRequested = false;
    std::string status = "DRAG SLIDERS TO CHANGE WATER";

    bool handle(const SDL_Event &event)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_H) {
            if (!event.key.repeat) {
                visible = !visible;
                activeSlider = -1;
            }
            return true;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) activeSlider = -1;
        if (!visible) return false;
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            mouseX = event.motion.x / scale;
            mouseY = event.motion.y / scale;
            if (activeSlider >= 0) {
                updateSlider(activeSlider, mouseX);
                return true;
            }
            return inside(mouseX, mouseY);
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            mouseX = event.button.x / scale;
            mouseY = event.button.y / scale;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const bool consumed = activeSlider >= 0 || inside(mouseX, mouseY);
                if (event.button.button == SDL_BUTTON_LEFT) activeSlider = -1;
                return consumed;
            }
            if (!inside(mouseX, mouseY)) return false;
            if (event.button.button == SDL_BUTTON_LEFT) {
                for (int row = 0; row < 5; ++row) {
                    if (mouseY >= trackY(row) - 12 && mouseY <= trackY(row) + 12 &&
                        mouseX >= Left + 12 && mouseX <= Left + Width - 12) {
                        activeSlider = row;
                        updateSlider(row, mouseX);
                        return true;
                    }
                }
                if (hit(mouseX, mouseY, Left + 16, Top + 351, 122, 34)) {
                    values = defaults;
                    status = "UNSAVED CHANGES";
                } else if (hit(mouseX, mouseY, Left + 150, Top + 351, 122, 34)) {
                    saveRequested = true;
                    status = "SAVING COLOR...";
                }
            }
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            mouseX = event.wheel.mouse_x / scale;
            mouseY = event.wheel.mouse_y / scale;
            return inside(mouseX, mouseY);
        }
        return false;
    }

    void draw(PosixDevice9 *device, unsigned width, unsigned height)
    {
        if (!visible || !device || !width || !height) return;
        scale = std::min(1.f, std::min(float(width) / (Width + 32), float(height) / (Height + 32)));
        vertices.clear();
        vertices.reserve(16000);

        rect(Left + 4, Top + 5, Width, Height, 0x60000000);
        rect(Left, Top, Width, Height, 0xf51a2027);
        rect(Left, Top, Width, 3, 0xff93bd75);
        rect(Left, Top + Height - 1, Width, 1, 0xff3a4650);
        text(Left + 16, Top + 16, "WATER PREVIEW", 2.f, 0xffeaf0e6);
        text(Left + 16, Top + 39, "LIVE COLOR MODIFIER", 1.5f, 0xff9caeb9);

        const unsigned r = byte(values.red), g = byte(values.green), b = byte(values.blue);
        const uint32_t tint = 0xff000000u | (r << 16) | (g << 8) | b;
        rect(Left + 16, Top + 59, 256, 36, 0xff66736b);
        rect(Left + 17, Top + 60, 254, 34, tint);
        const uint32_t swatchText = .2126f * r + .7152f * g + .0722f * b > 145 ? 0xff101710 : 0xfff8fff4;
        text(Left + 26, Top + 72, "TINT", 1.5f, swatchText);
        char hex[12];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
        text(Left + 178, Top + 70, hex, 2.f, swatchText);

        const char *labels[] = {"RED", "GREEN", "BLUE", "BRIGHTNESS", "POLLUTION"};
        const uint32_t accents[] = {0xffe58484, 0xff91c788, 0xff83acdc, 0xffd0c78f, 0xffa2bf7c};
        for (int row = 0; row < 5; ++row) {
            const float labelY = Top + 111 + 45 * row;
            const float value = component(row);
            const float low = row == 3 ? .1f : 0.f;
            const float fraction = clamp((value - low) / (1.f - low));
            const float knobX = Left + 21 + fraction * 246;
            char amount[16];
            std::snprintf(amount, sizeof(amount), "%3d%%", int(std::lround(value * 100.f)));
            text(Left + 16, labelY, labels[row], 1.5f, 0xffd8e1e5);
            text(Left + 224, labelY, amount, 1.5f, accents[row]);
            rect(Left + 21, trackY(row) - 3, 246, 6, 0xff3c4650);
            rect(Left + 21, trackY(row) - 3, fraction * 246, 6, accents[row]);
            if (activeSlider == row) rect(knobX - 7, trackY(row) - 10, 14, 20, 0x508cdcab);
            rect(knobX - 4, trackY(row) - 8, 8, 16, 0xffedf3eb);
            rect(knobX - 2, trackY(row) - 5, 4, 10, accents[row]);
        }

        button(Left + 16, Top + 351, "RESET COLOR", false);
        button(Left + 150, Top + 351, "SAVE COLOR", true);
        text(Left + 16, Top + 397, status.substr(0, 28).c_str(), 1.5f, 0xffa9b9a2);
        text(Left + 16, Top + 420, "H PANEL  L LOAD  E EXPORT INI", 1.f, 0xff879ba8);

        device->SetPixelShader(nullptr);
        device->SetVertexShader(nullptr);
        device->SetRenderState(D3DRS_LIGHTING, 0);
        device->SetRenderState(D3DRS_FOGENABLE, 0);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, 0);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
        device->SetTexture(0, nullptr);
        device->SetTexture(1, nullptr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<unsigned>(vertices.size() / 3), vertices.data(), sizeof(Vertex));
    }

private:
    struct Vertex { float x, y, z, rhw; uint32_t color; };
    static constexpr float Left = 16, Top = 16, Width = 288, Height = 442;
    float scale = 1.f, mouseX = -1.f, mouseY = -1.f;
    int activeSlider = -1;
    std::vector<Vertex> vertices;

    static float clamp(float value) { return std::max(0.f, std::min(1.f, value)); }
    static unsigned byte(float value) { return static_cast<unsigned>(std::lround(clamp(value) * 255.f)); }
    static float trackY(int row) { return Top + 134 + 45 * row; }
    static bool hit(float x, float y, float left, float top, float w, float h)
    { return x >= left && x < left + w && y >= top && y < top + h; }
    static bool inside(float x, float y) { return hit(x, y, Left, Top, Width, Height); }
    float &component(int row)
    {
        switch (row) {
            case 0: return values.red;
            case 1: return values.green;
            case 2: return values.blue;
            case 3: return values.brightness;
            default: return values.strength;
        }
    }
    void updateSlider(int row, float x)
    {
        const float fraction = clamp((x - Left - 21) / 246.f);
        const float low = row == 3 ? .1f : 0.f;
        const float next = low + fraction * (1.f - low);
        if (component(row) != next) {
            component(row) = next;
            status = "UNSAVED CHANGES";
        }
    }
    void rect(float x, float y, float w, float h, uint32_t color)
    {
        if (w <= 0 || h <= 0) return;
        const Vertex a = {x * scale, y * scale, 0.f, 1.f, color};
        const Vertex b = {(x + w) * scale, y * scale, 0.f, 1.f, color};
        const Vertex c = {(x + w) * scale, (y + h) * scale, 0.f, 1.f, color};
        const Vertex d = {x * scale, (y + h) * scale, 0.f, 1.f, color};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    void button(float x, float y, const char *label, bool primary)
    {
        const bool hover = hit(mouseX, mouseY, x, y, 122, 34);
        rect(x, y, 122, 34, primary ? 0xff95ba79 : 0xff56636d);
        rect(x + 1, y + 1, 120, 32, primary ? (hover ? 0xff466043 : 0xff354b34) : (hover ? 0xff3e4a55 : 0xff2d3741));
        const float offset = primary ? 16.f : 11.5f;
        text(x + offset, y + 12, label, 1.5f, 0xffe4eedf);
    }

    // Each byte is one five-pixel row of a compact, original bitmap alphabet.
    static std::array<uint8_t, 7> glyph(char c)
    {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        switch (c) {
            case 'A': return {{14,17,17,31,17,17,17}};
            case 'B': return {{30,17,17,30,17,17,30}};
            case 'C': return {{14,17,16,16,16,17,14}};
            case 'D': return {{30,17,17,17,17,17,30}};
            case 'E': return {{31,16,16,30,16,16,31}};
            case 'F': return {{31,16,16,30,16,16,16}};
            case 'G': return {{14,17,16,23,17,17,15}};
            case 'H': return {{17,17,17,31,17,17,17}};
            case 'I': return {{14,4,4,4,4,4,14}};
            case 'J': return {{7,2,2,2,18,18,12}};
            case 'K': return {{17,18,20,24,20,18,17}};
            case 'L': return {{16,16,16,16,16,16,31}};
            case 'M': return {{17,27,21,21,17,17,17}};
            case 'N': return {{17,25,25,21,19,19,17}};
            case 'O': return {{14,17,17,17,17,17,14}};
            case 'P': return {{30,17,17,30,16,16,16}};
            case 'Q': return {{14,17,17,17,21,18,13}};
            case 'R': return {{30,17,17,30,20,18,17}};
            case 'S': return {{15,16,16,14,1,1,30}};
            case 'T': return {{31,4,4,4,4,4,4}};
            case 'U': return {{17,17,17,17,17,17,14}};
            case 'V': return {{17,17,17,17,17,10,4}};
            case 'W': return {{17,17,17,21,21,21,10}};
            case 'X': return {{17,17,10,4,10,17,17}};
            case 'Y': return {{17,17,10,4,4,4,4}};
            case 'Z': return {{31,1,2,4,8,16,31}};
            case '0': return {{14,17,19,21,25,17,14}};
            case '1': return {{4,12,4,4,4,4,14}};
            case '2': return {{14,17,1,2,4,8,31}};
            case '3': return {{30,1,1,14,1,1,30}};
            case '4': return {{2,6,10,18,31,2,2}};
            case '5': return {{31,16,16,30,1,1,30}};
            case '6': return {{14,16,16,30,17,17,14}};
            case '7': return {{31,1,2,4,8,8,8}};
            case '8': return {{14,17,17,14,17,17,14}};
            case '9': return {{14,17,17,15,1,1,14}};
            case '#': return {{10,10,31,10,31,10,10}};
            case '%': return {{25,25,2,4,8,19,19}};
            case '/': return {{1,1,2,4,8,16,16}};
            case '-': return {{0,0,0,31,0,0,0}};
            case '.': return {{0,0,0,0,0,6,6}};
            case ':': return {{0,6,6,0,6,6,0}};
            default: return {{0,0,0,0,0,0,0}};
        }
    }
    void text(float x, float y, const char *label, float size, uint32_t color)
    {
        for (; *label; ++label, x += 6 * size) {
            const auto rows = glyph(*label);
            for (unsigned row = 0; row < rows.size(); ++row)
                for (unsigned column = 0; column < 5; ++column)
                    if (rows[row] & (1u << (4 - column)))
                        rect(x + column * size, y + row * size, size, size, color);
        }
    }
};

} // namespace WaterPreviewColor
