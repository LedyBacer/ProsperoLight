/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app_storage.hpp"

#include "elevation/elevation.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int sceKernelDebugOutText(int channel, const char *text);

namespace
{

// Must match sce_sys/param.json.
constexpr char kTitleId[] = "PPSA99002";
constexpr char kDataDir[] = "/data/prosperolight";
constexpr char kInstallDir[] = "/data/homebrew/PPSA99002";
// The title's sandbox, seen from the console's own root.
constexpr char kSandboxApp[] = "/mnt/sandbox/PPSA99002_000/app0";
constexpr char kSandboxData[] = "/mnt/sandbox/PPSA99002_000/download0";
// Compiled shaders belong to one version of the OpenGL runtime (tools/fetch-opengl-sdk.sh).
constexpr char kShaderCache[] = "opengl-1.0.0";
constexpr char kLogName[] = "prosperolight-launcher.log";
constexpr char kPreviousLogName[] = "prosperolight-launcher.prev.log";

// A file an earlier version kept in the sandbox. It is read before the process
// leaves the sandbox, while /download0 is certain to resolve, and written to
// its new place afterwards.
struct Kept
{
    const char *from;
    const char *folder;
    const char *name;
    char *data;
    std::size_t capacity;
    std::size_t size;
};

char g_kept_config[256 * 1024];
char g_kept_certificate[16 * 1024];
char g_kept_key[16 * 1024];
Kept g_kept[] = {
    {"/download0/prosperolight-config.bin", "config", "prosperolight-config.bin", g_kept_config,
     sizeof(g_kept_config), 0},
    {"/download0/moonlight/cert.pem", "pairing", "cert.pem", g_kept_certificate,
     sizeof(g_kept_certificate), 0},
    {"/download0/moonlight/key.pem", "pairing", "key.pem", g_kept_key, sizeof(g_kept_key), 0},
};

bool is_file(const char *path)
{
    struct stat info
    {
    };
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

bool is_directory(const char *path)
{
    struct stat info
    {
    };
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

void remember(Kept &kept)
{
    std::FILE *input = std::fopen(kept.from, "rb");
    if (input == nullptr)
        return;
    const std::size_t count = std::fread(kept.data, 1, kept.capacity, input);
    // A file that does not fit, or could not be read to its end, is left behind.
    if (std::ferror(input) == 0 && std::fgetc(input) == EOF)
        kept.size = count;
    std::fclose(input);
}

bool write_new(const char *path, const Kept &kept)
{
    char partial[176];
    std::snprintf(partial, sizeof(partial), "%s.part", path);
    std::FILE *output = std::fopen(partial, "wb");
    if (output == nullptr)
        return false;
    bool ok = std::fwrite(kept.data, 1, kept.size, output) == kept.size;
    ok = std::fclose(output) == 0 && ok;
    // A copy that stopped half way must not be taken for the file.
    if (ok)
        ok = std::rename(partial, path) == 0;
    else
        std::remove(partial);
    return ok;
}

void kept_path(char *path, std::size_t size, const Kept &kept)
{
    std::snprintf(path, size, "%s/%s/%s", kDataDir, kept.folder, kept.name);
}

int g_log_file = -1;

// Writes what the streams hold into the log file, a few times a second. A
// write to a file under /data takes tens of milliseconds; the OpenGL runtime
// writes some forty lines of statistics every ten thousand draws, and with
// unbuffered streams the screen stood still for a second each time. Now the
// lines wait in memory and this thread pays for the writes.
void *log_flusher(void *)
{
    for (;;)
    {
        usleep(200000);
        std::fflush(stdout);
        std::fflush(stderr);
    }
    return nullptr;
}

void open_log(const char *folder)
{
    char path[176];
    char previous[176];
    std::snprintf(path, sizeof(path), "%s/%s", folder, kLogName);
    std::snprintf(previous, sizeof(previous), "%s/%s", folder, kPreviousLogName);
    // Keep the previous launch's log: it is the one that explains a crash.
    std::rename(path, previous);
    std::FILE *stream = std::freopen(path, "w", stdout);
    // Start a fresh file, then make both streams append to it.
    if (stream != nullptr)
        stream = std::freopen(path, "a", stdout);
    const bool out = stream != nullptr;
    stream = std::freopen(path, "a", stderr);
    const bool err = stream != nullptr;
    // The crash report writes into the file itself: a fault may come before
    // the next flush.
    g_log_file = open(path, O_WRONLY | O_APPEND);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 64u * 1024u);
    pthread_t flusher;
    const bool started = pthread_create(&flusher, &attributes, log_flusher, nullptr) == 0;
    pthread_attr_destroy(&attributes);
    if (started)
        pthread_detach(flusher);
    // Without the thread every line is written at once, as before.
    if (out)
        std::setvbuf(stdout, nullptr, started ? _IOFBF : _IONBF, started ? 64u * 1024u : 0u);
    if (err)
        std::setvbuf(stderr, nullptr, started ? _IOFBF : _IONBF, started ? 64u * 1024u : 0u);
}

void say(const char *line)
{
    std::fputs(line, stdout);
    (void)sceKernelDebugOutText(0, line);
}

// The folders under /data/prosperolight; false when one could not be made.
bool make_data_folders()
{
    mkdir(kDataDir, 0777);
    bool ok = is_directory(kDataDir);
    for (const char *folder : {"config", "pairing", "logs"})
    {
        char path[160];
        std::snprintf(path, sizeof(path), "%s/%s", kDataDir, folder);
        mkdir(path, 0777);
        ok = is_directory(path) && ok;
    }
    return ok;
}

} // namespace

int storage::log_descriptor()
{
    return g_log_file;
}

void storage::Initialize()
{
    Paths &paths = g_paths;
    for (Kept &kept : g_kept)
        remember(kept);

    paths.status = static_cast<int>(elevation::request(elevation::Capability::filesystem));
    const bool granted = paths.status == 0;
    // Elevation leaves the effective group apart from the real one, and the
    // OpenGL runtime turns its shader cache off for such a process.
    const bool group_matched = getegid() == getgid() || setegid(getgid()) == 0;

    bool data_ready = false;
    if (granted)
    {
        // The console's root has no /app0: the sandbox mounts it from the
        // folder the app was installed in (or from the image it came as).
        char probe[160];
        std::snprintf(probe, sizeof(probe), "%s/eboot.bin", kInstallDir);
        std::snprintf(paths.app, sizeof(paths.app), "%s",
                      is_file(probe) ? kInstallDir : kSandboxApp);
        data_ready = make_data_folders();
        // Without its folders the app keeps the sandbox's storage, by the
        // name it has outside the sandbox.
        const char *base = data_ready ? kDataDir : kSandboxData;
        std::snprintf(paths.config, sizeof(paths.config), "%s/%sprosperolight-config.bin", base,
                      data_ready ? "config/" : "");
        std::snprintf(paths.config_temporary, sizeof(paths.config_temporary),
                      "%s/%sprosperolight-config.tmp", base, data_ready ? "config/" : "");
        std::snprintf(paths.pairing, sizeof(paths.pairing), "%s/%s", base,
                      data_ready ? "pairing" : "moonlight");
        std::snprintf(paths.logs, sizeof(paths.logs), "%s%s", base, data_ready ? "/logs" : "");
        std::snprintf(paths.performance, sizeof(paths.performance), "%s/%s", base,
                      data_ready ? "logs" : "moonlight");
    }
    open_log(paths.logs);

    // The OpenGL runtime keeps the shaders it compiled, so later launches, and
    // the first visit to each screen, skip that work.
    char shaders[176];
    const char *base = !granted ? "/download0" : data_ready ? kDataDir : kSandboxData;
    std::snprintf(shaders, sizeof(shaders), "%s/cache", base);
    mkdir(shaders, 0777);
    std::snprintf(shaders, sizeof(shaders), "%s/cache/%s", base, kShaderCache);
    mkdir(shaders, 0777);
    const bool cache_ready =
        is_directory(shaders) && setenv("PS5_SHADER_CACHE_DIR", shaders, 1) == 0;
    char line[400];
    std::snprintf(line, sizeof(line),
                  "[PL] storage: title=%s status=%d app=%s data=%s uid=%d/%d gid=%d/%d "
                  "group_matched=%d shader_cache=%d\n",
                  kTitleId, paths.status, paths.app,
                  granted ? (data_ready ? kDataDir : kSandboxData) : "/download0",
                  static_cast<int>(getuid()), static_cast<int>(geteuid()),
                  static_cast<int>(getgid()), static_cast<int>(getegid()), group_matched ? 1 : 0,
                  cache_ready ? 1 : 0);
    say(line);
    if (!data_ready)
        return;

    // First start with filesystem access: the saved PCs, the settings and the
    // pairing come along. The sandbox's copies stay where they are.
    char path[176];
    Kept &config = g_kept[0];
    kept_path(path, sizeof(path), config);
    if (config.size != 0 && !is_file(path) && write_new(path, config))
        say("[PL] storage: brought over the saved PCs and settings\n");
    // A certificate and its key belong together: both, or neither.
    Kept &certificate = g_kept[1];
    Kept &key = g_kept[2];
    char key_path[176];
    kept_path(path, sizeof(path), certificate);
    kept_path(key_path, sizeof(key_path), key);
    if (certificate.size != 0 && key.size != 0 && !is_file(path) && !is_file(key_path))
    {
        if (write_new(key_path, key) && write_new(path, certificate))
        {
            say("[PL] storage: brought over the pairing\n");
        }
        else
        {
            std::remove(path);
            std::remove(key_path);
            say("[PL] storage: the pairing could not be brought over\n");
        }
    }
    // The key does not stay in memory longer than it is needed.
    std::memset(g_kept_key, 0, sizeof(g_kept_key));
}
