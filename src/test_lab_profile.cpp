/* ps5-native-app-boilerplate - strict bounded JSON profile parser.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "test_lab_profile.hpp"
#include <cctype>
#include <cstdio>
#include <map>
#include <vector>
#include <sys/stat.h>
#include <time.h>
namespace prosperolight::test_lab
{
namespace
{
struct Value
{
    enum Kind
    {
        object,
        text,
        number,
        boolean
    } kind = object;
    std::map<std::string, Value> fields;
    std::string string;
    unsigned integer = 0;
    bool flag = false;
};
class Parser
{
    std::string_view input;
    size_t at = 0, nodes = 0;
    void space()
    {
        while (at < input.size() &&
               (input[at] == ' ' || input[at] == '\n' || input[at] == '\r' || input[at] == '\t'))
            ++at;
    }
    bool take(char c)
    {
        space();
        if (at < input.size() && input[at] == c)
        {
            ++at;
            return true;
        }
        return false;
    }
    bool string(std::string &out)
    {
        if (!take('"'))
            return false;
        while (at < input.size())
        {
            unsigned char c = input[at++];
            if (c == '"')
                return true;
            // ASCII identifiers/metadata only in client MVP; reject escapes rather
            // than accepting ambiguous paths, duplicate keys or invalid UTF-8.
            if (c < 32 || c > 126 || c == '\\' || out.size() >= 256)
                return false;
            out += static_cast<char>(c);
        }
        return false;
    }
    bool value(Value &out, unsigned depth)
    {
        if (depth > 8 || ++nodes > 512)
            return false;
        space();
        if (at == input.size())
            return false;
        if (input[at] == '{')
        {
            ++at;
            if (take('}'))
                return true;
            do
            {
                std::string key;
                Value child;
                if (!string(key) || key.empty() || !take(':') || !value(child, depth + 1) ||
                    !out.fields.emplace(key, std::move(child)).second)
                    return false;
                if (take('}'))
                    return true;
            } while (take(','));
            return false;
        }
        if (input[at] == '"')
        {
            out.kind = Value::text;
            return string(out.string);
        }
        if (input.substr(at, 4) == "true")
        {
            at += 4;
            out.kind = Value::boolean;
            out.flag = true;
            return true;
        }
        if (input.substr(at, 5) == "false")
        {
            at += 5;
            out.kind = Value::boolean;
            return true;
        }
        if (input[at] < '0' || input[at] > '9')
            return false;
        out.kind = Value::number;
        if (input[at] == '0')
        {
            ++at;
            return true;
        }
        while (at < input.size() && input[at] >= '0' && input[at] <= '9')
        {
            unsigned digit = input[at++] - '0';
            if (out.integer > (1000000000u - digit) / 10u)
                return false;
            out.integer = out.integer * 10 + digit;
        }
        return true;
    }

  public:
    explicit Parser(std::string_view s) : input(s)
    {
    }
    bool parse(Value &out)
    {
        if (input.size() > 16384 || !value(out, 0))
            return false;
        space();
        return at == input.size() && out.kind == Value::object;
    }
};
bool known(const Value &v, std::initializer_list<std::string_view> names)
{
    if (v.kind != Value::object)
        return false;
    for (const auto &[key, ignored] : v.fields)
    {
        (void)ignored;
        bool found = false;
        for (auto name : names)
            found = found || key == name;
        if (!found)
            return false;
    }
    return true;
}
const Value *field(const Value &v, const char *name)
{
    auto i = v.fields.find(name);
    return i == v.fields.end() ? nullptr : &i->second;
}
bool text(const Value &v, const char *key, std::string &out, bool required = false)
{
    auto p = field(v, key);
    if (!p)
        return !required;
    if (p->kind != Value::text || p->string.empty())
        return false;
    out = p->string;
    return true;
}
bool integer(const Value &v, const char *key, std::optional<unsigned> &out, unsigned low,
             unsigned high)
{
    auto p = field(v, key);
    if (!p)
        return true;
    if (p->kind != Value::number || p->integer < low || p->integer > high)
        return false;
    out = p->integer;
    return true;
}
bool flag(const Value &v, const char *key, std::optional<bool> &out)
{
    auto p = field(v, key);
    if (!p)
        return true;
    if (p->kind != Value::boolean)
        return false;
    out = p->flag;
    return true;
}
bool choice(const Value &v, const char *key, std::optional<unsigned> &out,
            std::initializer_list<std::string_view> choices)
{
    auto p = field(v, key);
    if (!p)
        return true;
    if (p->kind != Value::text)
        return false;
    unsigned i = 0;
    for (auto name : choices)
    {
        if (p->string == name)
        {
            out = i;
            return true;
        }
        ++i;
    }
    return false;
}
} // namespace
bool safe_run_id(std::string_view id)
{
    if (id.empty() || id.size() > 127)
        return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    return true;
}
bool parse_profile(std::string_view json, std::string_view expected_id, TestProfile &out,
                   std::string &error)
{
    error = "INVALID_PROFILE";
    Value root;
    TestProfile p;
    std::string schema;
    if (!safe_run_id(expected_id) || !Parser(json).parse(root) ||
        !known(root, {"schema", "run_id", "suite", "source_sha", "build_label", "repeat", "client",
                      "timing", "stimulus", "capture", "environment", "coordinated"}) ||
        !text(root, "schema", schema, true) || schema != "prosperolight-test/1" ||
        !text(root, "run_id", p.run_id, true) || p.run_id != expected_id ||
        !safe_run_id(p.run_id) || !flag(root, "coordinated", p.coordinated) ||
        !text(root, "suite", p.suite, true) || !text(root, "source_sha", p.source_sha) ||
        !text(root, "build_label", p.build_label))
        return false;
    if (!p.source_sha.empty())
    {
        if (p.source_sha.size() != 40)
            return false;
        for (char c : p.source_sha)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
    }
    std::optional<unsigned> repeat;
    if (!integer(root, "repeat", repeat, 1, 100))
        return false;
    auto client = field(root, "client");
    if (!client ||
        !known(*client, {"host", "app", "codec", "chroma", "hdr", "resolution", "fps",
                         "bitrate_mbps", "vsync", "decoder_pipeline", "decoder_cores", "pacing",
                         "vrr_profile", "host_extensions"}) ||
        !text(*client, "host", p.host, true) || p.host.size() > 127 ||
        !text(*client, "app", p.app, true) || p.app.size() > 127 ||
        !choice(*client, "codec", p.codec, {"h264", "hevc", "pyrowave"}) ||
        !choice(*client, "chroma", p.chroma, {"420", "444"}) ||
        !choice(*client, "resolution", p.resolution, {"1080p", "1440p", "2160p"}) ||
        !choice(*client, "pacing", p.pacing, {"unpaced", "paced", "vrr"}) ||
        !choice(*client, "decoder_pipeline", p.pipeline, {"classic", "adaptive"}) ||
        !choice(*client, "vrr_profile", p.vrr_profile, {"low-latency", "balanced", "smooth"}) ||
        !integer(*client, "fps", p.fps, 30, 120) ||
        !integer(*client, "bitrate_mbps", p.bitrate_mbps, 1, 1000) ||
        !integer(*client, "decoder_cores", p.cores, 3, 5) || !flag(*client, "hdr", p.hdr) ||
        !flag(*client, "vsync", p.vsync))
        return false;
    if (auto ext = field(*client, "host_extensions"))
    {
        if (!known(*ext,
                   {"enabled", "vrr", "display", "scale", "optimize", "mute_host", "quit_host"}) ||
            !flag(*ext, "enabled", p.extensions) || !flag(*ext, "optimize", p.optimize) ||
            !flag(*ext, "mute_host", p.mute_host) || !flag(*ext, "quit_host", p.quit_host) ||
            !choice(*ext, "vrr", p.host_vrr, {"default", "off", "on"}) ||
            !choice(*ext, "display", p.display, {"default", "physical", "virtual"}) ||
            !integer(*ext, "scale", p.scale, 50, 100))
            return false;
    }
    if (auto timing = field(root, "timing"))
    {
        if (!known(*timing, {"startup_seconds", "ready_seconds", "hard_seconds", "warmup_seconds",
                             "measure_seconds", "cooldown_seconds"}))
            return false;
        for (const auto &[key, val] : timing->fields)
            if (val.kind != Value::number || val.integer > 1800 ||
                (key != "warmup_seconds" && key != "cooldown_seconds" && !val.integer))
                return false;
        if (auto v = field(*timing, "startup_seconds"))
            p.startup_seconds = v->integer;
        if (auto v = field(*timing, "ready_seconds"))
            p.ready_seconds = v->integer;
        if (auto v = field(*timing, "hard_seconds"))
            p.hard_seconds = v->integer;
        if (auto v = field(*timing, "warmup_seconds"))
            p.warmup_seconds = v->integer;
        if (auto v = field(*timing, "measure_seconds"))
            p.measure_seconds = v->integer;
        if (auto v = field(*timing, "cooldown_seconds"))
            p.cooldown_seconds = v->integer;
        unsigned total = p.warmup_seconds + p.measure_seconds + p.cooldown_seconds;
        if (p.hard_seconds < p.startup_seconds || p.hard_seconds < p.ready_seconds ||
            total > p.hard_seconds)
            return false;
    }
    // Metadata is validated now; worker/capture execution belongs to later milestones.
    if (auto stimulus = field(root, "stimulus"))
    {
        if (!known(*stimulus,
                   {"engine", "suite", "scene_id", "seed", "content_hash", "target_monitor"}))
            return false;
        for (const auto &[key, val] : stimulus->fields)
            if (key == "seed" ? val.kind != Value::number : val.kind != Value::text)
                return false;
        if (auto engine = field(*stimulus, "engine"))
            if (engine->string != "browser")
                return false;
    }
    if (auto capture = field(root, "capture"))
    {
        if (!known(*capture, {"detailed_frame_trace", "host_logs", "gpu_metrics"}))
            return false;
        for (const auto &[key, val] : capture->fields)
        {
            (void)key;
            if (val.kind != Value::boolean)
                return false;
        }
    }
    if (auto env = field(root, "environment"))
    {
        if (!known(*env, {"host_platform", "display_resolution", "display_refresh", "hdr",
                          "virtual_display"}))
            return false;
        for (const auto &[key, val] : env->fields)
        {
            if ((key == "hdr" || key == "virtual_display") ? val.kind != Value::boolean
                : key == "display_refresh"                 ? val.kind != Value::number
                                                           : val.kind != Value::text)
                return false;
        }
    }
    if ((p.codec == 0 && p.hdr == true) || (p.codec && *p.codec != 2 && p.chroma == 1))
    {
        error = "UNSUPPORTED_COMBINATION";
        return false;
    }
    out = std::move(p);
    error.clear();
    return true;
}
bool read_profile(std::string_view id, TestProfile &out, std::string &error)
{
    if (!safe_run_id(id))
    {
        error = "INVALID_RUN_ID";
        return false;
    }
    const auto path =
        std::string("/data/prosperolight/test-lab/inbox/") + std::string(id) + ".json";
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        error = "PROFILE_MISSING";
        return false;
    }
    char bytes[16385];
    size_t n = std::fread(bytes, 1, sizeof(bytes), f);
    bool failed = std::ferror(f);
    std::fclose(f);
    if (failed || n > 16384)
    {
        error = "PROFILE_READ_FAILED";
        return false;
    }
    return parse_profile({bytes, n}, id, out, error);
}
bool resolve_profile(const TestProfile &p, const moonlight_stream_options_t &saved,
                     const HostPreferences &host, unsigned pacing, unsigned vrr,
                     moonlight_stream_options_t &out, std::string &error)
{
    auto result = saved;
    if (p.codec)
        result.video_codec = *p.codec;
    if (p.chroma)
        result.chroma_sampling = *p.chroma;
    if (p.resolution)
        result.stream_resolution = *p.resolution;
    if (p.fps)
        result.stream_fps = *p.fps;
    if (p.bitrate_mbps)
        result.bitrate_kbps = *p.bitrate_mbps * 1000;
    if (p.pipeline)
        result.decoder_pipeline = *p.pipeline;
    if (p.cores)
        result.decoder_cores = *p.cores;
    if (p.hdr)
        result.hdr_enabled = *p.hdr;
    if (p.vsync)
        result.vsync_enabled = *p.vsync;
    if ((result.video_codec == MOONLIGHT_VIDEO_CODEC_H264 && result.hdr_enabled) ||
        (result.video_codec != MOONLIGHT_VIDEO_CODEC_PYROWAVE &&
         result.chroma_sampling != MOONLIGHT_CHROMA_420))
    {
        error = "UNSUPPORTED_COMBINATION";
        return false;
    }
    auto &o = result.session_overrides;
    o = {1,
         p.pacing.value_or(pacing),
         p.vrr_profile.value_or(vrr),
         p.extensions.value_or(host.extensions),
         p.host_vrr.value_or(host.vrr),
         p.display.value_or(host.display),
         p.scale.value_or(host.scale),
         p.optimize.value_or(host.optimize),
         p.mute_host.value_or(host.mute_host),
         p.quit_host.value_or(host.quit_host)};
    if (!o.extensions && (p.host_vrr || p.display || p.scale))
    {
        error = "HOST_EXTENSIONS_DISABLED";
        return false;
    }
    if (result.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE && (p.pipeline || p.cores))
    {
        error = "UNSUPPORTED_DECODER_CONTROLS";
        return false;
    }
    out = result;
    error.clear();
    return true;
}

bool write_resolved_options(FILE *f, const moonlight_stream_options_t &options)
{
    const auto &o = options;
    const auto &s = o.session_overrides;
    return std::fprintf(
               f,
               ",\"resolved\":{\"codec\":%u,\"chroma\":%u,\"hdr\":%u,\"resolution\":%u,"
               "\"fps\":%u,\"bitrate_kbps\":%u,\"vsync\":%u,\"decoder_pipeline\":%u,\"decoder_"
               "cores\":%u,"
               "\"pacing\":%u,\"vrr_profile\":%u,\"extensions\":%u,\"host_vrr\":%u,\"display\":%u,"
               "\"scale\":%u,\"optimize\":%u,\"mute_host\":%u,\"quit_host\":%u}",
               o.video_codec, o.chroma_sampling, o.hdr_enabled, o.stream_resolution, o.stream_fps,
               o.bitrate_kbps, o.vsync_enabled, o.decoder_pipeline, o.decoder_cores, s.pacing,
               s.vrr_profile, s.extensions, s.host_vrr, s.display, s.scale, s.optimize, s.mute_host,
               s.quit_host) > 0;
}

bool write_profile_status(std::string_view id, const char *state, unsigned sequence,
                          const moonlight_stream_options_t *options, std::string_view error,
                          std::string_view command_id, std::string_view command_result,
                          std::string_view command_action, bool stream_ready)
{
    if (!safe_run_id(id) || !state || error.size() > 127 ||
        (!command_id.empty() && !safe_run_id(command_id)))
        return false;
    if (!command_id.empty() && ((command_result != "accepted" && command_result != "rejected") ||
                                (command_action != "start_measure" && command_action != "stop" &&
                                 command_action != "cancel" && command_action != "clock_probe")))
        return false;
    for (char c : error)
        if (c < ' ' || c == '"' || c == '\\')
            return false;
    const std::string root = PROSPEROLIGHT_TEST_LAB_ROOT "/status";
    // FTP and the title run as different users. Status is non-secret evidence:
    // allow read/traverse, while only the title owner may publish receipts.
    (void)mkdir(root.c_str(), 0755);
    (void)chmod(root.c_str(), 0755);
    const auto path = root + "/" + std::string(id) + ".json", tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const unsigned long long us =
        static_cast<unsigned long long>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
    bool ok = std::fprintf(
                  f,
                  "{\"schema\":\"prosperolight-test-status/1\",\"run_id\":\"%s\",\"state\":\"%s\","
                  "\"sequence\":%u,\"monotonic_us\":%llu,\"reason\":\"%.*s\","
                  "\"stream_ready\":%s,\"negotiated\":null,\"capabilities\":[\"clock_probe\"]",
                  std::string(id).c_str(), state, sequence, us, static_cast<int>(error.size()),
                  error.empty() ? "" : error.data(), stream_ready ? "true" : "false") > 0;
    if (options)
        ok = ok && write_resolved_options(f, *options);
    if (!command_id.empty())
        ok = ok &&
             std::fprintf(
                 f,
                 ",\"command_id\":\"%.*s\",\"command_result\":\"%.*s\",\"command_action\":\"%.*s\"",
                 static_cast<int>(command_id.size()), command_id.data(),
                 static_cast<int>(command_result.size()), command_result.data(),
                 static_cast<int>(command_action.size()), command_action.data()) > 0;
    ok = ok && std::fputs("}\n", f) >= 0;
    const int closed = std::fclose(f);
    return ok && !closed && !std::rename(tmp.c_str(), path.c_str());
}

bool parse_control(std::string_view json, std::string_view id, ControlCommand &out)
{
    Value root;
    std::string schema, run;
    ControlCommand command;
    if (json.size() > 2048 || !Parser(json).parse(root) ||
        !known(root, {"schema", "run_id", "command_id", "action"}) ||
        !text(root, "schema", schema, true) || schema != "prosperolight-test-control/1" ||
        !text(root, "run_id", run, true) || run != id || !safe_run_id(run) ||
        !text(root, "command_id", command.command_id, true) || !safe_run_id(command.command_id) ||
        !text(root, "action", command.action, true) ||
        (command.action != "start_measure" && command.action != "stop" &&
         command.action != "cancel" && command.action != "clock_probe"))
        return false;
    out = std::move(command);
    return true;
}

TestProfile &active_profile()
{
    static TestProfile p;
    return p;
}
bool &active()
{
    static bool enabled = false;
    return enabled;
}
} // namespace prosperolight::test_lab
