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

#include "audiotypes.h"
#include <emscripten/threading.h>

namespace muse::audio {
class AudioThread
{
public:
    AudioThread() = default;
    ~AudioThread();

    // static std::thread::id ID;

    using ID_Type = int;

    static inline ID_Type ID;

    static ID_Type get_current_id() {
        if (current_thread_id == 0) {
            current_thread_id = next_id.fetch_add(1) + 1;
        }
        return current_thread_id;
    }

    using Runnable = std::function<void ()>;

    void run(const Runnable& onStart, const Runnable& loopBody, const msecs_t interval = 1);
    void setInterval(const msecs_t interval);
    void stop(const Runnable& onFinished = nullptr);
    bool isRunning() const;

private:
    void main();

    static std::atomic<int> next_id;
    static thread_local int current_thread_id;

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
