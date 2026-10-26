// Standalone water-material editor using the production SDL device, FFT, shader and INI rules.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/EngineShaderName.h"
#include "Platform/ModernWaterConfig.h"
#include "Platform/ModernWaterMaterial.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "WaterPreviewColorPanel.h"

namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr float WaterLevel = 40.0f;
constexpr unsigned Grid = 220;
constexpr float Extent = 1100.0f;
constexpr RenderUInt32 WaterFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX4;

struct Options {
    unsigned width = 1280, height = 800, frames = 0;
    float time = 18.0f, strength = 0.80f, brightness = 0.67f;
    float mapDepth = 2.0f, mapOpacity = 0.8f;
    int mode = 2;
    bool offscreen = false, bubbles = true;
    bool controls = true, strengthSet = false, brightnessSet = false, bubblesSet = false;
    bool loadPreset = false;
    std::string screenshot, colour, config, exportConfig;
    std::string mapColour = "324B32", mapTexture = "TSWater03.tga";
};

void help()
{
    std::puts(
        "Water Preview - standalone production-water material editor\n"
        "Usage: water_preview [--offscreen] [--frames N] [--screenshot FILE.png]\n"
        "       [--mode clean|material|split] [--no-bubbles] [--strength 0..1]\n"
        "       [--color RRGGBB] [--brightness 0.1..1] [--hide-controls]\n"
        "       [--config FILE.ini] [--map-color RRGGBB] [--map-texture NAME]\n"
        "       [--map-depth N] [--map-opacity 0..1] [--export-config FILE.ini]\n"
        "       [--load-preset] [--width N] [--height N] [--time SECONDS]\n"
        "\n1 clean baseline | 2 resolved material | 3 split (baseline left / material right)\n"
        "B bubbles | Space pause | drag / arrows orbit | wheel / +/- zoom\n"
        "[ / ] pollution | R reset camera | S save PNG | E export rule | Escape close\n"
        "H color panel: RGB, brightness, pollution; Save color stores an editor preset.\n"
        "L loads saved editor colors; --load-preset restores them at startup.\n"
        "Ordinary starts resolve the INI rules.\n"
        "E writes --export-config or water-preview-rule.ini in the working directory.\n"
        "The texture name is a matching input; authored texture pixels are not loaded.\n"
        "\nNo game data is required. No game/save files are changed.\n"
        "--offscreen defaults to one frame; captures use deterministic 1/60 s steps.");
}

std::string hexColour(const char *text)
{
    const std::string value=text;
    if (value.size()!=6 || value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
        throw std::runtime_error("color must be six hexadecimal digits RRGGBB");
    return value;
}

ModernWaterConfig::Color mapColour(const std::string &value)
{
    const unsigned rgb=unsigned(std::strtoul(value.c_str(),nullptr,16));
    return {int((rgb>>16)&255),int((rgb>>8)&255),int(rgb&255)};
}

unsigned integer(const char *text, unsigned low, unsigned high)
{
    char *end = nullptr;
    const unsigned long n = std::strtoul(text, &end, 10);
    if (!*text || *end || n < low || n > high)
        throw std::runtime_error(std::string("invalid integer: ") + text);
    return static_cast<unsigned>(n);
}

float number(const char *text, float low, float high)
{
    char *end = nullptr;
    const float n = std::strtof(text, &end);
    if (!*text || *end || !std::isfinite(n) || n < low || n > high)
        throw std::runtime_error(std::string("invalid number: ") + text);
    return n;
}

Options parse(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() {
            if (i + 1 == argc) throw std::runtime_error("missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--offscreen") o.offscreen = true;
        else if (arg == "--no-bubbles") { o.bubbles = false; o.bubblesSet = true; }
        else if (arg == "--width") o.width = integer(value(), 320, 4096);
        else if (arg == "--height") o.height = integer(value(), 240, 4096);
        else if (arg == "--frames") o.frames = integer(value(), 1, 1000000);
        else if (arg == "--time") o.time = number(value(), 0, 1000000);
        else if (arg == "--strength") { o.strength = number(value(), 0, 1); o.strengthSet = true; }
        else if (arg == "--brightness") { o.brightness = number(value(), 0.1f, 1); o.brightnessSet = true; }
        else if (arg == "--hide-controls") o.controls = false;
        else if (arg == "--load-preset") o.loadPreset = true;
        else if (arg == "--color") o.colour = hexColour(value());
        else if (arg == "--map-color") o.mapColour = hexColour(value());
        else if (arg == "--map-texture") {
            o.mapTexture=value();
            if (o.mapTexture.empty() || o.mapTexture.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.\\/")!=std::string::npos)
                throw std::runtime_error("map texture must be a filename without spaces or INI delimiters");
        }
        else if (arg == "--map-depth") o.mapDepth = number(value(), 0.001f, 1000000);
        else if (arg == "--map-opacity") o.mapOpacity = number(value(), 0, 1);
        else if (arg == "--config") o.config = value();
        else if (arg == "--export-config") o.exportConfig = value();
        else if (arg == "--screenshot") o.screenshot = value();
        else if (arg == "--mode") {
            const std::string mode = value();
            if (mode == "clean") o.mode = 1;
            else if (mode == "polluted" || mode == "material") o.mode = 2;
            else if (mode == "split") o.mode = 3;
            else throw std::runtime_error("mode must be clean, material (or polluted), or split");
        } else throw std::runtime_error("unknown argument: " + arg);
    }
    if (o.offscreen && !o.frames) o.frames = 1;
    return o;
}

struct Vec3 { float x, y, z; };
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 normalized(Vec3 v) { const float s = 1.0f / std::sqrt(dot(v,v)); return {v.x*s,v.y*s,v.z*s}; }

D3DMATRIX identity()
{
    D3DMATRIX m = {};
    m._11 = m._22 = m._33 = m._44 = 1;
    return m;
}

// Row-vector, right-handed view and zero-to-one depth, as the game's D3D9 device expects.
D3DMATRIX viewMatrix(Vec3 eye, Vec3 target)
{
    const Vec3 z = normalized(subtract(eye,target));
    const Vec3 x = normalized(cross({0,0,1},z));
    const Vec3 y = cross(z,x);
    D3DMATRIX m = identity();
    m._11=x.x; m._12=y.x; m._13=z.x;
    m._21=x.y; m._22=y.y; m._23=z.y;
    m._31=x.z; m._32=y.z; m._33=z.z;
    m._41=-dot(x,eye); m._42=-dot(y,eye); m._43=-dot(z,eye);
    return m;
}

D3DMATRIX projection(float aspect)
{
    const float near = 2.0f, far = 6000.0f, scale = 1.0f / std::tan(48.0f*Pi/360.0f);
    D3DMATRIX m = {};
    m._11=scale/aspect; m._22=scale; m._33=far/(near-far);
    m._34=-1; m._43=near*far/(near-far);
    return m;
}

void require(RenderResult result, const char *operation)
{
    if (result != D3D_OK) throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}

struct WaterVertex { float x,y,z,nx,ny,nz; uint32_t colour; float uv[8]; };
struct BedVertex { float x,y,z; uint32_t colour; float u,v; };

class Preview {
public:
    explicit Preview(Options options) : options(options), time(options.time) {}
    ~Preview()
    {
        if (vertices) vertices->Release();
        if (indices) indices->Release();
        if (shader) shader->Release();
        for (auto *texture : textures) texture->Release();
        if (device) device->Release();
        if (adapter) adapter->Release();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    int run()
    {
        initialize();
        updateTitle();
        std::puts("Controls: drag color sliders; H panel; L load saved colors; 1 clean baseline, 2 material, 3 split; B bubbles; Space pause; drag/arrows orbit; wheel/+/- zoom; S PNG; E export rule; Esc close.");
        uint64_t previous = SDL_GetTicksNS();
        unsigned frame = 0;
        bool done = false;
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) handle(event, done);
            if (done) break;
            const uint64_t now = SDL_GetTicksNS();
            const float elapsed = options.frames ? 1.0f/60.0f : std::min(float(now-previous)*1.0e-9f,0.05f);
            previous = now;
            if (frame && !paused) time += elapsed;
            keyboard(elapsed);
            draw();
            const bool last = options.frames && frame+1 >= options.frames;
            if (capture || (last && !options.screenshot.empty())) {
                save(options.screenshot.empty() ? "water-preview.png" : options.screenshot);
                capture = false;
            }
            require(device->Present(nullptr,nullptr,nullptr,nullptr),"present");
            ++frame;
            if (last) done = true;
        }
        if (frame) std::printf("Rendered %u frames; %u recorded draws; %llu production FFT updates; no refused draws.\n",
            frame,device->Draws_Recorded(),static_cast<unsigned long long>(device->Get_Gpu()->Ocean_Updates()));
        return 0;
    }

private:
    Options options;
    SDL_Window *window = nullptr;
    IDirect3D9 *adapter = nullptr;
    PosixDevice9 *device = nullptr;
    IDirect3DPixelShader9 *shader = nullptr;
    IDirect3DVertexBuffer9 *vertices = nullptr;
    IDirect3DIndexBuffer9 *indices = nullptr;
    std::vector<IDirect3DTexture9 *> textures;
    IDirect3DTexture9 *bed = nullptr, *height = nullptr, *optical = nullptr, *material = nullptr;
    float time, yaw = 0.10f, pitch = 0.82f, distance = 370.0f;
    bool paused = false, dragging = false, capture = false;
    WaterPreviewColor::Panel colourPanel;
    std::string colourSettingsPath;
    ModernWaterConfig::Material resolvedMaterial;
    std::string loadedConfigPath;

    void loadMaterial()
    {
        auto rules=ModernWaterConfig::Defaults();
        std::string path=options.config;
        if (path.empty()) {
            const char *base=SDL_GetBasePath();
            if (base) {
                for (const char *suffix : {"overlay/Data/INI/ModernWater.ini","Data/INI/ModernWater.ini"}) {
                    const std::string candidate=std::string(base)+suffix;
                    if (std::filesystem::exists(candidate)) { path=candidate; break; }
                }
            }
        }
        if (!path.empty()) {
            std::ifstream input(path,std::ios::binary);
            if (input) {
                const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
                std::string error;
                if (!ModernWaterConfig::Parse(text,rules,error))
                    throw std::runtime_error("invalid ModernWater INI "+path+": "+error);
                loadedConfigPath = path;
                std::printf("Modern water configuration: %s\n",path.c_str());
            } else if (std::filesystem::exists(path)) {
                throw std::runtime_error("cannot read ModernWater INI: "+path);
            } else std::printf("Modern water configuration absent (%s); using built-in defaults.\n",path.c_str());
        } else std::puts("Modern water configuration absent; using built-in defaults.");
        resolvedMaterial=ModernWaterConfig::Resolve(rules,mapColour(options.mapColour),options.mapTexture);
        auto &v=colourPanel.values;
        v.red=resolvedMaterial.color.r/255.f;
        v.green=resolvedMaterial.color.g/255.f;
        v.blue=resolvedMaterial.color.b/255.f;
        v.brightness=resolvedMaterial.brightness;
        v.strength=resolvedMaterial.pollution;
        colourPanel.defaults=v;
        if (!options.bubblesSet) options.bubbles=resolvedMaterial.bubbles;
        colourPanel.status=resolvedMaterial.ruleName.empty()?"MAP COLOR - NO RULE MATCH":"INI MATERIAL LOADED";
        std::printf("Map input: #%s, %s; material: %s; tint #%02X%02X%02X, brightness %.2f, pollution %.2f, bubbles %s.\n",
            options.mapColour.c_str(),options.mapTexture.c_str(),
            resolvedMaterial.ruleName.empty()?"map color fallback":resolvedMaterial.ruleName.c_str(),
            resolvedMaterial.color.r,resolvedMaterial.color.g,resolvedMaterial.color.b,
            v.brightness,v.strength,options.bubbles?"yes":"no");
    }

    void loadColour()
    {
        char *folder=SDL_GetPrefPath("CnCGeneralsZH-Reforged","WaterPreview");
        if (folder) { colourSettingsPath=std::string(folder)+"colors.ini"; SDL_free(folder); }
        // The INI is authoritative unless loading a local editor preset is requested explicitly.
        if (options.loadPreset && !loadSavedColour())
            throw std::runtime_error("no valid saved preview preset at "+colourSettingsPath);
        auto &v=colourPanel.values;
        if (options.strengthSet) v.strength=options.strength;
        if (options.brightnessSet) v.brightness=options.brightness;
        if (!options.colour.empty()) {
            const unsigned rgb=unsigned(std::strtoul(options.colour.c_str(),nullptr,16));
            v.red=float((rgb>>16)&255)/255; v.green=float((rgb>>8)&255)/255; v.blue=float(rgb&255)/255;
        }
        if (options.strengthSet || options.brightnessSet || !options.colour.empty())
            colourPanel.status="UNSAVED CHANGES";
        colourPanel.visible=options.controls;
    }

    bool loadSavedColour()
    {
        if (colourSettingsPath.empty()) { colourPanel.status="LOAD UNAVAILABLE"; return false; }
        std::ifstream input(colourSettingsPath);
        if (!input) { colourPanel.status="NO SAVED COLOR FOUND"; return false; }
        WaterPreviewColor::Values saved;
        if (!WaterPreviewColor::readPreset(input, saved)) {
            colourPanel.status="SAVED COLOR INVALID";
            return false;
        }
        colourPanel.values=saved;
        colourPanel.status="SAVED COLOR LOADED";
        std::printf("Loaded preview color: %s\n",colourSettingsPath.c_str());
        return true;
    }

    void saveColour()
    {
        colourPanel.saveRequested=false;
        if (colourSettingsPath.empty()) { colourPanel.status="SAVE UNAVAILABLE"; return; }
        const auto &v=colourPanel.values;
        const std::string temporary=colourSettingsPath+".tmp";
        std::ofstream output(temporary,std::ios::trunc);
        output.imbue(std::locale::classic());
        output.precision(9);
        output<<"WATER_PREVIEW_COLOR_V1\n"<<v.red<<' '<<v.green<<' '<<v.blue<<' '
            <<v.brightness<<' '<<v.strength<<'\n';
        output.close();
        if (!output || std::rename(temporary.c_str(),colourSettingsPath.c_str())!=0) {
            std::remove(temporary.c_str());
            colourPanel.status="SAVE FAILED";
            return;
        }
        colourPanel.status="COLOR SAVED";
        std::printf("Saved preview color: %s\n",colourSettingsPath.c_str());
    }

    void exportRule()
    {
        const std::string path=options.exportConfig.empty()?"water-preview-rule.ini":options.exportConfig;
        if (options.exportConfig.empty()
            && WaterPreviewColor::defaultExportOverwritesInput(loadedConfigPath, path))
            throw std::runtime_error("default export would overwrite the loaded configuration; "
                "choose --export-config explicitly");
        const std::string temporary=path+".tmp";
        const auto input=mapColour(options.mapColour);
        const auto &v=colourPanel.values;
        const auto channel=[](float value) { return int(std::lround(std::clamp(value,0.f,1.f)*255)); };
        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents.precision(9);
        contents<<"; Exported by Water Preview. Add entries to either matching list as needed.\n"
            <<"ModernWaterRule PreviewMaterial\n"
            <<"  MatchColors\n    Color = R:"<<input.r<<" G:"<<input.g<<" B:"<<input.b<<"\n  End\n"
            <<"  MatchTextures\n    Texture = "<<options.mapTexture<<"\n  End\n"
            <<"  ReplacementColor = R:"<<channel(v.red)<<" G:"<<channel(v.green)<<" B:"<<channel(v.blue)<<"\n"
            <<"  Brightness = "<<v.brightness<<"\n  Pollution = "<<v.strength
            <<"\n  Bubbles = "<<(options.bubbles?"Yes":"No")<<"\nEnd\n";
        ModernWaterConfig::Rules parsed;
        std::string error;
        if (!ModernWaterConfig::Parse(contents.str(),parsed,error))
            throw std::runtime_error("cannot export material rule: "+error);
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<contents.str();
        output.close();
        if (!output || std::rename(temporary.c_str(),path.c_str())!=0) {
            std::remove(temporary.c_str());
            throw std::runtime_error("cannot write material rule: "+path);
        }
        colourPanel.status="INI RULE EXPORTED";
        std::printf("Exported ModernWater rule: %s\n",path.c_str());
    }

    IDirect3DTexture9 *texture(unsigned side, const std::vector<uint32_t> &pixels)
    {
        IDirect3DTexture9 *result = nullptr;
        require(device->CreateTexture(side,side,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&result,nullptr),"create texture");
        textures.push_back(result);
        D3DLOCKED_RECT locked = {};
        require(result->LockRect(0,&locked,nullptr,0),"lock texture");
        for (unsigned y=0; y<side; ++y)
            std::memcpy(static_cast<uint8_t *>(locked.pBits)+y*locked.Pitch,pixels.data()+y*side,side*4);
        require(result->UnlockRect(0),"unlock texture");
        return result;
    }

    void initialize()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL video: ")+SDL_GetError());
        loadMaterial();
        loadColour();
        if (!options.exportConfig.empty()) exportRule();
        if (!options.offscreen) {
            window = SDL_CreateWindow("Water Preview",options.width,options.height,0);
            if (!window) throw std::runtime_error(std::string("create window: ")+SDL_GetError());
        } else SDL_SetHint("ZH_OFFSCREEN_FRAMES","1");
        adapter = Direct3DCreate9(D3D_SDK_VERSION);
        if (!adapter) throw std::runtime_error("create D3D9-shaped SDL adapter");
        D3DPRESENT_PARAMETERS parameters = {};
        parameters.BackBufferWidth=options.width; parameters.BackBufferHeight=options.height;
        parameters.BackBufferFormat=D3DFMT_X8R8G8B8;
        parameters.hDeviceWindow=reinterpret_cast<RenderWindow>(window);
        parameters.Windowed=1; parameters.EnableAutoDepthStencil=1; parameters.AutoDepthStencilFormat=D3DFMT_D24S8;
        IDirect3DDevice9 *created = nullptr;
        require(adapter->CreateDevice(0,D3DDEVTYPE_HAL,parameters.hDeviceWindow,0,&parameters,&created),"create GPU device");
        device = static_cast<PosixDevice9 *>(created);
        if (!device->Get_Gpu()) throw std::runtime_error("no SDL GPU frame available");
        std::printf("Renderer: %s; %ux%u; standalone production-water preview\n",SDL_GetGPUDeviceDriver(device->Get_Gpu()->Device()),options.width,options.height);
        const RenderUInt32 tokens[] = {0xffff0101u,0x0000ffffu};
        require(device->CreatePixelShader(tokens,&shader),"create water shader");
        PosixDevice_Name_Shader(shader,"trapezoid water ps.1.1");
        createMesh();
        createTextures();
        device->SetRenderState(D3DRS_LIGHTING,0);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        device->SetRenderState(D3DRS_FOGENABLE,0);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        device->SetVertexShader(nullptr);
        for (unsigned stage=0; stage<8; ++stage) {
            device->SetSamplerState(stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
            device->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            device->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            device->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,stage);
            device->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        }
    }

    void createMesh()
    {
        std::vector<WaterVertex> data;
        data.reserve((Grid+1)*(Grid+1));
        for (unsigned y=0; y<=Grid; ++y) for (unsigned x=0; x<=Grid; ++x) {
            const float px=(float(x)/Grid*2-1)*Extent, py=(float(y)/Grid*2-1)*Extent;
            // Exact production FFT metadata: weight, free damping, offshore exposure;
            // UV0 geometric correction, UV1 attenuation and original water elevation.
            data.push_back({px,py,WaterLevel,1.0f,0.72f,0.26f,0xffffffffu,{0,0,0.72f,WaterLevel,0,0,0,0}});
        }
        std::vector<uint16_t> faces;
        faces.reserve(Grid*Grid*6);
        for (unsigned y=0; y<Grid; ++y) for (unsigned x=0; x<Grid; ++x) {
            const uint16_t a=uint16_t(y*(Grid+1)+x), b=uint16_t(a+1), c=uint16_t(a+Grid+1), d=uint16_t(c+1);
            faces.insert(faces.end(),{a,b,c,b,d,c});
        }
        require(device->CreateVertexBuffer(unsigned(data.size()*sizeof(WaterVertex)),D3DUSAGE_WRITEONLY,WaterFVF,D3DPOOL_MANAGED,&vertices,nullptr),"create water vertices");
        void *memory = nullptr;
        require(vertices->Lock(0,0,&memory,0),"lock water vertices");
        std::memcpy(memory,data.data(),data.size()*sizeof(WaterVertex));
        require(vertices->Unlock(),"unlock water vertices");
        require(device->CreateIndexBuffer(unsigned(faces.size()*sizeof(uint16_t)),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices,nullptr),"create water indices");
        require(indices->Lock(0,0,&memory,0),"lock water indices");
        std::memcpy(memory,faces.data(),faces.size()*sizeof(uint16_t));
        require(indices->Unlock(),"unlock water indices");
    }

    void createTextures()
    {
        std::vector<uint32_t> colour(256*256), detail(256*256);
        for (unsigned y=0; y<256; ++y) for (unsigned x=0; x<256; ++x) {
            uint32_t hash=(x*1973u+y*9277u+89173u); hash=(hash^(hash>>13))*1274126177u;
            const float grain=float(hash&255)/255.0f;
            const float variation=std::sin(float(x)*0.068f+std::sin(float(y)*0.037f)*2)*0.04f;
            const auto channel=[](float c) { return uint32_t(std::clamp(c,0.0f,1.0f)*255.0f); };
            colour[y*256+x]=0xff000000u|(channel(.39f+grain*.11f+variation)<<16)
                |(channel(.36f+grain*.09f+variation)<<8)|channel(.27f+grain*.07f+variation);
            detail[y*256+x]=0xff000000u|(channel(.49f+grain*.12f)<<16)|(channel(.40f+grain*.10f)<<8)|channel(.45f+grain*.10f);
        }
        bed=texture(256,colour);
        material=texture(256,detail);
        height=texture(1,{0x00000000u}); // zero-height bed, alpha zero disables wave cages
        optical=texture(1,{0xff430000u}); // enclosed-water exposure: 0.263, no coastline
    }

    void camera()
    {
        pitch=std::clamp(pitch,0.48f,1.38f); distance=std::clamp(distance,180.0f,1050.0f);
        const Vec3 offset={std::sin(yaw)*std::cos(pitch),-std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
        const Vec3 right=normalized(cross({0,0,1},offset)),up=cross(offset,right);
        const float tangent=std::tan(48.0f*Pi/360.0f),aspect=float(options.width)/options.height;
        float farthest=0;
        for (int x : {-1,1}) for (int y : {-1,1}) {
            const Vec3 ray={-offset.x+tangent*(up.x*y+right.x*x*aspect),
                -offset.y+tangent*(up.y*y+right.y*x*aspect),-offset.z+tangent*(up.z*y+right.z*x*aspect)};
            const float travel=-offset.z/ray.z;
            farthest=std::max(farthest,std::max(std::abs(offset.x+ray.x*travel),std::abs(offset.y+ray.y*travel)));
        }
        // Keep the finite patch outside every camera ray, including wide windows and
        // low viewpoints. A margin covers the actual wave displacement near the boundary.
        distance=std::min(distance,Extent*.90f/std::max(farthest,.01f));
        const Vec3 eye={offset.x*distance,offset.y*distance,WaterLevel+offset.z*distance};
        const D3DMATRIX world=identity(),view=viewMatrix(eye,{0,0,WaterLevel}),project=projection(float(options.width)/options.height);
        device->SetTransform(D3DTS_WORLD,&world);
        device->SetTransform(D3DTS_VIEW,&view);
        device->SetTransform(D3DTS_PROJECTION,&project);
    }

    void draw()
    {
        camera();
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff1b2830u,1,0),"clear frame");
        require(device->BeginScene(),"begin scene");
        device->SetPixelShader(nullptr);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE,1);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
        device->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffffu);
        for (unsigned stage=0; stage<16; ++stage) device->SetTexture(stage,nullptr);
        device->SetTexture(0,bed);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        const float edge=1800,uv=edge/80;
        const BedVertex floor[]={{-edge,-edge,0,0xffffffffu,-uv,-uv},{edge,-edge,0,0xffffffffu,uv,-uv},{-edge,edge,0,0xffffffffu,-uv,uv},
            {edge,-edge,0,0xffffffffu,uv,-uv},{edge,edge,0,0xffffffffu,uv,uv},{-edge,edge,0,0xffffffffu,-uv,uv}};
        require(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,floor,sizeof(BedVertex)),"draw submerged bed");

        device->SetTexture(0,nullptr);
        device->SetTexture(2,height);
        device->SetTexture(4,optical);
        device->SetTexture(7,material);
        device->SetPixelShader(shader);
        const float constants[7][4]={{time,0,0,WaterLevel},{-Extent,-Extent,Extent,Extent},
            {-.30f,-.45f,.84f,0},{.88f,.81f,.66f,1},{Extent,Extent*2,100,1},
            {options.mapDepth,options.mapOpacity,1,1},{0,.26f,.28f,.30f}};
        device->SetPixelShaderConstantF(1,&constants[0][0],7);
        const float fill[4][4]={{0,0,0,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
        device->SetPixelShaderConstantF(90,&fill[0][0],4);
        device->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffffu);
        device->SetRenderState(D3DRS_ZWRITEENABLE,0);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
        device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        device->SetFVF(WaterFVF);
        device->SetStreamSource(0,vertices,0,sizeof(WaterVertex));
        device->SetIndices(indices);
        if (options.mode==3) {
            // Crop the same projection into two viewports. The device's viewport scissor
            // supplies the comparison boundary; production shading needs no split hook.
            drawWater(false,0,options.width/2);
            drawWater(true,options.width/2,options.width-options.width/2);
            const D3DVIEWPORT9 viewport={0,0,options.width,options.height,0,1};
            require(device->SetViewport(&viewport),"restore viewport");
            const D3DMATRIX project=projection(float(options.width)/options.height);
            require(device->SetTransform(D3DTS_PROJECTION,&project),"restore projection");
        } else drawWater(options.mode!=1,0,options.width);
        colourPanel.draw(device,options.width,options.height);
        require(device->EndScene(),"end scene");
        if (!device->Draw_Refusals().empty()) {
            std::string why="GPU refused a draw:";
            for (const auto &entry:device->Draw_Refusals()) why+=" "+entry.first+";";
            throw std::runtime_error(why);
        }
        if (!device->Get_Gpu()->Ocean_Updates()) throw std::runtime_error("production FFT did not initialize");
    }

    void drawWater(bool enabled,unsigned left,unsigned width)
    {
        const D3DVIEWPORT9 viewport={left,0,width,options.height,0,1};
        require(device->SetViewport(&viewport),"set water viewport");
        D3DMATRIX project=projection(float(options.width)/options.height);
        project._11*=float(options.width)/width;
        const float offset=(float(options.width)-2.f*left-width)/width;
        project._31+=offset*project._34;
        require(device->SetTransform(D3DTS_PROJECTION,&project),"set water projection");
        const auto &v=colourPanel.values;
        const float colour[4]={v.red,v.green,v.blue,v.brightness};
        // Editing a neutral fallback creates an explicit tint without changing pollution.
        const float tint=resolvedMaterial.tintAmount>0 || v.red<1 || v.green<1 || v.blue<1 || v.brightness<1 ? 1.f : 0.f;
        const float parameters[4]={enabled?tint:0.f,enabled?v.strength:0.f,enabled&&options.bubbles?1.f:0.f,0.f};
        require(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterColor,colour,1),"set water material color");
        require(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterParameters,parameters,1),"set water material parameters");
        require(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,(Grid+1)*(Grid+1),0,Grid*Grid*2),"draw modern water");
    }

    void save(const std::string &path)
    {
        std::vector<uint8_t> pixels;
        if (!device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),options.width,options.height,pixels))
            throw std::runtime_error("read screenshot from GPU");
        SDL_Surface *surface=SDL_CreateSurfaceFrom(options.width,options.height,SDL_PIXELFORMAT_BGRA32,pixels.data(),options.width*4);
        if (!surface) throw std::runtime_error(std::string("create screenshot surface: ")+SDL_GetError());
        const bool bmp=path.size()>=4 && path.substr(path.size()-4)==".bmp";
        const bool saved=bmp?SDL_SaveBMP(surface,path.c_str()):SDL_SavePNG(surface,path.c_str());
        SDL_DestroySurface(surface);
        if (!saved) throw std::runtime_error(std::string("save screenshot: ")+SDL_GetError());
        std::printf("Screenshot: %s\n",path.c_str());
    }

    void updateTitle()
    {
        if (!window) return;
        char title[320];
        const char *mode=options.mode==1?"Clean baseline":options.mode==2?"Resolved material":"Baseline LEFT | Material RIGHT";
        std::snprintf(title,sizeof(title),"Water Preview: %s | brightness %.0f%% | pollution %.0f%% | bubbles %s | H panel, E export%s",
            mode,colourPanel.values.brightness*100,colourPanel.values.strength*100,options.bubbles?"ON":"OFF",paused?" | PAUSED":"");
        SDL_SetWindowTitle(window,title);
    }

    void handle(const SDL_Event &event, bool &done)
    {
        if (event.type==SDL_EVENT_WINDOW_FOCUS_LOST) dragging=false;
        if (colourPanel.handle(event)) {
            dragging=false;
            if (colourPanel.saveRequested) saveColour();
            updateTitle();
            return;
        }
        if (event.type==SDL_EVENT_QUIT) done=true;
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) dragging=true;
        else if (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) dragging=false;
        else if (event.type==SDL_EVENT_MOUSE_MOTION && dragging) {yaw-=event.motion.xrel*.005f;pitch+=event.motion.yrel*.004f;}
        else if (event.type==SDL_EVENT_MOUSE_WHEEL) distance*=std::exp(-event.wheel.y*.09f);
        else if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            switch (event.key.key) {
                case SDLK_ESCAPE: done=true; break;
                case SDLK_1: options.mode=1; break;
                case SDLK_2: options.mode=2; break;
                case SDLK_3: options.mode=3; break;
                case SDLK_B: options.bubbles=!options.bubbles; colourPanel.status="UNSAVED CHANGES"; break;
                case SDLK_SPACE: paused=!paused; break;
                case SDLK_R: yaw=.10f;pitch=.82f;distance=370; break;
                case SDLK_S: capture=true; break;
                case SDLK_L: colourPanel.visible=true; loadSavedColour(); break;
                case SDLK_E:
                    try { exportRule(); }
                    catch (const std::exception &error) { colourPanel.status=std::string("EXPORT FAILED: ")+error.what(); }
                    break;
                case SDLK_LEFTBRACKET: colourPanel.values.strength=std::max(0.0f,colourPanel.values.strength-.05f); colourPanel.status="UNSAVED CHANGES"; break;
                case SDLK_RIGHTBRACKET: colourPanel.values.strength=std::min(1.0f,colourPanel.values.strength+.05f); colourPanel.status="UNSAVED CHANGES"; break;
                default: break;
            }
            updateTitle();
        }
    }

    void keyboard(float dt)
    {
        const bool *keys=SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) yaw-=dt*.65f;
        if (keys[SDL_SCANCODE_RIGHT]) yaw+=dt*.65f;
        if (keys[SDL_SCANCODE_UP]) pitch+=dt*.5f;
        if (keys[SDL_SCANCODE_DOWN]) pitch-=dt*.5f;
        if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) distance*=std::exp(-dt*.7f);
        if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) distance*=std::exp(dt*.7f);
    }
};
} // namespace

int main(int argc, char **argv)
{
    for (int i=1;i<argc;++i) if (std::strcmp(argv[i],"--help")==0 || std::strcmp(argv[i],"-h")==0) {help();return 0;}
    try { Preview preview(parse(argc,argv)); return preview.run(); }
    catch (const std::exception &error) { std::fprintf(stderr,"water_preview: %s\n",error.what()); return 1; }
}
