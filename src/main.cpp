/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <SDL2/SDL.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/SystemInterface.h>

#include "bitmap_font_engine.hpp"
#include "moonlight_app.hpp"
#include "frame_cadence.hpp"
#include "moonlight_stream.hpp"
#include "native_agc_present.hpp"
#include "ps5_pngdec.hpp"
#include "radio_input.hpp"
#include "radio_ime.hpp"
#include "ui_sound.hpp"
#if PROSPEROLIGHT_PYROWAVE
#include "pyrowave/video/ps5_presentation_stats.hpp"
#endif

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <pthread.h>
#include <vector>

extern "C" int sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *parameters);
extern "C" int sceVideoOutClose(int32_t handle);
extern "C" int sceVideoOutConfigureOutput(int32_t handle, uint32_t request, const void *,
                                          const void *, const void *);

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" std::int64_t sceKernelGetDirectMemorySize(void);
extern "C" int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                             std::size_t length, std::size_t alignment,
                                             int memory_type, std::int64_t *direct_memory_start);
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t length, int protection,
                                        int flags, std::int64_t direct_memory_start,
                                        std::size_t alignment);
extern "C" int sceKernelReleaseDirectMemory(std::int64_t direct_memory_start, std::size_t length);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" int sceSysmoduleLoadModule(std::uint16_t module_id);
extern "C" void *mmap(void *address, std::size_t length, int protection, int flags, int descriptor,
                      long offset);
extern "C" int munmap(void *address, std::size_t length);
extern "C" void *__dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

#ifndef PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS
#define PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS 0
#endif

static_assert(PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 0 ||
                  PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 90 ||
                  PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 120,
              "VIDEO_OUTPUT_SELF_TEST_FPS must be 0, 90, or 120");

namespace
{

constexpr std::size_t kMappedAllocationThreshold = 64 * 1024;
constexpr std::uint64_t kAllocationMagic = UINT64_C(0x524144494F4D454D);
constexpr int kProtectionReadWrite = 3;
constexpr int kMapPrivateAnonymous = 0x1002;
constexpr std::uint16_t kPngDecModule = 0x008c;
constexpr std::uint32_t kArtworkWidth = 144;
constexpr std::uint32_t kArtworkHeight = 204;
// HFR-capable titles switch the HDMI link before user code starts. Keep our
// replacement artwork up long enough for slower TVs to lock onto 4K/120,
// then fade it at the launcher's 60 Hz presentation cadence.
constexpr std::uint32_t kStartupHoldMilliseconds = 1500;
constexpr std::uint32_t kStartupFadeFrames = 45;
bool png_decoder_available = false;

struct ArtworkFit
{
    std::uint32_t width;
    std::uint32_t height;
};

constexpr ArtworkFit FitArtwork(std::uint32_t width, std::uint32_t height)
{
    return static_cast<std::uint64_t>(width) * kArtworkHeight >
                   static_cast<std::uint64_t>(height) * kArtworkWidth
               ? ArtworkFit{kArtworkWidth,
                            static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) *
                                                       kArtworkWidth / width)}
               : ArtworkFit{static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) *
                                                       kArtworkHeight / height),
                            kArtworkHeight};
}

static_assert(FitArtwork(628, 888).width == 144 && FitArtwork(628, 888).height == 203);
static_assert(FitArtwork(1920, 1080).width == 144 && FitArtwork(1920, 1080).height == 81);

static_assert(sizeof(ScePngDecCreateParam) == 12);
static_assert(sizeof(ScePngDecDecodeParam) == 32);
static_assert(sizeof(ScePngDecParseParam) == 16);
static_assert(sizeof(ScePngDecImageInfo) == 16);

struct LauncherSelection
{
    char host[MOONLIGHT_CONFIG_ADDRESS_SIZE] = {};
    char app_name[64] = {};
    int app_id = 0;
    unsigned bitrate_kbps = 20000;
    unsigned display_area = MOONLIGHT_DISPLAY_AREA_TV_SAFE;
    unsigned video_codec = MOONLIGHT_VIDEO_CODEC_H264;
    unsigned stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1080P;
    unsigned stream_fps = MOONLIGHT_STREAM_FPS_60;
    unsigned hdr_enabled = 0;
    unsigned chroma_sampling = MOONLIGHT_CHROMA_420;
    unsigned audio_configuration = MOONLIGHT_AUDIO_STEREO;
    unsigned vsync_enabled = 1;
    unsigned decoder_pipeline = MOONLIGHT_DECODER_PIPELINE_CLASSIC;
    unsigned decoder_cores = MOONLIGHT_DECODER_CORES_DEFAULT;
};

struct alignas(std::max_align_t) AllocationHeader
{
    std::uint64_t magic;
    std::size_t requested_size;
    std::size_t mapped_size;
};

void *AllocateTracked(std::size_t size)
{
    if (size == 0)
        size = 1;
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader))
        return nullptr;

    const std::size_t total = sizeof(AllocationHeader) + size;
    AllocationHeader *header = nullptr;
    std::size_t mapped_size = 0;
    if (size >= kMappedAllocationThreshold)
    {
        mapped_size = (total + 0x3fff) & ~std::size_t(0x3fff);
        void *mapping =
            mmap(nullptr, mapped_size, kProtectionReadWrite, kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void *>(-1))
        {
            header = static_cast<AllocationHeader *>(mapping);
        }
    }
    else
    {
        header = static_cast<AllocationHeader *>(std::malloc(total));
    }
    if (!header)
        return nullptr;

    header->magic = kAllocationMagic;
    header->requested_size = size;
    header->mapped_size = mapped_size;
    return header + 1;
}

void FreeTracked(void *allocation) noexcept
{
    if (!allocation)
        return;
    auto *header = static_cast<AllocationHeader *>(allocation) - 1;
    // SDL can retain small allocations made by its original allocator before
    // custom memory functions are installed. Those remain libc-owned.
    if (header->magic != kAllocationMagic)
    {
        std::free(allocation);
        return;
    }
    if (header->mapped_size != 0)
    {
        munmap(header, header->mapped_size);
    }
    else
    {
        std::free(header);
    }
}

void *CallocTracked(std::size_t count, std::size_t size)
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
        return nullptr;
    const std::size_t total = count * size;
    void *allocation = AllocateTracked(total);
    if (allocation)
        std::memset(allocation, 0, total);
    return allocation;
}

void *ReallocTracked(void *allocation, std::size_t size)
{
    if (!allocation)
        return AllocateTracked(size);
    if (size == 0)
    {
        FreeTracked(allocation);
        return nullptr;
    }

    auto *old_header = static_cast<AllocationHeader *>(allocation) - 1;
    if (old_header->magic != kAllocationMagic)
        std::abort();
    void *replacement = AllocateTracked(size);
    if (!replacement)
        return nullptr;
    std::memcpy(replacement, allocation,
                old_header->requested_size < size ? old_header->requested_size : size);
    FreeTracked(allocation);
    return replacement;
}

} // namespace

extern "C" int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT)
        return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT)
    {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" void __assert(const char *, const char *, int, const char *)
{
    std::abort();
}

extern "C" float strtof(const char *value, char **end)
{
    return static_cast<float>(strtod(value, end));
}

extern "C" int fseek(std::FILE *file, long offset, int origin)
{
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE *file)
{
    return static_cast<long>(ftello(file));
}

extern "C" char *strcasestr(const char *haystack, const char *needle)
{
    if (!*needle)
        return const_cast<char *>(haystack);
    for (; *haystack; ++haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n)
        {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc)
                break;
            ++h;
            ++n;
        }
        if (!*n)
            return const_cast<char *>(haystack);
    }
    return nullptr;
}

namespace
{

class AppSystemInterface final : public Rml::SystemInterface
{
  public:
    double GetElapsedTime() override
    {
        return static_cast<double>(SDL_GetTicks64() - start_ticks_) / 1000.0;
    }

  private:
    Uint64 start_ticks_ = SDL_GetTicks64();
};

class AppFileInterface final : public Rml::FileInterface
{
  public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            const Rml::String app_path = "/app0/" + path;
            file = std::fopen(app_path.c_str(), "rb");
        }
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle file) override
    {
        if (file)
            std::fclose(reinterpret_cast<std::FILE *>(file));
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        return std::fread(buffer, 1, size, reinterpret_cast<std::FILE *>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        return fseeko(reinterpret_cast<std::FILE *>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        return static_cast<size_t>(ftello(reinterpret_cast<std::FILE *>(file)));
    }
};

class SdlRenderInterface final : public Rml::RenderInterfaceCompatibility
{
  public:
    SdlRenderInterface(SDL_Renderer *renderer, SDL_Surface *surface)
        : renderer_(renderer), surface_(surface)
    {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }

    void RenderGeometry(Rml::Vertex *rml_vertices, int num_vertices, int *indices, int num_indices,
                        Rml::TextureHandle texture, const Rml::Vector2f &translation) override
    {
        AppTexture *app_texture = reinterpret_cast<AppTexture *>(texture);
        if (app_texture && RenderPixelAlignedQuads(rml_vertices, num_vertices, indices, num_indices,
                                                   app_texture, translation))
        {
            return;
        }

        std::vector<SDL_Vertex> vertices;
        vertices.reserve(static_cast<size_t>(num_vertices));
        for (int i = 0; i < num_vertices; ++i)
        {
            const Rml::Vertex &vertex = rml_vertices[i];
            SDL_Vertex sdl_vertex{};
            sdl_vertex.position = {vertex.position.x + translation.x,
                                   vertex.position.y + translation.y};
            sdl_vertex.color = {vertex.colour.red, vertex.colour.green, vertex.colour.blue,
                                vertex.colour.alpha};
            sdl_vertex.tex_coord = {vertex.tex_coord.x, vertex.tex_coord.y};
            vertices.push_back(sdl_vertex);
        }
        SDL_RenderGeometry(renderer_, app_texture ? app_texture->texture : nullptr, vertices.data(),
                           num_vertices, indices, num_indices);
    }

    bool LoadTexture(Rml::TextureHandle &texture_handle, Rml::Vector2i &texture_dimensions,
                     const Rml::String &source) override
    {
        texture_handle = {};
        texture_dimensions = {};

        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        bool decoded = false;

        if (source.rfind("artwork://", 0) == 0)
        {
            const int app_id = std::atoi(source.c_str() + 10);
            decoded = DecodeArtworkPng(app_id, width, height, pixels);
            if (!decoded)
                return LoadTexture(texture_handle, texture_dimensions, "icons/app-placeholder.tga");
        }
        else
        {
            std::FILE *file = std::fopen(source.c_str(), "rb");
            if (!file && !source.empty() && source[0] != '/')
            {
                const Rml::String app_path = "/app0/" + source;
                file = std::fopen(app_path.c_str(), "rb");
            }
            if (!file)
                return false;

            unsigned char header[18]{};
            const bool header_read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
            if (header_read && std::memcmp(header, "RTA1", 4) == 0)
            {
                width = ReadLe16(header + 4);
                height = ReadLe16(header + 6);
                const std::uint32_t pixel_count = ReadLe32(header + 8);
                const std::uint32_t payload_length = ReadLe32(header + 12);
                const std::size_t expected_pixels =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                if (width > 0 && height > 0 && pixel_count == expected_pixels &&
                    expected_pixels <= std::numeric_limits<std::size_t>::max() / 4 &&
                    payload_length <= expected_pixels * 2 && fseeko(file, 16, SEEK_SET) == 0)
                {
                    std::vector<unsigned char> payload(payload_length);
                    if (std::fread(payload.data(), 1, payload.size(), file) == payload.size())
                        decoded = DecodeRadioAtlas(payload, expected_pixels, pixels);
                }
            }
            else
            {
                width = header[12] | (header[13] << 8);
                height = header[14] | (header[15] << 8);
                const bool supported = header_read && header[0] == 0 && header[1] == 0 &&
                                       header[2] == 2 && width > 0 && height > 0 &&
                                       header[16] == 32 && (header[17] & 0x0f) == 8 &&
                                       (header[17] & 0x30) == 0x20;
                if (supported &&
                    static_cast<std::size_t>(width) <= std::numeric_limits<std::size_t>::max() /
                                                           (static_cast<std::size_t>(height) * 4))
                {
                    pixels.resize(static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4);
                    decoded = std::fread(pixels.data(), 1, pixels.size(), file) == pixels.size();
                }
            }
            std::fclose(file);
        }
        if (!decoded)
            return false;

        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_BGRA32);
        if (!surface)
            return false;

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture)
            return false;

        if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0 ||
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest) != 0)
        {
            SDL_DestroyTexture(texture);
            return false;
        }

        auto *app_texture = new AppTexture;
        app_texture->texture = texture;
        app_texture->width = width;
        app_texture->height = height;
        app_texture->exact_pixels = source.find("lvgl-bitmap") != Rml::String::npos;
        app_texture->rgba.resize(pixels.size());
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            app_texture->rgba[i + 0] = pixels[i + 2];
            app_texture->rgba[i + 1] = pixels[i + 1];
            app_texture->rgba[i + 2] = pixels[i + 0];
            app_texture->rgba[i + 3] = pixels[i + 3];
        }
        if (app_texture->exact_pixels)
        {
            app_texture->surface = SDL_CreateRGBSurfaceWithFormatFrom(
                app_texture->rgba.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
            if (!app_texture->surface ||
                SDL_SetSurfaceBlendMode(app_texture->surface, SDL_BLENDMODE_BLEND) != 0)
            {
                SDL_FreeSurface(app_texture->surface);
                SDL_DestroyTexture(texture);
                delete app_texture;
                return false;
            }
        }

        texture_dimensions = {width, height};
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    bool GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source,
                         const Rml::Vector2i &dimensions) override
    {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            const_cast<Rml::byte *>(source), dimensions.x, dimensions.y, 32, dimensions.x * 4,
            SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return false;

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (texture)
        {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        }
        if (!texture)
            return false;

        auto *app_texture = new AppTexture;
        app_texture->texture = texture;
        app_texture->width = dimensions.x;
        app_texture->height = dimensions.y;
        app_texture->rgba.assign(source, source + static_cast<std::size_t>(dimensions.x) *
                                                      static_cast<std::size_t>(dimensions.y) * 4);
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        auto *app_texture = reinterpret_cast<AppTexture *>(texture);
        if (!app_texture)
            return;
        SDL_FreeSurface(app_texture->surface);
        SDL_DestroyTexture(app_texture->texture);
        delete app_texture;
    }

    void EnableScissorRegion(bool enable) override
    {
        scissor_enabled_ = enable;
        SDL_RenderSetClipRect(renderer_, enable ? &scissor_ : nullptr);
    }

    void SetScissorRegion(int x, int y, int width, int height) override
    {
        scissor_ = {x, y, width, height};
        if (scissor_enabled_)
            SDL_RenderSetClipRect(renderer_, &scissor_);
    }

  private:
    struct AppTexture
    {
        SDL_Texture *texture = nullptr;
        SDL_Surface *surface = nullptr;
        int width = 0;
        int height = 0;
        bool exact_pixels = false;
        std::vector<Rml::byte> rgba;
    };

    static std::uint16_t ReadLe16(const unsigned char *value)
    {
        return static_cast<std::uint16_t>(value[0]) | (static_cast<std::uint16_t>(value[1]) << 8);
    }

    static std::uint32_t ReadLe32(const unsigned char *value)
    {
        return static_cast<std::uint32_t>(value[0]) | (static_cast<std::uint32_t>(value[1]) << 8) |
               (static_cast<std::uint32_t>(value[2]) << 16) |
               (static_cast<std::uint32_t>(value[3]) << 24);
    }

    static bool DecodeArtworkPng(int app_id, int &width, int &height,
                                 std::vector<unsigned char> &pixels)
    {
        size_t png_size = 0;
        const unsigned char *png = moonlight_backend_find_app_artwork(app_id, &png_size);
        if (!png_decoder_available || !png || png_size == 0 || png_size > UINT32_MAX)
            return false;

        ScePngDecParseParam parse{png, static_cast<std::uint32_t>(png_size), 0};
        ScePngDecImageInfo info{};
        if (scePngDecParseHeader(&parse, &info) < 0 || info.image_width == 0 ||
            info.image_height == 0)
            return false;

        const std::uint64_t image_bytes =
            static_cast<std::uint64_t>(info.image_width) * info.image_height * 4;
        if (image_bytes > 64 * 1024 * 1024 || image_bytes > UINT32_MAX)
            return false;

        ScePngDecCreateParam create{
            sizeof(ScePngDecCreateParam),
            info.bit_depth > 8 ? 1u : 0u,
            info.image_width,
        };
        const int work_size = scePngDecQueryMemorySize(&create);
        if (work_size <= 0)
            return false;

        void *work = AllocateTracked(static_cast<std::size_t>(work_size));
        void *handle = nullptr;
        if (!work ||
            scePngDecCreate(&create, work, static_cast<std::uint32_t>(work_size), &handle) < 0)
        {
            FreeTracked(work);
            return false;
        }

        std::vector<unsigned char> decoded(static_cast<std::size_t>(image_bytes));
        ScePngDecDecodeParam decode{
            png,
            decoded.data(),
            static_cast<std::uint32_t>(png_size),
            static_cast<std::uint32_t>(image_bytes),
            1,
            255,
            info.image_width * 4,
        };
        ScePngDecImageInfo output_info{};
        const int result = scePngDecDecode(handle, &decode, &output_info);
        (void)scePngDecDelete(handle);
        FreeTracked(work);
        if (result < 0)
            return false;

        const std::uint32_t source_width = info.image_width;
        const std::uint32_t source_height = info.image_height;
        const ArtworkFit fit = FitArtwork(source_width, source_height);
        const std::uint32_t fitted_width = fit.width ? fit.width : 1;
        const std::uint32_t fitted_height = fit.height ? fit.height : 1;
        const std::uint32_t left = (kArtworkWidth - fitted_width) / 2;
        const std::uint32_t top = (kArtworkHeight - fitted_height) / 2;
        pixels.assign(kArtworkWidth * kArtworkHeight * 4, 0);
        for (std::uint32_t y = 0; y < fitted_height; ++y)
        {
            const std::uint32_t source_y = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(y) * source_height) / fitted_height);
            for (std::uint32_t x = 0; x < fitted_width; ++x)
            {
                const std::uint32_t source_x = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(x) * source_width) / fitted_width);
                std::memcpy(&pixels[((top + y) * kArtworkWidth + left + x) * 4],
                            &decoded[(source_y * source_width + source_x) * 4], 4);
            }
        }
        width = kArtworkWidth;
        height = kArtworkHeight;
        return true;
    }

    static bool DecodeRadioAtlas(const std::vector<unsigned char> &payload, std::size_t pixel_count,
                                 std::vector<unsigned char> &pixels)
    {
        pixels.assign(pixel_count * 4, 255);
        std::size_t input = 0;
        std::size_t output = 0;
        while (input < payload.size() && output < pixel_count)
        {
            const unsigned char token = payload[input++];
            const std::size_t length = static_cast<std::size_t>(token & 0x7f) + 1;
            if (length > pixel_count - output)
                return false;
            if ((token & 0x80) == 0)
            {
                for (std::size_t index = 0; index < length; ++index)
                    pixels[(output + index) * 4 + 3] = 0;
            }
            else
            {
                const std::size_t bytes = (length + 1) / 2;
                if (bytes > payload.size() - input)
                    return false;
                for (std::size_t index = 0; index < length; ++index)
                {
                    const unsigned char packed = payload[input + index / 2];
                    const unsigned char alpha =
                        (index & 1) != 0 ? static_cast<unsigned char>((packed & 0x0f) * 17)
                                         : static_cast<unsigned char>((packed >> 4) * 17);
                    pixels[(output + index) * 4 + 3] = alpha;
                }
                input += bytes;
            }
            output += length;
        }
        return input == payload.size() && output == pixel_count;
    }

    struct PixelCopy
    {
        SDL_Rect source;
        SDL_Rect destination;
        Rml::ColourbPremultiplied colour;
    };

    static int RoundPixel(float value)
    {
        return static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
    }

    static bool SameColor(const Rml::Vertex &lhs, const Rml::Vertex &rhs)
    {
        return lhs.colour.red == rhs.colour.red && lhs.colour.green == rhs.colour.green &&
               lhs.colour.blue == rhs.colour.blue && lhs.colour.alpha == rhs.colour.alpha;
    }

    bool RenderPixelAlignedQuads(Rml::Vertex *vertices, int num_vertices, int *indices,
                                 int num_indices, AppTexture *texture,
                                 const Rml::Vector2f &translation)
    {
        if (num_vertices <= 0 || num_indices <= 0 || num_indices % 6 != 0)
            return false;

        const int texture_width = texture->width;
        const int texture_height = texture->height;
        if (texture_width <= 0 || texture_height <= 0)
            return false;

        const int num_quads = num_indices / 6;
        std::vector<PixelCopy> copies;
        copies.reserve(static_cast<size_t>(num_quads));
        for (int quad = 0; quad < num_quads; ++quad)
        {
            const int index = quad * 6;
            const int i0 = indices[index + 0];
            const int i3 = indices[index + 1];
            const int i1 = indices[index + 2];
            const int i2 = indices[index + 5];
            if (i0 < 0 || i0 >= num_vertices || i1 < 0 || i1 >= num_vertices || i2 < 0 ||
                i2 >= num_vertices || i3 < 0 || i3 >= num_vertices || indices[index + 3] != i1 ||
                indices[index + 4] != i3 || i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 ||
                i1 == i3 || i2 == i3)
                return false;

            const Rml::Vertex &v0 = vertices[i0];
            const Rml::Vertex &v1 = vertices[i1];
            const Rml::Vertex &v2 = vertices[i2];
            const Rml::Vertex &v3 = vertices[i3];
            if (v0.position.y != v1.position.y || v1.position.x != v2.position.x ||
                v2.position.y != v3.position.y || v3.position.x != v0.position.x ||
                v0.tex_coord.y != v1.tex_coord.y || v1.tex_coord.x != v2.tex_coord.x ||
                v2.tex_coord.y != v3.tex_coord.y || v3.tex_coord.x != v0.tex_coord.x ||
                !SameColor(v0, v1) || !SameColor(v0, v2) || !SameColor(v0, v3))
                return false;

            const int source_width = RoundPixel((v1.tex_coord.x - v0.tex_coord.x) * texture_width);
            const int source_height =
                RoundPixel((v3.tex_coord.y - v0.tex_coord.y) * texture_height);
            const int destination_width = RoundPixel(v1.position.x - v0.position.x);
            const int destination_height = RoundPixel(v3.position.y - v0.position.y);
            if (source_width <= 0 || source_height <= 0 || source_width != destination_width ||
                source_height != destination_height)
                return false;

            SDL_Rect source{RoundPixel(v0.tex_coord.x * texture_width),
                            RoundPixel(v0.tex_coord.y * texture_height), source_width,
                            source_height};
            if (source.x < 0 || source.y < 0 || source.x + source.w > texture_width ||
                source.y + source.h > texture_height)
                return false;
            copies.push_back(
                {source,
                 {RoundPixel(v0.position.x + translation.x),
                  RoundPixel(v0.position.y + translation.y), destination_width, destination_height},
                 v0.colour});
        }

        if (texture->exact_pixels)
            return CompositeExactPixels(*texture, copies);

        for (const PixelCopy &copy : copies)
        {
            SDL_SetTextureColorMod(texture->texture, copy.colour.red, copy.colour.green,
                                   copy.colour.blue);
            SDL_SetTextureAlphaMod(texture->texture, copy.colour.alpha);
            SDL_RenderCopy(renderer_, texture->texture, &copy.source, &copy.destination);
        }

        SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture->texture, 255);
        return true;
    }

    bool CompositeExactPixels(const AppTexture &texture, const std::vector<PixelCopy> &copies)
    {
        if (!surface_ || !texture.surface)
            return false;
        SDL_RenderFlush(renderer_);

        SDL_Rect old_clip{};
        SDL_GetClipRect(surface_, &old_clip);
        SDL_SetClipRect(surface_, scissor_enabled_ ? &scissor_ : nullptr);
        for (const PixelCopy &copy : copies)
        {
            SDL_SetSurfaceColorMod(texture.surface, copy.colour.red, copy.colour.green,
                                   copy.colour.blue);
            SDL_SetSurfaceAlphaMod(texture.surface, copy.colour.alpha);
            SDL_Rect destination = copy.destination;
            SDL_BlitSurface(texture.surface, &copy.source, surface_, &destination);
        }
        SDL_SetSurfaceColorMod(texture.surface, 255, 255, 255);
        SDL_SetSurfaceAlphaMod(texture.surface, 255);
        SDL_SetClipRect(surface_, &old_clip);
        return true;
    }

    SDL_Renderer *renderer_;
    SDL_Surface *surface_;
    SDL_Rect scissor_{};
    bool scissor_enabled_ = false;
};

bool LoadFonts()
{
    static constexpr const char *kBitmapFonts[] = {
        "ui/fonts/lvgl-bitmap/Montserrat-20.fnt", "ui/fonts/lvgl-bitmap/Montserrat-24.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-28.fnt", "ui/fonts/lvgl-bitmap/Montserrat-32.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-36.fnt", "ui/fonts/lvgl-bitmap/Montserrat-40.fnt",
        "ui/fonts/lvgl-bitmap/Montserrat-48.fnt",
    };
    for (const char *font : kBitmapFonts)
    {
        if (!Rml::LoadFontFace(font))
            return false;
    }
    static constexpr const char *kMultilingualFonts[] = {
        "ui/fonts/lvgl-bitmap/multilingual/Radio-20.fnt",
        "ui/fonts/lvgl-bitmap/multilingual/Radio-24.fnt",
        "ui/fonts/lvgl-bitmap/multilingual/Radio-28.fnt",
        "ui/fonts/lvgl-bitmap/multilingual/Radio-32.fnt",
    };
    for (const char *font : kMultilingualFonts)
    {
        if (!Rml::LoadFontFace(font))
            return false;
    }
    return true;
}

[[noreturn]] void KeepProcessAlive()
{
    for (;;)
        sceKernelUsleep(1000000);
}

void PresentColor(SDL_Renderer *renderer, SDL_Window *window, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

void PresentLauncher(Rml::Context *context, SDL_Renderer *renderer, SDL_Window *window)
{
    context->Update();
    SDL_SetRenderDrawColor(renderer, 7, 16, 22, 255);
    SDL_RenderClear(renderer);
    context->Render();
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

#if PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS != 0
void RunVideoOutputSelfTest()
{
    constexpr std::uint32_t pitch =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 3840
        : PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ? 2560
                                                                                         : 1920;
    constexpr std::uint32_t visible_height =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 2160
        : PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ? 1440
                                                                                         : 1080;
    constexpr std::uint32_t surface_height =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P ? 2176
        : visible_height == 1080                                                       ? 1088
                                 : visible_height;
    constexpr std::size_t visible_surface_bytes =
        static_cast<std::size_t>(pitch) * surface_height * 3 / 2;
    constexpr std::size_t surface_bytes = (visible_surface_bytes + 0x3fff) & ~std::size_t(0x3fff);
    constexpr std::uint32_t surface_count = 3;
    constexpr std::size_t surface_pool_bytes = surface_bytes * surface_count;
    constexpr std::uint32_t frame_count = PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS * 5;
    std::int64_t surface_start = -1;
    void *surface = nullptr;
    (void)std::remove("/download0/prosperolight-agc-selftest.log");
    FILE *receipt = std::fopen("/download0/prosperolight-videoout-selftest.txt", "w");

    int result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                                               surface_pool_bytes, 0x4000, 12, &surface_start);
    if (result == 0)
        result =
            sceKernelMapDirectMemory(&surface, surface_pool_bytes, 0x32, 0, surface_start, 0x4000);
    if (receipt)
    {
        std::fprintf(receipt, "requested=%u width=%u height=%u allocation_rc=%08x surface=%p\n",
                     PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS, pitch, visible_height,
                     static_cast<unsigned>(result), surface);
        std::fflush(receipt);
    }
    if (result != 0 || !surface)
    {
        if (surface_start >= 0)
            (void)sceKernelReleaseDirectMemory(surface_start, surface_pool_bytes);
        KeepProcessAlive();
    }

    int present_result = 0;
    for (std::uint32_t index = 0; index < surface_count; ++index)
    {
        void *frame_surface = static_cast<std::uint8_t *>(surface) + index * surface_bytes;
        present_result =
            native_agc_present_loading(frame_surface, visible_surface_bytes, index, 0, pitch,
                                       visible_height, PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS);
        if (present_result != 0)
            break;
    }

    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    const std::uint64_t started = SDL_GetPerformanceCounter();
    std::uint32_t presented = 0;
    for (std::uint32_t frame = 0; frame < frame_count && present_result == 0; ++frame)
    {
        void *frame_surface =
            static_cast<std::uint8_t *>(surface) + (frame % surface_count) * surface_bytes;
        present_result = native_agc_wait_source_idle(frame_surface);
        if (present_result == 0)
            present_result = native_agc_present_nv12(
                frame_surface, visible_surface_bytes, pitch, surface_height, pitch, visible_height,
                PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS, nullptr);
        if (present_result != 0)
            break;
        ++presented;
        const std::uint64_t deadline = started + static_cast<std::uint64_t>(presented) * frequency /
                                                     PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS;
        const std::uint64_t now = SDL_GetPerformanceCounter();
        if (deadline > now)
        {
            const std::uint64_t remaining_us = (deadline - now) * 1000000 / frequency;
            if (remaining_us != 0)
                sceKernelUsleep(static_cast<std::uint32_t>(remaining_us));
        }
    }
    const std::uint64_t elapsed = SDL_GetPerformanceCounter() - started;
    const std::uint64_t measured_fps_x100 =
        elapsed != 0 ? static_cast<std::uint64_t>(presented) * frequency * 100 / elapsed : 0;
    const int shutdown_result = native_agc_present_shutdown();
    const int unmap_result = munmap(surface, surface_pool_bytes);
    const int release_result = sceKernelReleaseDirectMemory(surface_start, surface_pool_bytes);
    if (receipt)
    {
        std::fprintf(receipt,
                     "presented=%u present_rc=%08x measured=%llu.%02llu shutdown_rc=%08x "
                     "unmap_rc=%08x release_rc=%08x\n",
                     presented, static_cast<unsigned>(present_result),
                     static_cast<unsigned long long>(measured_fps_x100 / 100),
                     static_cast<unsigned long long>(measured_fps_x100 % 100),
                     static_cast<unsigned>(shutdown_result), static_cast<unsigned>(unmap_result),
                     static_cast<unsigned>(release_result));
        std::fclose(receipt);
    }
    KeepProcessAlive();
}
#endif

MoonlightApp::Command RunLauncher(LauncherSelection *selection, const char *stream_error,
                                  bool play_open_sound)
{
#if PROSPEROLIGHT_PYROWAVE
    struct LauncherOutputScope
    {
        LauncherOutputScope()
        {
            ps5_launcher_output_policy(true);
        }
        ~LauncherOutputScope()
        {
            ps5_launcher_output_policy(false);
        }
    } launcher_output_scope;
#else
    // Restore fixed default output before SDL opens its launcher VideoOut handle.
    // Never retain streaming VRR/high-refresh state across a return to the menu.
    const int launcher_video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (launcher_video >= 0)
    {
        const int reset = sceVideoOutConfigureOutput(launcher_video, 1u, nullptr, nullptr, nullptr);
        std::fprintf(stderr, "Launcher fixed-output reset: rc=%08x\n",
                     static_cast<unsigned>(reset));
        (void)sceVideoOutClose(launcher_video);
    }
#endif
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        return MoonlightApp::Command::None;
    }
    const bool sound_ready = ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0 ||
                              SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) &&
                             prosperolight::ui_sound_initialize();
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window *window = SDL_CreateWindow("ProsperoLight", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer)
        return MoonlightApp::Command::None;

    const uint64_t launcher_clock = SDL_GetPerformanceFrequency();
    moonlight::FrameCadence launcher_cadence;
    launcher_cadence.reset(SDL_GetPerformanceCounter(), launcher_clock, 60);
    const auto wait_launcher_frame = [&]()
    {
        const uint64_t now = SDL_GetPerformanceCounter();
        const uint64_t deadline = launcher_cadence.next(now);
        if (deadline > now && launcher_clock)
        {
            const uint64_t wait_us = (deadline - now) * 1000000 / launcher_clock;
            if (wait_us)
                sceKernelUsleep(static_cast<uint32_t>(wait_us));
        }
    };

    AppSystemInterface system_interface;
    AppFileInterface file_interface;
    SdlRenderInterface render_interface(renderer, surface);
    BitmapFontEngine font_engine;
    Rml::RenderInterface *adapted_render_interface = render_interface.GetAdaptedInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetRenderInterface(adapted_render_interface);
    Rml::SetFontEngineInterface(&font_engine);

    bool running = Rml::Initialise();
    if (running)
        running = LoadFonts();
    Rml::Context *context =
        running ? Rml::CreateContext("moonlight-ps5", {1920, 1080}, adapted_render_interface)
                : nullptr;
    Rml::ElementDocument *document = context ? context->LoadDocument("ui/main.rml") : nullptr;
    Rml::Element *startup_splash =
        document && play_open_sound ? document->GetElementById("startup-splash") : nullptr;
    MoonlightApp app;
    MoonlightApp::Command command = MoonlightApp::Command::None;
    bool input_ready = false;
    bool ime_ready = false;
    if (document)
    {
        document->Show();
        if (!startup_splash)
        {
            if (Rml::Element *element = document->GetElementById("startup-splash"))
                element->SetClass("hidden", true);
        }
        std::uint64_t startup_presented_at = 0;
        if (startup_splash)
        {
            PresentLauncher(context, renderer, window);
            sceSystemServiceHideSplashScreen();
            startup_presented_at = SDL_GetPerformanceCounter();
            if (sound_ready)
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Open);
        }
        input_ready = radio_input_init();
        ime_ready = input_ready && radio_ime_init();
        running = input_ready && app.Initialize(document);
        if (running)
            app.ShowStreamError(stream_error);
        if (running)
        {
            if (startup_splash)
            {
                const std::uint64_t frequency = SDL_GetPerformanceFrequency();
                const std::uint64_t hold_until =
                    startup_presented_at + frequency * kStartupHoldMilliseconds / 1000u;
                while (SDL_GetPerformanceCounter() < hold_until)
                {
                    PresentLauncher(context, renderer, window);
                    wait_launcher_frame();
                }
                for (std::uint32_t frame = 1; frame <= kStartupFadeFrames; ++frame)
                {
                    char opacity[16];
                    std::snprintf(opacity, sizeof(opacity), "%.3f",
                                  1.0f - static_cast<float>(frame) / kStartupFadeFrames);
                    startup_splash->SetProperty("opacity", opacity);
                    PresentLauncher(context, renderer, window);
                    wait_launcher_frame();
                }
                startup_splash->SetClass("hidden", true);
            }
            PresentLauncher(context, renderer, window);
            if (!startup_splash)
            {
                sceSystemServiceHideSplashScreen();
                if (sound_ready && play_open_sound)
                    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Open);
            }
        }
    }
    else
    {
        PresentColor(renderer, window, 180, 20, 40);
        running = false;
    }

    while (running)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
        }
        radio_input_poll();
        if (ime_ready)
            radio_ime_poll();
        radio_input_event_t input{};
        while (radio_input_next(&input))
            app.HandleInput(input);
        command = app.TakeCommand();
        if (command != MoonlightApp::Command::None)
        {
            if (selection)
            {
                std::snprintf(selection->host, sizeof(selection->host), "%s",
                              app.SelectedHostAddress());
                std::snprintf(selection->app_name, sizeof(selection->app_name), "%s",
                              app.SelectedAppName());
                selection->app_id = app.SelectedAppId();
                selection->bitrate_kbps = app.BitrateKbps();
                selection->display_area = app.DisplayArea();
                selection->video_codec = app.VideoCodec();
                selection->stream_resolution = app.StreamResolution();
                selection->stream_fps = app.StreamFps();
                selection->hdr_enabled = app.HdrEnabled();
                selection->chroma_sampling = app.ChromaSampling();
                selection->audio_configuration = app.AudioConfiguration();
                selection->vsync_enabled = app.VsyncEnabled();
                selection->decoder_pipeline = app.DecoderPipeline();
                selection->decoder_cores = app.DecoderCores();
            }
            running = false;
        }
        if (running)
            app.Poll();
        PresentLauncher(context, renderer, window);
        wait_launcher_frame();
    }

    app.Shutdown();
    if (ime_ready)
        radio_ime_shutdown();
    if (input_ready)
        radio_input_shutdown();
    if (document)
        document->Close();
    if (context)
        Rml::RemoveContext("moonlight-ps5");
    Rml::Shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    // The PS5 SDL driver faults when a queued device is closed. Keep this
    // process-scoped device open but silent while native stream AudioOut runs.
    prosperolight::ui_sound_clear_for_stream();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return command;
}

} // namespace

int main()
{
    if (SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked) != 0)
    {
        KeepProcessAlive();
    }

#if PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS != 0
    RunVideoOutputSelfTest();
#endif

    png_decoder_available = sceSysmoduleLoadModule(kPngDecModule) >= 0;
    char stream_error[192]{};
    bool play_open_sound = true;
    for (;;)
    {
        LauncherSelection selection;
        const MoonlightApp::Command command =
            RunLauncher(&selection, stream_error, play_open_sound);
        play_open_sound = false;
        stream_error[0] = '\0';
        if (command != MoonlightApp::Command::StartStream)
            KeepProcessAlive();

        // Give launcher VideoOut teardown one final display interval before
        // AGC opens it.
        sceKernelUsleep(100000);
        moonlight_stream_options_t options{};
        moonlight_stream_metrics_t metrics{};
        options.host = selection.host;
        options.app_name = selection.app_name;
        options.app_id = selection.app_id;
        options.bitrate_kbps = selection.bitrate_kbps;
        options.display_area = selection.display_area;
        options.video_codec = selection.video_codec;
        options.stream_resolution = selection.stream_resolution;
        options.stream_fps = selection.stream_fps;
        options.hdr_enabled = selection.hdr_enabled;
        options.chroma_sampling = selection.chroma_sampling;
        options.audio_configuration = selection.audio_configuration;
        options.vsync_enabled = selection.vsync_enabled;
        options.decoder_pipeline = selection.decoder_pipeline;
        options.decoder_cores = selection.decoder_cores;
        (void)moonlight_stream_run(&options, &metrics);
        std::snprintf(stream_error, sizeof(stream_error), "%s", metrics.error);
        sceKernelUsleep(100000);
    }
}
