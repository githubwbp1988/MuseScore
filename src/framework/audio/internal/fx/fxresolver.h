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

#ifndef MUSE_AUDIO_FXRESOLVER_H
#define MUSE_AUDIO_FXRESOLVER_H

#include <map>
#include <mutex>

#include "../../ifxresolver.h"

#include "internal/audiosanitizer.h"

#include "log.h"

using namespace muse::async;
using namespace muse::audio;

namespace muse::audio::fx {
class FxResolver : public IFxResolver
{
public:
    // std::vector<IFxProcessorPtr> resolveMasterFxList(const AudioFxChain& fxChain) override;
    // std::vector<IFxProcessorPtr> resolveFxList(const TrackId trackId, const AudioFxChain& fxChain) override;
    // AudioResourceMetaList resolveAvailableResources() const override;
    // void registerResolver(const AudioFxType type, IResolverPtr resolver) override;
    // void clearAllFx() override;


    std::vector<IFxProcessorPtr> resolveMasterFxList(const AudioFxChain& fxChain) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TRACEFUNC;

        std::lock_guard lock(m_mutex);

        std::vector<IFxProcessorPtr> result;

        for (const auto& resolver : m_resolvers) {
            AudioFxChain fxChainByType;

            for (const auto& fx : fxChain) {
                if (resolver.first == fx.second.type() || fx.second.type() == AudioFxType::Undefined) {
                    fxChainByType.insert(fx);
                }
            }

            if (fxChainByType.empty()) {
                continue;
            }

            std::vector<IFxProcessorPtr> fxList = resolver.second->resolveMasterFxList(std::move(fxChainByType));
            result.insert(result.end(), fxList.begin(), fxList.end());
        }

        return result;
    }

    std::vector<IFxProcessorPtr> resolveFxList(const TrackId trackId, const AudioFxChain& fxChain) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        TRACEFUNC;

        std::lock_guard lock(m_mutex);

        std::vector<IFxProcessorPtr> result;

        for (const auto& resolver : m_resolvers) {
            AudioFxChain fxChainByType;

            for (const auto& fx : fxChain) {
                if (resolver.first == fx.second.type() || fx.second.type() == AudioFxType::Undefined) {
                    fxChainByType.insert(fx);
                }
            }

            if (fxChainByType.empty()) {
                continue;
            }

            std::vector<IFxProcessorPtr> fxList = resolver.second->resolveFxList(trackId, std::move(fxChainByType));
            result.insert(result.end(), fxList.begin(), fxList.end());
        }

        return result;
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

    void registerResolver(const AudioFxType type, IResolverPtr resolver) override
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        std::lock_guard lock(m_mutex);

        m_resolvers.insert_or_assign(type, std::move(resolver));
    }

    void clearAllFx() override
    {
        ONLY_AUDIO_MAIN_THREAD;

        std::lock_guard lock(m_mutex);

        for (auto it = m_resolvers.begin(); it != m_resolvers.end(); ++it) {
            it->second->clearAllFx();
        }
    }


private:
    std::map<AudioFxType, IResolverPtr> m_resolvers;
    mutable std::mutex m_mutex;
};
}

#endif // MUSE_AUDIO_FXRESOLVER_H
