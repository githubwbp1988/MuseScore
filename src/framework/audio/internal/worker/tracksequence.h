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

#ifndef MUSE_AUDIO_TRACKSEQUENCE_H
#define MUSE_AUDIO_TRACKSEQUENCE_H

#include "global/async/asyncable.h"
#include "modularity/ioc.h"
#include "iaudioengine.h"

#include "itracksequence.h"
#include "igettracks.h"
#include "iclock.h"
#include "track.h"
#include "../../audiotypes.h"

#include "../audiosanitizer.h"
#include "clock.h"
#include "eventaudiosource.h"
#include "sequenceplayer.h"
#include "sequenceio.h"
#include "../../audioerrors.h"

#include "log.h"

using namespace muse;
using namespace muse::async;

namespace muse::audio {
class Mixer;
class TrackSequence : public ITrackSequence, public IGetTracks, public muse::Injectable, public async::Asyncable
{
    Inject<IAudioEngine> audioEngine = { this };

public:
    // TrackSequence(const TrackSequenceId id, const muse::modularity::ContextPtr& iocCtx);
    // ~TrackSequence();

    // ITrackSequence
    // TrackSequenceId id() const override;

    // RetVal2<TrackId, AudioParams> addTrack(const std::string& trackName, const mpe::PlaybackData& playbackData,
    //                                        const AudioParams& requiredParams) override;
    // RetVal2<TrackId, AudioParams> addTrack(const std::string& trackName, io::IODevice* device, const AudioParams& requiredParams) override;

    // RetVal2<TrackId, AudioOutputParams> addAuxTrack(const std::string& trackName, const AudioOutputParams& requiredOutputParams) override;

    // TrackName trackName(const TrackId id) const override;
    // TrackIdList trackIdList() const override;

    // Ret removeTrack(const TrackId id) override;
    // void removeAllTracks() override;

    // async::Channel<TrackId> trackAdded() const override;
    // async::Channel<TrackId> trackRemoved() const override;

    // ISequencePlayerPtr player() const override;
    // ISequenceIOPtr audioIO() const override;

    // IGetTracks
    // TrackPtr track(const TrackId id) const override;
    // const TracksMap& allTracks() const override;

    // async::Channel<TrackPtr> trackAboutToBeAdded() const override;
    // async::Channel<TrackPtr> trackAboutToBeRemoved() const override;

    TrackSequence(const TrackSequenceId id, const muse::modularity::ContextPtr& iocCtx)
    : muse::Injectable(iocCtx), m_id(id)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_clock = std::make_shared<Clock>();
        m_player = std::make_shared<SequencePlayer>(this, m_clock, iocCtx);
        m_audioIO = std::make_shared<SequenceIO>(this);

        audioEngine()->modeChanged().onNotify(this, [this]() {
            m_prevActiveTrackId = INVALID_TRACK_ID;
        });

        mixer()->addClock(m_clock);
    }

    ~TrackSequence()
    {
        ONLY_AUDIO_WORKER_THREAD;

        mixer()->removeClock(m_clock);

        removeAllTracks();
    }

    TrackSequenceId id() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_id;
    }

    RetVal2<TrackId, AudioParams> addTrack(const std::string& trackName, const mpe::PlaybackData& playbackData,
                                           const AudioParams& requiredParams) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        RetVal2<TrackId, AudioParams> result;
        result.val1 = -1;

        IF_ASSERT_FAILED(mixer()) {
            result.ret = make_ret(Err::Undefined);
            return result;
        }

        if (!playbackData.setupData.isValid()) {
            result.ret = make_ret(Err::InvalidSetupData);
            return result;
        }

        TrackId newId = newTrackId();

        auto onOffStreamReceived = [this](const TrackId trackId) {
            if (m_prevActiveTrackId == INVALID_TRACK_ID) {
                mixer()->setTracksToProcessWhenIdle({ trackId });
            } else {
                mixer()->setTracksToProcessWhenIdle({ m_prevActiveTrackId, trackId });
            }

            m_prevActiveTrackId = trackId;
        };

        EventTrackPtr trackPtr = std::make_shared<EventTrack>();
        EventAudioSourcePtr source = std::make_shared<EventAudioSource>(newId, playbackData, onOffStreamReceived, iocContext());

        trackPtr->id = newId;
        trackPtr->name = trackName;
        trackPtr->setPlaybackData(playbackData);
        trackPtr->inputHandler = source;
        trackPtr->outputHandler = mixer()->addChannel(newId, source).val;
        trackPtr->setInputParams(requiredParams.in);
        trackPtr->setOutputParams(requiredParams.out);

        m_trackAboutToBeAdded.send(trackPtr);
        m_tracks.emplace(newId, trackPtr);
        m_trackAdded.send(newId);

        result.ret = make_ret(Err::NoError);
        result.val1 = newId;
        result.val2 = { trackPtr->inputParams(), trackPtr->outputParams() };

        return result;
    }

    RetVal2<TrackId, AudioParams> addTrack(const std::string& trackName, io::IODevice* device, const AudioParams& requiredParams) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        NOT_IMPLEMENTED;

        RetVal2<TrackId, AudioParams> result;

        if (!device) {
            result.ret = make_ret(Err::InvalidAudioFilePath);
            result.val1 = -1;
            return result;
        }

        TrackId newId = newTrackId();

        SoundTrackPtr trackPtr = std::make_shared<SoundTrack>();
        trackPtr->id = newId;
        trackPtr->name = trackName;
        trackPtr->setPlaybackData(device);
        trackPtr->setInputParams(requiredParams.in);
        trackPtr->setOutputParams(requiredParams.out);
        //TODO create AudioSource and MixerChannel

        m_trackAboutToBeAdded.send(trackPtr);
        m_tracks.emplace(newId, trackPtr);
        m_trackAdded.send(newId);

        result.ret = make_ret(Err::NoError);
        result.val1 = newId;
        result.val2 = { trackPtr->inputParams(), trackPtr->outputParams() };

        return result;
    }

    RetVal2<TrackId, AudioOutputParams> addAuxTrack(const std::string& trackName, const AudioOutputParams& requiredOutputParams) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        RetVal2<TrackId, AudioOutputParams> result;
        result.val1 = -1;

        IF_ASSERT_FAILED(mixer()) {
            result.ret = make_ret(Err::Undefined);
            return result;
        }

        TrackId newId = newTrackId();

        EventTrackPtr trackPtr = std::make_shared<EventTrack>();
        trackPtr->id = newId;
        trackPtr->name = trackName;
        trackPtr->outputHandler = mixer()->addAuxChannel(newId).val;
        trackPtr->setOutputParams(requiredOutputParams);

        m_trackAboutToBeAdded.send(trackPtr);
        m_tracks.emplace(newId, trackPtr);
        m_trackAdded.send(newId);

        result.ret = make_ret(Err::NoError);
        result.val1 = newId;
        result.val2 = trackPtr->outputParams();

        return result;
    }

    TrackName trackName(const TrackId id) const override
    {
        TrackPtr trackPtr = track(id);

        if (trackPtr) {
            return trackPtr->name;
        }

        static TrackName emptyName;
        return emptyName;
    }

    TrackIdList trackIdList() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TrackIdList result;

        for (const auto& pair : m_tracks) {
            result.push_back(pair.first);
        }

        return result;
    }

    Ret removeTrack(const TrackId id) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(mixer()) {
            return make_ret(Err::Undefined);
        }

        auto search = m_tracks.find(id);

        if (search != m_tracks.end() && search->second) {
            m_trackAboutToBeRemoved.send(search->second);
            mixer()->removeChannel(id);
            m_tracks.erase(id);
            m_trackRemoved.send(id);
            return true;
        }

        return make_ret(Err::InvalidTrackId);
    }

    void removeAllTracks() override
    {
        ONLY_AUDIO_WORKER_THREAD;

        for (const TrackId& id : trackIdList()) {
            removeTrack(id);
        }
    }

    async::Channel<TrackId> trackAdded() const override
    {
        return m_trackAdded;
    }

    async::Channel<TrackId> trackRemoved() const override
    {
        return m_trackRemoved;
    }

    ISequencePlayerPtr player() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_player;
    }

    ISequenceIOPtr audioIO() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_audioIO;
    }

    TrackPtr track(const TrackId id) const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        auto search = m_tracks.find(id);

        if (search != m_tracks.end()) {
            return search->second;
        }

        return nullptr;
    }

    const TracksMap& allTracks() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_tracks;
    }

    async::Channel<TrackPtr> trackAboutToBeAdded() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_trackAboutToBeAdded;
    }

    async::Channel<TrackPtr> trackAboutToBeRemoved() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_trackAboutToBeRemoved;
    }


private:
    // TrackId newTrackId() const;

    // std::shared_ptr<Mixer> mixer() const;

    TrackId newTrackId() const
    {
        if (m_tracks.empty()) {
            return static_cast<TrackId>(m_tracks.size());
        }

        auto last = m_tracks.rbegin();

        return last->first + 1;
    }

    std::shared_ptr<Mixer> mixer() const
    {
        return audioEngine()->mixer();
    }

    TrackSequenceId m_id = -1;

    TracksMap m_tracks;

    ISequencePlayerPtr m_player = nullptr;
    ISequenceIOPtr m_audioIO = nullptr;

    IClockPtr m_clock = nullptr;

    async::Channel<TrackId> m_trackAdded;
    async::Channel<TrackId> m_trackRemoved;

    async::Channel<TrackPtr> m_trackAboutToBeAdded;
    async::Channel<TrackPtr> m_trackAboutToBeRemoved;

    TrackId m_prevActiveTrackId = INVALID_TRACK_ID;
};
}

#endif // MUSE_AUDIO_TRACKSEQUENCE_H
