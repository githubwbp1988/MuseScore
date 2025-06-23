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

#ifndef MUSE_AUDIO_SEQUENCETRACKS_H
#define MUSE_AUDIO_SEQUENCETRACKS_H

#include "global/modularity/ioc.h"
#include "global/async/asyncable.h"

#include "../../isynthresolver.h"
#include "../../itracks.h"
#include "igettracksequence.h"

#include "global/async/async.h"

#include "../audiothread.h"
#include "../audiosanitizer.h"
#include "../../audioerrors.h"

#include "log.h"

using namespace muse::async;

namespace muse::audio {
class TracksHandler : public ITracks, public Injectable, public async::Asyncable
{
    Inject<synth::ISynthResolver> resolver = { this };

public:
    // explicit TracksHandler(IGetTrackSequence* getSequence, const modularity::ContextPtr& iocCtx);
    // ~TracksHandler();

    // async::Promise<TrackIdList> trackIdList(const TrackSequenceId sequenceId) const override;
    // async::Promise<TrackName> trackName(const TrackSequenceId sequenceId, const TrackId trackId) const override;

    // async::Promise<TrackId, AudioParams> addTrack(const TrackSequenceId sequenceId, const std::string& trackName,
    //                                               io::IODevice* playbackData, AudioParams&& params) override;

    // async::Promise<TrackId, AudioParams> addTrack(const TrackSequenceId sequenceId, const std::string& trackName,
    //                                               const mpe::PlaybackData& playbackData, AudioParams&& params) override;

    // async::Promise<TrackId, AudioOutputParams> addAuxTrack(const TrackSequenceId sequenceId, const std::string& trackName,
    //                                                        const AudioOutputParams& outputParams) override;

    // void removeTrack(const TrackSequenceId sequenceId, const TrackId trackId) override;
    // void removeAllTracks(const TrackSequenceId sequenceId) override;

    // async::Channel<TrackSequenceId, TrackId> trackAdded() const override;
    // async::Channel<TrackSequenceId, TrackId> trackRemoved() const override;

    // async::Promise<AudioResourceMetaList> availableInputResources() const override;
    // async::Promise<SoundPresetList> availableSoundPresets(const AudioResourceMeta& resourceMeta) const override;

    // async::Promise<AudioInputParams> inputParams(const TrackSequenceId sequenceId, const TrackId trackId) const override;
    // void setInputParams(const TrackSequenceId sequenceId, const TrackId trackId, const AudioInputParams& params) override;
    // async::Channel<TrackSequenceId, TrackId, AudioInputParams> inputParamsChanged() const override;

    // void clearSources() override;


    explicit TracksHandler(muse::audio::IGetTrackSequence* getSequence, const muse::modularity::ContextPtr& iocCtx)
    : Injectable(iocCtx), m_getSequence(getSequence)
    {
    }

    ~TracksHandler()
    {
        m_getSequence = nullptr;
    }

    muse::async::Promise<TrackIdList> trackIdList(const TrackSequenceId sequenceId) const override
    {
        return Promise<TrackIdList>([this, sequenceId](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (s) {
                return resolve(s->trackIdList());
            } else {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }
        }, AudioThread::ID);
    }

    muse::async::Promise<TrackName> trackName(const TrackSequenceId sequenceId, const TrackId trackId) const override
    {
        return Promise<TrackName>([this, sequenceId, trackId](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (s) {
                return resolve(s->trackName(trackId));
            } else {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }
        }, AudioThread::ID);
    }

    muse::async::Promise<TrackId, AudioParams> addTrack(const TrackSequenceId sequenceId, const std::string& trackName,
                                                  io::IODevice* playbackData, AudioParams&& params) override
    {
        return Promise<TrackId, AudioParams>([this, sequenceId, trackName, playbackData, params](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }

            RetVal2<TrackId, AudioParams> result = s->addTrack(trackName, playbackData, params);

            if (!result.ret) {
                return reject(result.ret.code(), result.ret.text());
            }

            return resolve(result.val1, result.val2);
        }, AudioThread::ID);
    }

    muse::async::Promise<TrackId, AudioParams> addTrack(const TrackSequenceId sequenceId, const std::string& trackName,
                                                  const mpe::PlaybackData& playbackData, AudioParams&& params) override
    {
        return Promise<TrackId, AudioParams>([this, sequenceId, trackName, playbackData, params](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }

            RetVal2<TrackId, AudioParams> result = s->addTrack(trackName, playbackData, params);

            if (!result.ret) {
                return reject(result.ret.code(), result.ret.text());
            }

            return resolve(result.val1, result.val2);
        }, AudioThread::ID);
    }

    muse::async::Promise<TrackId, AudioOutputParams> addAuxTrack(const TrackSequenceId sequenceId, const std::string& trackName,
                                                           const AudioOutputParams& outputParams) override
    {
        return Promise<TrackId, AudioOutputParams>([this, sequenceId, trackName, outputParams](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }

            RetVal2<TrackId, AudioOutputParams> result = s->addAuxTrack(trackName, outputParams);

            if (!result.ret) {
                return reject(result.ret.code(), result.ret.text());
            }

            return resolve(result.val1, result.val2);
        }, AudioThread::ID);
    }

    void removeTrack(const TrackSequenceId sequenceId, const TrackId trackId) override
    {
        Async::call(this, [this, sequenceId, trackId]() {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return;
            }

            s->removeTrack(trackId);
        }, AudioThread::ID);
    }

    void removeAllTracks(const TrackSequenceId sequenceId) override
    {
        Async::call(this, [this, sequenceId]() {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return;
            }

            for (const TrackId& id : s->trackIdList()) {
                s->removeTrack(id);
            }
        }, AudioThread::ID);
    }

    muse::async::Channel<TrackSequenceId, TrackId> trackAdded() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_trackAdded;
    }

    muse::async::Channel<TrackSequenceId, TrackId> trackRemoved() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_trackRemoved;
    }

    muse::async::Promise<AudioResourceMetaList> availableInputResources() const override
    {
        return Promise<AudioResourceMetaList>([this](auto resolve, auto /*reject*/) {
            ONLY_AUDIO_WORKER_THREAD;

            return resolve(resolver()->resolveAvailableResources());
        }, AudioThread::ID);
    }

    muse::async::Promise<SoundPresetList> availableSoundPresets(const AudioResourceMeta& resourceMeta) const override
    {
        return Promise<SoundPresetList>([this, resourceMeta](auto resolve, auto /*reject*/) {
            ONLY_AUDIO_WORKER_THREAD;

            return resolve(resolver()->resolveAvailableSoundPresets(resourceMeta));
        }, AudioThread::ID);
    }

    muse::async::Promise<AudioInputParams> inputParams(const TrackSequenceId sequenceId, const TrackId trackId) const override
    {
        return Promise<AudioInputParams>([this, sequenceId, trackId](auto resolve, auto reject) {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (!s) {
                return reject(static_cast<int>(Err::InvalidSequenceId), "invalid sequence id");
            }

            RetVal<AudioInputParams> result = s->audioIO()->inputParams(trackId);

            if (!result.ret) {
                return reject(result.ret.code(), result.ret.text());
            }

            return resolve(result.val);
        }, AudioThread::ID);
    }

    void setInputParams(const TrackSequenceId sequenceId, const TrackId trackId, const AudioInputParams& params) override
    {
        Async::call(this, [this, sequenceId, trackId, params]() {
            ONLY_AUDIO_WORKER_THREAD;

            ITrackSequencePtr s = sequence(sequenceId);

            if (s) {
                s->audioIO()->setInputParams(trackId, params);
            }
        }, AudioThread::ID);
    }

    muse::async::Channel<TrackSequenceId, TrackId, AudioInputParams> inputParamsChanged() const override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        return m_inputParamsChanged;
    }

    void clearSources() override
    {
        resolver()->clearSources();
    }


private:
    // ITrackSequencePtr sequence(const TrackSequenceId id) const;
    // void ensureSubscriptions(const ITrackSequencePtr s) const;

    ITrackSequencePtr sequence(const TrackSequenceId id) const
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(m_getSequence) {
            return nullptr;
        }

        ITrackSequencePtr s = m_getSequence->sequence(id);
        ensureSubscriptions(s);

        return s;
    }

    void ensureSubscriptions(const ITrackSequencePtr s) const
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (!s) {
            return;
        }

        TrackSequenceId sequenceId = s->id();

        if (!s->audioIO()->inputParamsChanged().isConnected()) {
            s->audioIO()->inputParamsChanged().onReceive(this, [this, sequenceId](const TrackId trackId, const AudioInputParams& params) {
                m_inputParamsChanged.send(sequenceId, trackId, params);
            });
        }

        if (!s->trackAdded().isConnected()) {
            s->trackAdded().onReceive(this, [this, sequenceId](const TrackId trackId) {
                m_trackAdded.send(sequenceId, trackId);
            });
        }

        if (!s->trackRemoved().isConnected()) {
            s->trackRemoved().onReceive(this, [this, sequenceId](const TrackId trackId) {
                m_trackRemoved.send(sequenceId, trackId);
            });
        }
    }

    mutable muse::async::Channel<TrackSequenceId, TrackId> m_trackAdded;
    mutable muse::async::Channel<TrackSequenceId, TrackId> m_trackRemoved;
    mutable muse::async::Channel<TrackSequenceId, TrackId, AudioInputParams> m_inputParamsChanged;

    muse::audio::IGetTrackSequence* m_getSequence = nullptr;
};
}

#endif // MUSE_AUDIO_SEQUENCETRACKS_H
