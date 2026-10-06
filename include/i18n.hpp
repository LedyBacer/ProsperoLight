// ps5-native-app-boilerplate - Launcher localization and layout.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app_storage.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#ifdef __PROSPERO__
extern "C" int sceSystemServiceParamGetInt(int, int *);
#endif
namespace i18n
{
struct Language
{
    const char *code;
    const char *name;
};
inline constexpr Language languages[] = {
    {"en", "English"},     {"bg", "Български"},
    {"ckb", "کوردی"},      {"cs", "Čeština"},
    {"de", "Deutsch"},     {"el", "Ελληνικά"},
    {"eo", "Esperanto"},   {"es", "Español"},
    {"et", "Eesti"},       {"fr", "Français"},
    {"he", "עברית"},       {"hi", "हिन्दी"},
    {"hu", "Magyar"},      {"it", "Italiano"},
    {"ja", "日本語"},      {"ko", "한국어"},
    {"lt", "Lietuvių"},    {"nb_NO", "Norsk bokmål"},
    {"nl", "Nederlands"},  {"pl", "Polski"},
    {"pt", "Português"},   {"pt_BR", "Português (Brasil)"},
    {"ru", "Русский"},     {"sv", "Svenska"},
    {"ta", "தமிழ்"},        {"th", "ไทย"},
    {"tr", "Türkçe"},      {"uk", "Українська"},
    {"vi", "Tiếng Việt"},  {"zh_CN", "简体中文"},
    {"zh_TW", "繁體中文"},
};
inline std::mutex mutex;
inline std::atomic<unsigned> active{0};
inline std::array<std::map<std::string, std::string>, std::size(languages)> catalogs;
inline std::array<bool, std::size(languages)> loaded{};
inline std::string folder, preference = "auto";
inline unsigned system_index{};
inline bool initialized{};
// Catalogs are flat UTF-8 JSON objects, generated with literal Unicode.
// Escapes are decoded and duplicate keys/malformed input are rejected.
inline std::string formats(std::string_view value)
{
    std::string result;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] != '%')
            continue;
        const size_t start = i;
        if (++i == value.size())
            return "invalid";
        if (value[i] == '%')
        {
            result += "%%;";
            continue;
        }
        while (i < value.size() &&
               (value[i] == '-' || value[i] == '+' || value[i] == ' ' || value[i] == '#' ||
                value[i] == '.' || (value[i] >= '0' && value[i] <= '9')))
            ++i;
        if (i == value.size() ||
            (value[i] != 'u' && value[i] != 's' && value[i] != 'f' && value[i] != 'd'))
            return "invalid";
        result.append(value.substr(start, i - start + 1));
        result += ';';
    }
    return result;
}
inline bool parse(std::string_view data, std::map<std::string, std::string> &out)
{
    size_t p = 0;
    const auto space = [&]
    {
        while (p < data.size() &&
               (data[p] == ' ' || data[p] == '\n' || data[p] == '\r' || data[p] == '\t'))
            ++p;
    };
    const auto string = [&](std::string &value)
    {
        space();
        if (p >= data.size() || data[p++] != '"')
            return false;
        value.clear();
        while (p < data.size())
        {
            char c = data[p++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 32)
                return false;
            if (c == '\\')
            {
                if (p == data.size())
                    return false;
                c = data[p++];
                switch (c)
                {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case '"':
                case '\\':
                case '/':
                    break;
                case 'u':
                {
                    const auto hex = [&]() -> int
                    {
                        if (p + 4 > data.size())
                            return -1;
                        int cp = 0;
                        for (int j = 0; j < 4; ++j)
                        {
                            const char h = data[p++];
                            int digit;
                            if (h >= '0' && h <= '9')
                                digit = h - '0';
                            else if (h >= 'a' && h <= 'f')
                                digit = h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F')
                                digit = h - 'A' + 10;
                            else
                                return -1;
                            cp = cp * 16 + digit;
                        }
                        return cp;
                    };
                    int cp = hex();
                    if (cp < 0 || cp == 0)
                        return false;
                    if (cp >= 0xd800 && cp <= 0xdbff)
                    {
                        if (p + 2 > data.size() || data[p++] != '\\' || data[p++] != 'u')
                            return false;
                        const int low = hex();
                        if (low < 0xdc00 || low > 0xdfff)
                            return false;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    }
                    else if (cp >= 0xdc00 && cp <= 0xdfff)
                        return false;
                    if (cp < 0x80)
                        value += static_cast<char>(cp);
                    else if (cp < 0x800)
                    {
                        value += static_cast<char>(0xc0 | (cp >> 6));
                        value += static_cast<char>(0x80 | (cp & 63));
                    }
                    else if (cp < 0x10000)
                    {
                        value += static_cast<char>(0xe0 | (cp >> 12));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 63));
                        value += static_cast<char>(0x80 | (cp & 63));
                    }
                    else
                    {
                        value += static_cast<char>(0xf0 | (cp >> 18));
                        value += static_cast<char>(0x80 | ((cp >> 12) & 63));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 63));
                        value += static_cast<char>(0x80 | (cp & 63));
                    }
                    continue;
                }
                default:
                    return false;
                }
            }
            value += c;
        }
        return false;
    };
    std::map<std::string, std::string> result;
    space();
    if (p == data.size() || data[p++] != '{')
        return false;
    space();
    if (p < data.size() && data[p] != '}')
    {
        for (;;)
        {
            std::string key, value;
            if (!string(key))
                return false;
            space();
            if (p == data.size() || data[p++] != ':')
                return false;
            if (!string(value) || !result.emplace(key, value).second)
                return false;
            space();
            if (p == data.size())
                return false;
            if (data[p] == '}')
                break;
            if (data[p++] != ',')
                return false;
        }
    }
    if (p == data.size() || data[p++] != '}')
        return false;
    space();
    if (p != data.size())
        return false;
    out.swap(result);
    return true;
}
inline bool read(const std::string &path, std::map<std::string, std::string> &result)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    std::string data;
    char block[4096];
    size_t n;
    while ((n = std::fread(block, 1, sizeof(block), file)))
    {
        data.append(block, n);
        if (data.size() > 1024u * 1024u)
            break;
    }
    const bool ok = data.size() <= 1024u * 1024u && !std::ferror(file);
    std::fclose(file);
    return ok && parse(data, result);
}
inline unsigned index(std::string_view code)
{
    for (unsigned i = 0; i < std::size(languages); ++i)
        if (code == languages[i].code)
            return i;
    return 0;
}
inline const char *console_language(int value)
{
    // SceSystemService language enum, shared by PS4/PS5. Unsupported
    // console languages fall back to English without pretending to support them.
    switch (value)
    {
    case 0:
        return "ja";
    case 2:
        return "fr";
    case 3:
        return "es";
    case 4:
        return "de";
    case 5:
        return "it";
    case 6:
        return "nl";
    case 7:
        return "pt";
    case 8:
        return "ru";
    case 9:
        return "ko";
    case 10:
        return "zh_TW";
    case 11:
        return "zh_CN";
    case 13:
        return "sv";
    case 15:
        return "nb_NO";
    case 16:
        return "pl";
    case 17:
        return "pt_BR";
    case 19:
        return "tr";
    case 25:
        return "el";
    case 23:
        return "cs";
    case 24:
        return "hu";
    case 27:
        return "th";
    case 28:
        return "vi";
    case 20:
        return "es";
    case 22:
        return "fr";
    case 30:
        return "uk";
    default:
        return "en";
    }
}
inline void activate(unsigned i)
{
    if (!loaded[i])
    {
        if (!read(folder + "/rendered/" + languages[i].code + ".json", catalogs[i]))
            (void)read(folder + "/" + languages[i].code + ".json", catalogs[i]);
        loaded[i] = true;
    }
    active.store(i);
}
inline void initialize(const std::string &path)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (initialized)
        return;
    folder = path;
#ifndef __PROSPERO__
    if (const char *dir = std::getenv("PROSPEROLIGHT_LOCALE_DIR"))
        folder = dir;
#endif
    int value = -1;
#ifdef __PROSPERO__
    if (sceSystemServiceParamGetInt(1, &value) != 0)
        value = -1;
#endif
    system_index = index(console_language(value));
    std::map<std::string, std::string> stored;
    if (read(storage::setting_file("prosperolight-language.json"), stored))
    {
        const auto found = stored.find("language");
        if (found != stored.end() &&
            (found->second == "auto" || index(found->second) > 0 || found->second == "en"))
            preference = found->second;
    }
    activate(preference == "auto" ? system_index : index(preference));
    initialized = true;
#ifdef __PROSPERO__
    std::printf("[PL] i18n: console_language=%d locale=%s selection=%s\n", value,
                languages[active.load()].code, preference.c_str());
#endif
}
inline const char *tr(const char *source)
{
    std::lock_guard<std::mutex> guard(mutex);
    const auto &catalog = catalogs[active.load()];
    const auto found = catalog.find(source);
    // Dictionaries are retained for the process lifetime: pointers remain
    // valid while model/network threads finish a translated notification.
    return found != catalog.end() && !found->second.empty() &&
                   formats(found->second) == formats(source)
               ? found->second.c_str()
               : source;
}
inline int selected()
{
    std::lock_guard<std::mutex> guard(mutex);
    return preference == "auto" ? 0 : static_cast<int>(index(preference)) + 1;
}
inline bool select(int selection)
{
    if (selection < 0 || selection > static_cast<int>(std::size(languages)))
        return false;
    std::lock_guard<std::mutex> guard(mutex);
    const std::string code = selection == 0 ? "auto" : languages[selection - 1].code;
    const std::string destination = storage::setting_file("prosperolight-language.json"),
                      temporary = destination + ".tmp";
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        return false;
    const std::string data = "{\"language\":\"" + code + "\"}\n";
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const int close = std::fclose(file);
    if (!written || close || std::rename(temporary.c_str(), destination.c_str()))
    {
        std::remove(temporary.c_str());
        return false;
    }
    preference = code;
    activate(code == "auto" ? system_index : index(code));
    return true;
}
} // namespace i18n
