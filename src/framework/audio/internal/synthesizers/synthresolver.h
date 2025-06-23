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

#ifndef MUSE_AUDIO_SYNTHRESOLVER_H
#define MUSE_AUDIO_SYNTHRESOLVER_H

#include <map>
#include <mutex>

#include "../../isynthresolver.h"

#include "../audiosanitizer.h"

#include "log.h"

using namespace muse::async;
using namespace muse::audio;

namespace muse::audio::synth {
class SynthResolver : public ISynthResolver
{
public:
    // void init(const AudioInputParams& defaultInputParams) override;

    // ISynthesizerPtr resolveSynth(const TrackId trackId, const AudioInputParams& params, const PlaybackSetupData& setupData) const override;
    // ISynthesizerPtr resolveDefaultSynth(const TrackId trackId) const override;
    // AudioInputParams resolveDefaultInputParams() const override;
    // AudioResourceMetaList resolveAvailableResources() const override;
    // SoundPresetList resolveAvailableSoundPresets(const AudioResourceMeta& resourceMeta) const override;

    // void registerResolver(const AudioSourceType type, IResolverPtr resolver) override;

    // void clearSources() override;

    void init(const AudioInputParams& defaultInputParams) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(defaultInputParams.isValid()) {
            return;
        }

        m_defaultInputParams = defaultInputParams;
    }

    ISynthesizerPtr resolveSynth(const TrackId trackId, const AudioInputParams& params, const PlaybackSetupData& setupData) const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TRACEFUNC;

        if (!params.isValid()) {
            LOGE() << "invalid audio source params for trackId: " << trackId;
            return nullptr;
        }

        std::lock_guard lock(m_mutex);

        auto search = m_resolvers.find(params.type());

        if (search == m_resolvers.end()) {
            return nullptr;
        }

        const IResolverPtr& resolver = search->second;

        if (!resolver->hasCompatibleResources(setupData)) {
            return nullptr;
        }

        return resolver->resolveSynth(trackId, params);
    }

    ISynthesizerPtr resolveDefaultSynth(const TrackId trackId) const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return resolveSynth(trackId, m_defaultInputParams, {});
    }

    AudioInputParams resolveDefaultInputParams() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_defaultInputParams;
    }

    AudioResourceMetaList resolveAvailableResources() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TRACEFUNC;

        std::lock_guard lock(m_mutex);

        AudioResourceMetaList result;

        for (const auto& pair : m_resolvers) {
            const AudioResourceMetaList& resolvedResources = pair.second->resolveResources();
            result.insert(result.end(), resolvedResources.begin(), resolvedResources.end());
        }

        return result;
    }

    SoundPresetList resolveAvailableSoundPresets(const AudioResourceMeta& resourceMeta) const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TRACEFUNC;

        std::lock_guard lock(m_mutex);

        auto search = m_resolvers.find(audio::sourceTypeFromResourceType(resourceMeta.type));
        if (search == m_resolvers.end()) {
            return SoundPresetList();
        }

        return search->second->resolveSoundPresets(resourceMeta);
    }

    void registerResolver(const AudioSourceType type, IResolverPtr resolver) override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        std::lock_guard lock(m_mutex);

        m_resolvers.insert_or_assign(type, std::move(resolver));
    }

    void clearSources() override
    {
        ONLY_AUDIO_MAIN_THREAD;

        std::lock_guard lock(m_mutex);

        for (auto it = m_resolvers.begin(); it != m_resolvers.end(); ++it) {
            it->second->clearSources();
        }
    }


private:
    using SynthPair = std::pair<audio::AudioResourceId, ISynthesizerPtr>;

    mutable std::mutex m_mutex;

    std::map<AudioSourceType, IResolverPtr> m_resolvers;
    AudioInputParams m_defaultInputParams;
};
}

#endif // MUSE_AUDIO_SYNTHRESOLVER_H
