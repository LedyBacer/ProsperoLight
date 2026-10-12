/*
 * ps5-native-app-boilerplate - ProsperoLight: a forwarded launch, step by step.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Turns "start this app on that PC" into the launcher's own actions: select
// the PC, wait for it to answer, select the app. The caller then starts the
// stream exactly as a press on Start does. It gives up with a sentence for the
// player when the PC is unknown, silent, unpaired or without that app.
#pragma once

#include "forwarded_launch.hpp"
#include "i18n.hpp"
#include "launcher/launcher_model.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace launcher
{

class ForwardedStart
{
  public:
    enum class Step
    {
        idle,    // nothing was asked for, or it is over
        waiting, // the PC has not answered yet
        start,   // the PC and the app are selected: start the stream now
        failed,  // *message says why; the launcher stays open
    };

    void Begin(prosperolight::ForwardedLaunch launch, std::uint64_t now_ms,
               std::uint64_t patience_ms = kPatienceMs)
    {
        launch_ = std::move(launch);
        active_ = launch_.requested();
        selected_ = false;
        deadline_ms_ = now_ms + patience_ms;
    }

    // Still on its way to a stream: the launcher keeps its screens out of sight.
    bool active() const
    {
        return active_;
    }
    // What was asked for, as it was typed: the names to show until the PC answers.
    const prosperolight::ForwardedLaunch &launch() const
    {
        return launch_;
    }

    bool exit_after_stream() const
    {
        return launch_.exit_after_stream;
    }

    Step Advance(Model &model, std::uint64_t now_ms, std::string *message)
    {
        if (!active_)
            return Step::idle;
        if (!selected_)
        {
            const int host = prosperolight::forwarded_host(model.config(), launch_.pc);
            if (host < 0)
                return Fail(message,
                            launch_.pc.empty()
                                ? std::string(i18n::tr("No PC is saved yet."))
                                : "\"" + launch_.pc + "\" " + i18n::tr("is not a saved PC."));
            if (static_cast<std::uint32_t>(host) != model.config().selected_host)
                model.SelectHost(static_cast<unsigned>(host));
            selected_ = true;
            return Step::waiting;
        }
        const moonlight_backend_snapshot_t &backend = model.backend();
        const std::string pc = PcName(model);
        if (model.session_only() && model.backend_valid() &&
            std::strstr(backend.error, "NOT_PAIRED") != nullptr)
            return Fail(message, "NOT_PAIRED");
        if (model.backend_valid() && backend.online)
        {
            if (!backend.paired)
                return Fail(message, pc + " " + i18n::tr("is not paired with this PS5."));
            const int app = prosperolight::forwarded_app(backend, launch_.app);
            if (app < 0)
                return Fail(message, "\"" + launch_.app + "\" " + i18n::tr("was not found on") +
                                         " " + pc + ".");
            model.SelectApp(static_cast<unsigned>(app));
            active_ = false;
            return Step::start;
        }
        // An offline answer is not final: the PC may be waking or reconnecting.
        if (now_ms >= deadline_ms_)
            return Fail(message, pc + " " + i18n::tr("did not answer."));
        return Step::waiting;
    }

  private:
    static constexpr std::uint64_t kPatienceMs = 15000;

    static std::string PcName(const Model &model)
    {
        const moonlight_config_host_t *host = model.selected_host();
        if (!host)
            return i18n::tr("The PC");
        return host->name[0] ? host->name : host->address;
    }

    Step Fail(std::string *message, std::string text)
    {
        active_ = false;
        if (message)
            *message = std::move(text);
        return Step::failed;
    }

    prosperolight::ForwardedLaunch launch_;
    bool active_ = false;
    bool selected_ = false;
    std::uint64_t deadline_ms_ = 0;
};

} // namespace launcher
