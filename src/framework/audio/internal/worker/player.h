/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore BVBA and others
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

#ifndef MUSE_AUDIO_PLAYER_H
#define MUSE_AUDIO_PLAYER_H

#include "../../iplayer.h"
#include "global/async/asyncable.h"

#include "igettracksequence.h"

#include "global/async/async.h"

#include "../audiosanitizer.h"
#include "../audiothread.h"
#include "../../audioerrors.h"

#include "log.h"

using namespace muse;
using namespace muse::async;

namespace muse::audio {
class Player : public IPlayer, public async::Asyncable
{
public:
    // Player(const IGetTrackSequence* getSeq, const TrackSequenceId sequenceId);

    // void init();

    // TrackSequenceId sequenceId() const override;

    // void play(const secs_t delay = 0) override;
    // void seek(const secs_t newPosition) override;
    // void stop() override;
    // void pause() override;
    // void resume(const secs_t delay = 0) override;

    // PlaybackStatus playbackStatus() const override;
    // async::Channel<PlaybackStatus> playbackStatusChanged() const override;

    // void setDuration(const msecs_t durationMsec) override;
    // async::Promise<bool> setLoop(const msecs_t fromMsec, const msecs_t toMsec) override;
    // void resetLoop() override;

    // secs_t playbackPosition() const override;
    // async::Channel<secs_t> playbackPositionChanged() const override;

    Player(const IGetTrackSequence* getSeq, const TrackSequenceId sequenceId)
    : m_getSequence(getSeq), m_sequenceId(sequenceId)
    {
    }

    void init()
    {
        ONLY_AUDIO_MAIN_THREAD;

        //! NOTE Subscribe and request initial state

        m_playbackStatusChanged.onReceive(this, [this](PlaybackStatus st) {
            m_playbackStatus = st;
        });

        m_playbackPositionChanged.onReceive(this, [this](const secs_t newPos) {
            m_playbackPosition = newPos;
        });

        Async::call(this, [this]() {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = seq();

            //! NOTE Send initial state
            m_playbackStatusChanged.send(s->player()->playbackStatus());
            s->player()->playbackStatusChanged().onReceive(this, [this](const PlaybackStatus newStatus) {
                m_playbackStatusChanged.send(newStatus);
            });

            //! NOTE Send initial state
            m_playbackPositionChanged.send(s->player()->playbackPosition());
            s->player()->playbackPositionChanged().onReceive(this, [this](const secs_t newPos) {
                // LOGALEX() << "m_playbackPositionChanged, newPos: " << newPos;
                m_playbackPositionChanged.send(newPos);
            });
        }, AudioThread::ID);
    }

    TrackSequenceId sequenceId() const override
    {
        ONLY_AUDIO_MAIN_THREAD;
        return m_sequenceId;
    }

    void play(const secs_t delay = 0) override
    {
        LOGALEX();
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this, delay]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->play(delay);
            }
        }, AudioThread::ID);
    }

    void seek(const secs_t newPosition) override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this, newPosition]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->seek(newPosition);
            }
        }, AudioThread::ID);
    }

    void stop() override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->stop();
            }
        }, AudioThread::ID);
    }

    void pause() override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->pause();
            }
        }, AudioThread::ID);
    }

    void resume(const secs_t delay = 0) override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this, delay]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->resume(delay);
            }
        }, AudioThread::ID);
    }

    void setDuration(const msecs_t durationMsec) override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this, durationMsec]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->setDuration(durationMsec);
            }
        }, AudioThread::ID);
    }

    async::Promise<bool> setLoop(const msecs_t fromMsec, const msecs_t toMsec) override
    {
        ONLY_AUDIO_MAIN_THREAD;

        return Promise<bool>([this, fromMsec, toMsec](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = seq();

            if (!s) {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }

            Ret result = s->player()->setLoop(fromMsec, toMsec);
            if (!result) {
                return reject(result.code(), result.text());
            }

            return resolve(result);
        }, AudioThread::ID);
    }

    void resetLoop() override
    {
        ONLY_AUDIO_MAIN_THREAD;

        Async::call(this, [this]() {
            ONLY_AUDIO_WORKER_THREAD;
            ITrackSequencePtr s = seq();
            if (s) {
                s->player()->resetLoop();
            }
        }, AudioThread::ID);
    }

    secs_t playbackPosition() const override
    {
        ONLY_AUDIO_MAIN_THREAD;
        return m_playbackPosition;
    }

    async::Channel<secs_t> playbackPositionChanged() const override
    {
        ONLY_AUDIO_MAIN_THREAD;
        return m_playbackPositionChanged;
    }

    PlaybackStatus playbackStatus() const override
    {
        ONLY_AUDIO_MAIN_THREAD;
        return m_playbackStatus;
    }

    async::Channel<PlaybackStatus> playbackStatusChanged() const override
    {
        ONLY_AUDIO_MAIN_THREAD;
        return m_playbackStatusChanged;
    }


private:

    // ITrackSequencePtr seq() const;
    ITrackSequencePtr seq() const
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(m_getSequence) {
            return nullptr;
        }

        return m_getSequence->sequence(m_sequenceId);
    }

    const IGetTrackSequence* m_getSequence = nullptr;
    TrackSequenceId m_sequenceId = -1;
    PlaybackStatus m_playbackStatus = PlaybackStatus::Stopped;
    async::Channel<PlaybackStatus> m_playbackStatusChanged;

    secs_t m_playbackPosition = 0.0;
    async::Channel<secs_t> m_playbackPositionChanged;
};
}

#endif // MUSE_AUDIO_PLAYER_H
