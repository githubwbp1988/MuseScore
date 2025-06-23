/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MUSE_AUDIO_AUDIOTHREAD_H
#define MUSE_AUDIO_AUDIOTHREAD_H

#include <memory>
#include <thread>
#include <atomic>
#include <functional>

#include "../audiotypes.h"

#include "global/runtime.h"
#include "global/threadutils.h"
#include "global/async/processevents.h"

#include "audiosanitizer.h"

#include <emscripten/html5.h>

#include "log.h"

static uint64_t toWinTime(const msecs_t msecs)
{
    return msecs * 10000;
}

namespace muse::audio {
class AudioThread
{
public:
    AudioThread() = default;
    // ~AudioThread();

    static std::thread::id ID;

    using Runnable = std::function<void ()>;

    // void run(const Runnable& onStart, const Runnable& loopBody, const msecs_t interval = 1);
    // void setInterval(const msecs_t interval);
    // void stop(const Runnable& onFinished = nullptr);
    // bool isRunning() const;

    ~AudioThread()
    {
        if (m_running) {
            stop();
        }
    }

    void run(const Runnable& onStart, const Runnable& loopBody, const msecs_t interval = 1)
    {
        m_onStart = onStart;
        m_mainLoopBody = loopBody;
        m_intervalMsecs = interval;
        m_intervalInWinTime = toWinTime(interval);
        emscripten_set_timeout_loop([](double, void* userData) -> EM_BOOL {
            reinterpret_cast<AudioThread*>(userData)->loopBody();
            return EM_TRUE;
        }, 2, this);
    }

    void setInterval(const msecs_t interval)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_intervalMsecs = interval;
        m_intervalInWinTime = toWinTime(interval);
    }

    void stop(const Runnable& onFinished = nullptr)
    {
        m_onFinished = onFinished;
        m_running = false;
        if (m_thread) {
            m_thread->join();
        }
    }

    bool isRunning() const
    {
        return m_running;
    }

private:
    // void main();
    void main()
    {
        runtime::setThreadName("audio_worker");

        ID = std::this_thread::get_id();

        if (m_onStart) {
            m_onStart();
        }

        while (m_running) {
            muse::async::processEvents();

            if (m_mainLoopBody) {
                m_mainLoopBody();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMsecs));
        }

        if (m_onFinished) {
            m_onFinished();
        }
    }

    Runnable m_onStart = nullptr;
    Runnable m_mainLoopBody = nullptr;
    Runnable m_onFinished = nullptr;
    msecs_t m_intervalMsecs = 0;
    uint64_t m_intervalInWinTime = 0;

    std::unique_ptr<std::thread> m_thread = nullptr;
    std::atomic<bool> m_running = false;
};
using AudioThreadPtr = std::shared_ptr<AudioThread>;
}

#endif // MUSE_AUDIO_AUDIOTHREAD_H
