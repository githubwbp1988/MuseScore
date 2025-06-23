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
#ifndef MUSE_AUDIO_SEQUENCER_H
#define MUSE_AUDIO_SEQUENCER_H

#include <map>

#include "modularity/ioc.h"
#include "global/async/asyncable.h"

#include "../../iplayer.h"
#include "../../itracks.h"
#include "../../iaudiooutput.h"
#include "igettracksequence.h"
#include "../../iplayback.h"

#include <utility>

#include "global/async/async.h"

#include "../audiothread.h"
#include "../audiosanitizer.h"

#include "player.h"
#include "trackshandler.h"
#include "audiooutputhandler.h"
#include "tracksequence.h"

#include "../../audioerrors.h"

#include "log.h"

using namespace muse;
using namespace muse::async;

namespace muse::audio {
class Playback : public IPlayback, public IGetTrackSequence, public Injectable, public async::Asyncable
{
public:
    Playback(const muse::modularity::ContextPtr& iocCtx)
        : Injectable(iocCtx) {}

    // void init() override;
    // void deinit() override;
    // bool isInited() const override;

    // // IPlayback
    // async::Promise<TrackSequenceId> addSequence() override;
    // async::Promise<TrackSequenceIdList> sequenceIdList() const override;
    // void removeSequence(const TrackSequenceId id) override;

    // async::Channel<TrackSequenceId> sequenceAdded() const override;
    // async::Channel<TrackSequenceId> sequenceRemoved() const override;

    // IPlayerPtr player(const TrackSequenceId id) const override;
    // ITracksPtr tracks() const override;
    // IAudioOutputPtr audioOutput() const override;

    void init() override
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_trackHandlersPtr = std::make_shared<TracksHandler>(this, iocContext());
        m_audioOutputPtr = std::make_shared<AudioOutputHandler>(this, iocContext());
    }

    void deinit() override
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_sequences.clear();

        m_trackHandlersPtr = nullptr;
        m_audioOutputPtr = nullptr;

        disconnectAll();
    }

    bool isInited() const override
    {
        return m_trackHandlersPtr != nullptr;
    }

    Promise<TrackSequenceId> addSequence() override
    {
        return Promise<TrackSequenceId>([this](auto resolve, auto /*reject*/) {
            ONLY_AUDIO_WORKER_THREAD;

            TrackSequenceId newId = static_cast<TrackSequenceId>(m_sequences.size());

            m_sequences.emplace(newId, std::make_shared<TrackSequence>(newId, iocContext()));
            m_sequenceAdded.send(newId);

            return resolve(std::move(newId));
        }, AudioThread::ID);
    }

    Promise<TrackSequenceIdList> sequenceIdList() const override
    {
        return Promise<TrackSequenceIdList>([this](auto resolve, auto /*reject*/) {
            ONLY_AUDIO_WORKER_THREAD;

            TrackSequenceIdList result;

            for (const auto& pair : m_sequences) {
                result.push_back(pair.first);
            }

            return resolve(std::move(result));
        }, AudioThread::ID);
    }

    void removeSequence(const TrackSequenceId id) override
    {
        Async::call(this, [this, id]() {
            ONLY_AUDIO_WORKER_THREAD;

            auto search = m_sequences.find(id);

            if (search != m_sequences.end()) {
                m_sequences.erase(search);
            }

            m_sequenceRemoved.send(id);
        }, AudioThread::ID);
    }

    Channel<TrackSequenceId> sequenceAdded() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_sequenceAdded;
    }

    Channel<TrackSequenceId> sequenceRemoved() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_sequenceRemoved;
    }

    IPlayerPtr player(const TrackSequenceId id) const override
    {
        std::shared_ptr<Player> p = std::make_shared<Player>(this, id);
        p->init();
        return p;
    }

    ITracksPtr tracks() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_trackHandlersPtr;
    }

    IAudioOutputPtr audioOutput() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_audioOutputPtr;
    }


protected:
    // IGetTrackSequence
    // ITrackSequencePtr sequence(const TrackSequenceId id) const override;
    ITrackSequencePtr sequence(const TrackSequenceId id) const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        auto search = m_sequences.find(id);

        if (search != m_sequences.end()) {
            return search->second;
        }

        return nullptr;
    }

private:
    ITracksPtr m_trackHandlersPtr = nullptr;
    IAudioOutputPtr m_audioOutputPtr = nullptr;

    std::map<TrackSequenceId, ITrackSequencePtr> m_sequences;

    async::Channel<TrackSequenceId> m_sequenceAdded;
    async::Channel<TrackSequenceId> m_sequenceRemoved;
};
}

#endif // MUSE_AUDIO_SEQUENCER_H
