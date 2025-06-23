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
#ifndef MUSE_AUDIO_MIXER_H
#define MUSE_AUDIO_MIXER_H

#include <memory>
#include <map>

#include "global/modularity/ioc.h"
#include "global/async/asyncable.h"
#include "global/types/retval.h"

#include "../../ifxresolver.h"
#include "../../iaudioconfiguration.h"
#include "../dsp/limiter.h"

#include "abstractaudiosource.h"
#include "mixerchannel.h"
#include "iclock.h"

#include "concurrency/taskscheduler.h"

#include "../audiosanitizer.h"
#include "../dsp/audiomathutils.h"
#include "../../audioerrors.h"

#include "log.h"

using namespace muse;
using namespace muse::async;

namespace muse {
class TaskScheduler;
}

namespace muse::audio {
class Mixer : public AbstractAudioSource, public Injectable, public async::Asyncable, public std::enable_shared_from_this<Mixer>
{
    Inject<fx::IFxResolver> fxResolver = { this };
    Inject<IAudioConfiguration> configuration = { this };

public:
    // Mixer(const modularity::ContextPtr& iocCtx);
    // ~Mixer();

    // IAudioSourcePtr mixedSource();

    // RetVal<MixerChannelPtr> addChannel(const TrackId trackId, ITrackAudioInputPtr source);
    // RetVal<MixerChannelPtr> addAuxChannel(const TrackId trackId);
    // Ret removeChannel(const TrackId trackId);

    // void setAudioChannelsCount(const audioch_t count);

    // void addClock(IClockPtr clock);
    // void removeClock(IClockPtr clock);

    // AudioOutputParams masterOutputParams() const;
    // void setMasterOutputParams(const AudioOutputParams& params);
    // void clearMasterOutputParams();
    // async::Channel<AudioOutputParams> masterOutputParamsChanged() const;

    // AudioSignalChanges masterAudioSignalChanges() const;

    // void setIsIdle(bool idle);
    // void setTracksToProcessWhenIdle(std::unordered_set<TrackId>&& trackIds);

    // // IAudioSource
    // void setSampleRate(unsigned int sampleRate) override;
    // unsigned int audioChannelsCount() const override;
    // samples_t process(float* outBuffer, samples_t samplesPerChannel) override;
    // void setIsActive(bool arg) override;

    Mixer(const muse::modularity::ContextPtr& iocCtx) : muse::Injectable(iocCtx)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_taskScheduler = std::make_unique<muse::TaskScheduler>(static_cast<thread_pool_size_t>(configuration()->desiredAudioThreadNumber()));

        if (!m_taskScheduler->setThreadsPriority(muse::ThreadPriority::High)) {
            LOGE() << "Unable to change audio threads priority";
        }

        muse::audio::AudioSanitizer::setMixerThreads(m_taskScheduler->threadIdSet());

        m_minTrackCountForMultithreading = configuration()->minTrackCountForMultithreading();
    }

    ~Mixer()
    {
        ONLY_AUDIO_WORKER_THREAD;
    }

    muse::audio::IAudioSourcePtr mixedSource()
    {
        ONLY_AUDIO_WORKER_THREAD;
        return shared_from_this();
    }

    RetVal<muse::audio::MixerChannelPtr> addChannel(const muse::audio::TrackId trackId, muse::audio::ITrackAudioInputPtr source)
    {
        ONLY_AUDIO_WORKER_THREAD;

        RetVal<muse::audio::MixerChannelPtr> result;

        if (!source) {
            result.val = nullptr;
            result.ret = make_ret(Err::InvalidAudioSource);
            return result;
        }

        muse::audio::MixerChannelPtr channel = std::make_shared<muse::audio::MixerChannel>(trackId, std::move(source), m_sampleRate, iocContext());
        std::weak_ptr<muse::audio::MixerChannel> channelWeakPtr = channel;

        m_nonMutedTrackCount++;

        channel->mutedChanged().onNotify(this, [this, channelWeakPtr]() {
            muse::audio::MixerChannelPtr channel = channelWeakPtr.lock();
            if (!channel) {
                return;
            }

            muse::audio::ITrackAudioInputPtr source = std::static_pointer_cast<muse::audio::ITrackAudioInput>(channel->source());

            if (channel->muted()) {
                if (source) {
                    source->setIsActive(false);
                }
                if (m_nonMutedTrackCount != 0) {
                    m_nonMutedTrackCount--;
                }
                return;
            }

            m_nonMutedTrackCount++;

            if (source) {
                source->setIsActive(isActive());
                source->seek(currentTime());
            }
        });

        m_trackChannels.emplace(trackId, channel);

        result.val = m_trackChannels[trackId];
        result.ret = make_ret(Ret::Code::Ok);

        return result;
    }

    RetVal<muse::audio::MixerChannelPtr> addAuxChannel(const muse::audio::TrackId trackId)
    {
        ONLY_AUDIO_WORKER_THREAD;

        const samples_t samplesToPreallocate = configuration()->samplesToPreallocate();
        const audioch_t audioChannelsCount = configuration()->audioChannelsCount();

        muse::audio::MixerChannelPtr channel = std::make_shared<MixerChannel>(trackId, m_sampleRate, audioChannelsCount, iocContext());

        AuxChannelInfo aux;
        aux.channel = channel;
        aux.buffer = std::vector<float>(samplesToPreallocate * audioChannelsCount, 0.f);

        m_auxChannelInfoList.emplace_back(std::move(aux));

        RetVal<muse::audio::MixerChannelPtr> result;
        result.val = channel;
        result.ret = make_ret(Ret::Code::Ok);

        return result;
    }

    Ret removeChannel(const muse::audio::TrackId trackId)
    {
        ONLY_AUDIO_WORKER_THREAD;

        auto search = m_trackChannels.find(trackId);

        if (search != m_trackChannels.end() && search->second) {
            if (m_nonMutedTrackCount != 0) {
                m_nonMutedTrackCount--;
            }

            m_trackChannels.erase(trackId);
            return make_ret(Ret::Code::Ok);
        }

        bool removed = muse::remove_if(m_auxChannelInfoList, [trackId](const AuxChannelInfo& aux) {
            return aux.channel->trackId() == trackId;
        });

        return removed ? make_ret(Ret::Code::Ok) : make_ret(Err::InvalidTrackId);
    }

    void setAudioChannelsCount(const audioch_t count)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_audioChannelsCount = count;
    }

    void setSampleRate(unsigned int sampleRate) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_limiter = std::make_unique<muse::audio::dsp::Limiter>(sampleRate);

        muse::audio::AbstractAudioSource::setSampleRate(sampleRate);

        for (auto& channel : m_trackChannels) {
            channel.second->setSampleRate(sampleRate);
        }

        for (AuxChannelInfo& aux : m_auxChannelInfoList) {
            aux.channel->setSampleRate(sampleRate);
        }

        for (muse::audio::IFxProcessorPtr& fx : m_masterFxProcessors) {
            fx->setSampleRate(sampleRate);
        }
    }

    unsigned int audioChannelsCount() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_audioChannelsCount;
    }

    samples_t process(float* outBuffer, samples_t samplesPerChannel) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        for (const IClockPtr& clock : m_clocks) {
            clock->forward((samplesPerChannel * 1000000) / m_sampleRate);
        }

        size_t outBufferSize = samplesPerChannel * m_audioChannelsCount;
        std::fill(outBuffer, outBuffer + outBufferSize, 0.f);

        if (m_isIdle && m_tracksToProcessWhenIdle.empty() && m_isSilence) {
            notifyNoAudioSignal();
            return 0;
        }

        TracksData tracksData;
        processTrackChannels(outBufferSize, samplesPerChannel, tracksData);

        prepareAuxBuffers(outBufferSize);

        for (auto& pair : tracksData) {
            auto channelIt = m_trackChannels.find(pair.first);
            if (channelIt == m_trackChannels.cend()) {
                continue;
            }

            const muse::audio::MixerChannelPtr& channel = channelIt->second;
            if (!channel->isSilent()) {
                m_isSilence = false;
            } else if (m_isSilence) {
                continue;
            }

            const std::vector<float>& trackBuffer = pair.second;
            mixOutputFromChannel(outBuffer, trackBuffer.data(), samplesPerChannel);
            writeTrackToAuxBuffers(trackBuffer.data(), channel->outputParams().auxSends, samplesPerChannel);
        }

        if (m_masterParams.muted || samplesPerChannel == 0 || m_isSilence) {
            notifyNoAudioSignal();
            return 0;
        }

        // LOGALEX() << "processAuxChannels(outBuffer, samplesPerChannel)";

        processAuxChannels(outBuffer, samplesPerChannel);
        completeOutput(outBuffer, samplesPerChannel);

        for (muse::audio::IFxProcessorPtr& fxProcessor : m_masterFxProcessors) {
            if (fxProcessor->active()) {
                fxProcessor->process(outBuffer, samplesPerChannel);
            }
        }

        return samplesPerChannel;
    }

    void setIsActive(bool arg) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        muse::audio::AbstractAudioSource::setIsActive(arg);

        for (auto& channel : m_trackChannels) {
            if (!channel.second->muted()) {
                channel.second->setIsActive(arg);
            }
        }

        for (auto& aux : m_auxChannelInfoList) {
            if (!aux.channel->muted()) {
                aux.channel->setIsActive(arg);
            }
        }
    }

    void addClock(muse::audio::IClockPtr clock)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_clocks.insert(std::move(clock));
    }

    void removeClock(muse::audio::IClockPtr clock)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_clocks.erase(clock);
    }

    muse::audio::AudioOutputParams masterOutputParams() const
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_masterParams;
    }

    void setMasterOutputParams(const muse::audio::AudioOutputParams& params)
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (m_masterParams == params) {
            return;
        }

        m_masterFxProcessors.clear();
        m_masterFxProcessors = fxResolver()->resolveMasterFxList(params.fxChain);

        for (muse::audio::IFxProcessorPtr& fx : m_masterFxProcessors) {
            fx->setSampleRate(m_sampleRate);
            fx->paramsChanged().onReceive(this, [this](const AudioFxParams& fxParams) {
                m_masterParams.fxChain.insert_or_assign(fxParams.chainOrder, fxParams);
                m_masterOutputParamsChanged.send(m_masterParams);
            });
        }

        AudioOutputParams resultParams = params;

        auto findFxProcessor = [this](const std::pair<muse::audio::AudioFxChainOrder, muse::audio::AudioFxParams>& params) -> IFxProcessorPtr {
            for (muse::audio::IFxProcessorPtr& fx : m_masterFxProcessors) {
                if (fx->params().chainOrder != params.first) {
                    continue;
                }

                if (fx->params().resourceMeta == params.second.resourceMeta) {
                    return fx;
                }
            }

            return nullptr;
        };

        for (auto it = resultParams.fxChain.begin(); it != resultParams.fxChain.end();) {
            if (muse::audio::IFxProcessorPtr fx = findFxProcessor(*it)) {
                fx->setActive(it->second.active);
                ++it;
            } else {
                it = resultParams.fxChain.erase(it);
            }
        }

        m_masterParams = resultParams;
        m_masterOutputParamsChanged.send(resultParams);
    }

    void clearMasterOutputParams()
    {
        setMasterOutputParams(muse::audio::AudioOutputParams());
    }

    Channel<muse::audio::AudioOutputParams> masterOutputParamsChanged() const
    {
        return m_masterOutputParamsChanged;
    }

    muse::audio::AudioSignalChanges masterAudioSignalChanges() const
    {
        return m_audioSignalNotifier.audioSignalChanges;
    }

    void setIsIdle(bool idle)
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (m_isIdle == idle) {
            return;
        }

        m_isIdle = idle;
        m_tracksToProcessWhenIdle.clear();
    }

    void setTracksToProcessWhenIdle(std::unordered_set<muse::audio::TrackId>&& trackIds)
    {
        ONLY_AUDIO_WORKER_THREAD;

        m_tracksToProcessWhenIdle = std::move(trackIds);
    }


private:
    using TracksData = std::map<muse::audio::TrackId, std::vector<float> >;

    // void processTrackChannels(size_t outBufferSize, size_t samplesPerChannel, TracksData& outTracksData);
    // void mixOutputFromChannel(float* outBuffer, const float* inBuffer, unsigned int samplesCount) const;
    // void prepareAuxBuffers(size_t outBufferSize);
    // void writeTrackToAuxBuffers(const float* trackBuffer, const AuxSendsParams& auxSends, samples_t samplesPerChannel);
    // void processAuxChannels(float* buffer, samples_t samplesPerChannel);
    // void completeOutput(float* buffer, samples_t samplesPerChannel);

    // bool useMultithreading() const;

    // void notifyNoAudioSignal();

    // msecs_t currentTime() const;

    void processTrackChannels(size_t outBufferSize, size_t samplesPerChannel, TracksData& outTracksData)
    {
        auto processChannel = [outBufferSize, samplesPerChannel](MixerChannelPtr channel) -> std::vector<float> {
            thread_local std::vector<float> buffer(outBufferSize, 0.f);
            thread_local std::vector<float> silent_buffer(outBufferSize, 0.f);

            if (buffer.size() < outBufferSize) {
                buffer.resize(outBufferSize, 0.f);
                silent_buffer.resize(outBufferSize, 0.f);
            }

            buffer = silent_buffer;

            if (channel) {
                channel->process(buffer.data(), samplesPerChannel);
            }

            return buffer;
        };

        bool filterTracks = m_isIdle && !m_tracksToProcessWhenIdle.empty();

        if (useMultithreading()) {
            std::map<TrackId, std::future<std::vector<float> > > futures;

            for (const auto& pair : m_trackChannels) {
                if (filterTracks && !muse::contains(m_tracksToProcessWhenIdle, pair.second->trackId())) {
                    continue;
                }

                if (pair.second->muted() && pair.second->isSilent()) {
                    pair.second->notifyNoAudioSignal();
                    continue;
                }

                std::future<std::vector<float> > future = m_taskScheduler->submit(processChannel, pair.second);
                futures.emplace(pair.first, std::move(future));
            }

            for (auto& pair : futures) {
                outTracksData.emplace(pair.first, pair.second.get());
            }
        } else {
            for (const auto& pair : m_trackChannels) {
                if (filterTracks && !muse::contains(m_tracksToProcessWhenIdle, pair.second->trackId())) {
                    continue;
                }

                if (pair.second->muted() && pair.second->isSilent()) {
                    pair.second->notifyNoAudioSignal();
                    continue;
                }

                outTracksData.emplace(pair.first, processChannel(pair.second));
            }
        }
    }

    void mixOutputFromChannel(float* outBuffer, const float* inBuffer, unsigned int samplesCount) const
    {
        IF_ASSERT_FAILED(outBuffer && inBuffer) {
            return;
        }

        if (m_masterParams.muted) {
            return;
        }

        for (samples_t s = 0; s < samplesCount; ++s) {
            size_t samplePos = s * m_audioChannelsCount;

            for (audioch_t audioChNum = 0; audioChNum < m_audioChannelsCount; ++audioChNum) {
                size_t idx = samplePos + audioChNum;
                float sample = inBuffer[idx];
                outBuffer[idx] += sample;
            }
        }
    }

    void prepareAuxBuffers(size_t outBufferSize)
    {
        for (AuxChannelInfo& aux : m_auxChannelInfoList) {
            aux.receivedAudioSignal = false;

            if (aux.channel->outputParams().fxChain.empty()) {
                continue;
            }

            if (aux.buffer.size() < outBufferSize) {
                aux.buffer.resize(outBufferSize);
            }

            std::fill(aux.buffer.begin(), aux.buffer.begin() + outBufferSize, 0.f);
        }
    }

    void writeTrackToAuxBuffers(const float* trackBuffer, const AuxSendsParams& auxSends, samples_t samplesPerChannel)
    {
        for (aux_channel_idx_t auxIdx = 0; auxIdx < auxSends.size(); ++auxIdx) {
            if (auxIdx >= m_auxChannelInfoList.size()) {
                break;
            }

            AuxChannelInfo& aux = m_auxChannelInfoList.at(auxIdx);
            if (aux.channel->outputParams().fxChain.empty()) {
                continue;
            }

            const AuxSendParams& auxSend = auxSends.at(auxIdx);
            if (!auxSend.active || RealIsNull(auxSend.signalAmount)) {
                continue;
            }

            float* auxBuffer = aux.buffer.data();
            float signalAmount = auxSend.signalAmount;

            for (samples_t s = 0; s < samplesPerChannel; ++s) {
                size_t samplePos = s * m_audioChannelsCount;

                for (audioch_t audioChNum = 0; audioChNum < m_audioChannelsCount; ++audioChNum) {
                    size_t idx = samplePos + audioChNum;
                    auxBuffer[idx] += trackBuffer[idx] * signalAmount;
                }
            }

            aux.receivedAudioSignal = true;
        }
    }

    void processAuxChannels(float* buffer, samples_t samplesPerChannel)
    {
        for (AuxChannelInfo& aux : m_auxChannelInfoList) {
            if (!aux.receivedAudioSignal) {
                continue;
            }

            float* auxBuffer = aux.buffer.data();
            aux.channel->process(auxBuffer, samplesPerChannel);

            if (!aux.channel->isSilent()) {
                mixOutputFromChannel(buffer, auxBuffer, samplesPerChannel);
            }
        }
    }

    void completeOutput(float* buffer, samples_t samplesPerChannel)
    {
        IF_ASSERT_FAILED(buffer) {
            return;
        }

        float totalSquaredSum = 0.f;
        float volume = muse::db_to_linear(m_masterParams.volume);

        for (audioch_t audioChNum = 0; audioChNum < m_audioChannelsCount; ++audioChNum) {
            float singleChannelSquaredSum = 0.f;
            gain_t totalGain = dsp::balanceGain(m_masterParams.balance, audioChNum) * volume;

            for (samples_t s = 0; s < samplesPerChannel; ++s) {
                int idx = s * m_audioChannelsCount + audioChNum;

                float resultSample = buffer[idx] * totalGain;
                buffer[idx] = resultSample;

                float squaredSample = resultSample * resultSample;
                totalSquaredSum += squaredSample;
                singleChannelSquaredSum += squaredSample;
            }

            float rms = dsp::samplesRootMeanSquare(singleChannelSquaredSum, samplesPerChannel);
            m_audioSignalNotifier.updateSignalValues(audioChNum, rms);
        }

        m_isSilence = RealIsNull(totalSquaredSum);
        m_audioSignalNotifier.notifyAboutChanges();

        if (!m_limiter->isActive()) {
            return;
        }

        float totalRms = dsp::samplesRootMeanSquare(totalSquaredSum, samplesPerChannel * m_audioChannelsCount);
        m_limiter->process(totalRms, buffer, m_audioChannelsCount, samplesPerChannel);
    }

    bool useMultithreading() const
    {
        if (m_nonMutedTrackCount < m_minTrackCountForMultithreading) {
            return false;
        }

        if (m_isIdle) {
            if (m_tracksToProcessWhenIdle.size() < m_minTrackCountForMultithreading) {
                return false;
            }
        }

        return true;
    }

    void notifyNoAudioSignal()
    {
        for (audioch_t audioChNum = 0; audioChNum < m_audioChannelsCount; ++audioChNum) {
            m_audioSignalNotifier.updateSignalValues(audioChNum, 0.f);
        }

        m_audioSignalNotifier.notifyAboutChanges();
    }

    msecs_t currentTime() const
    {
        if (m_clocks.empty()) {
            return 0;
        }

        IClockPtr clock = *m_clocks.begin();
        return clock->currentTime();
    }

    std::unique_ptr<muse::TaskScheduler> m_taskScheduler;

    size_t m_minTrackCountForMultithreading = 0;
    size_t m_nonMutedTrackCount = 0;

    muse::audio::AudioOutputParams m_masterParams;
    async::Channel<muse::audio::AudioOutputParams> m_masterOutputParamsChanged;
    std::vector<muse::audio::IFxProcessorPtr> m_masterFxProcessors = {};

    std::map<muse::audio::TrackId, muse::audio::MixerChannelPtr> m_trackChannels = {};
    std::unordered_set<muse::audio::TrackId> m_tracksToProcessWhenIdle;

    struct AuxChannelInfo {
        muse::audio::MixerChannelPtr channel;
        std::vector<float> buffer;
        bool receivedAudioSignal = false;
    };

    std::vector<AuxChannelInfo> m_auxChannelInfoList;

    muse::audio::dsp::LimiterPtr m_limiter = nullptr;

    std::set<muse::audio::IClockPtr> m_clocks;
    audioch_t m_audioChannelsCount = 0;

    mutable muse::audio::AudioSignalsNotifier m_audioSignalNotifier;

    bool m_isSilence = false;
    bool m_isIdle = false;
};

using MixerPtr = std::shared_ptr<Mixer>;
}

#endif // MUSE_AUDIO_MIXER_H
