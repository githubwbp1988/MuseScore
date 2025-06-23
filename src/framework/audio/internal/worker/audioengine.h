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
#ifndef MUSE_AUDIO_AUDIOENGINE_H
#define MUSE_AUDIO_AUDIOENGINE_H

#include <memory>

#include "global/async/asyncable.h"
#include "global/async/notification.h"
#include "global/types/ret.h"

#include "iaudioengine.h"
#include "mixer.h"

#include "../audiobuffer.h"
#include "../audiosanitizer.h"

#include "log.h"

using namespace muse;

namespace muse::audio {
class AudioBuffer;
class AudioEngine : public IAudioEngine, public Injectable, public async::Asyncable
{
public:
    AudioEngine(const modularity::ContextPtr& iocCtx)
        : Injectable(iocCtx) {}
    // ~AudioEngine();

    struct RenderConstraints {
        samples_t minSamplesToReserveWhenIdle = 0;
        samples_t minSamplesToReserveInRealtime = 0;
    };

    // Ret init(std::shared_ptr<AudioBuffer> bufferPtr, const RenderConstraints& consts);
    // void deinit();

    using OnReadBufferChanged = std::function<void (const samples_t, const sample_rate_t)>;
    // void setOnReadBufferChanged(const OnReadBufferChanged func);

    // sample_rate_t sampleRate() const override;

    // void setSampleRate(const sample_rate_t sampleRate) override;
    // void setReadBufferSize(const uint16_t readBufferSize) override;
    // void setAudioChannelsCount(const audioch_t count) override;

    // RenderMode mode() const override;
    // void setMode(const RenderMode newMode) override;
    // async::Notification modeChanged() const override;

    // MixerPtr mixer() const override;

    ~AudioEngine()
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;
    }

    Ret init(std::shared_ptr<AudioBuffer> bufferPtr, const RenderConstraints& consts)
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (m_inited) {
            return make_ret(Ret::Code::Ok);
        }

        IF_ASSERT_FAILED(bufferPtr) {
            return make_ret(Ret::Code::InternalError);
        }

        IF_ASSERT_FAILED(consts.minSamplesToReserveWhenIdle != 0
                        && consts.minSamplesToReserveInRealtime != 0) {
            return make_ret(Ret::Code::InternalError);
        }

        m_mixer = std::make_shared<Mixer>(iocContext());
        m_buffer = std::move(bufferPtr);
        m_renderConsts = consts;

        setMode(RenderMode::IdleMode);

        m_inited = true;

        return make_ret(Ret::Code::Ok);
    }

    void deinit()
    {
        ONLY_AUDIO_WORKER_THREAD;
        if (m_inited) {
            m_buffer->setSource(nullptr);
            m_buffer = nullptr;
            m_mixer = nullptr;
            m_inited = false;
        }
    }

    void setOnReadBufferChanged(const OnReadBufferChanged func)
    {
        ONLY_AUDIO_MAIN_OR_WORKER_THREAD;

        m_onReadBufferChanged = func;
    }

    sample_rate_t sampleRate() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_sampleRate;
    }

    void setSampleRate(const sample_rate_t sampleRate) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(m_mixer) {
            return;
        }

        if (m_sampleRate == sampleRate) {
            return;
        }

        LOGI() << "Sample rate: " << sampleRate;

        m_sampleRate = sampleRate;
        m_mixer->mixedSource()->setSampleRate(sampleRate);

        if (m_onReadBufferChanged) {
            m_onReadBufferChanged(m_readBufferSize, m_sampleRate);
        }
    }

    void setReadBufferSize(const uint16_t readBufferSize) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (m_readBufferSize == readBufferSize) {
            return;
        }

        LOGI() << "Read buffer size: " << readBufferSize;

        m_readBufferSize = readBufferSize;
        updateBufferConstraints();

        if (m_onReadBufferChanged) {
            m_onReadBufferChanged(m_readBufferSize, m_sampleRate);
        }
    }

    void setAudioChannelsCount(const audioch_t count) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        IF_ASSERT_FAILED(m_mixer) {
            return;
        }

        LOGI() << "Audio channels: " << count;

        m_mixer->setAudioChannelsCount(count);
    }

    RenderMode mode() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_currentMode;
    }

    void setMode(const RenderMode newMode) override
    {
        ONLY_AUDIO_WORKER_THREAD;

        if (newMode == m_currentMode) {
            return;
        }

        m_currentMode = newMode;

        switch (m_currentMode) {
        case RenderMode::RealTimeMode:
            m_buffer->setSource(m_mixer->mixedSource());
            m_mixer->setIsIdle(false);
            break;
        case RenderMode::IdleMode:
            m_buffer->setSource(m_mixer->mixedSource());
            m_mixer->setIsIdle(true);
            break;
        case RenderMode::OfflineMode:
            m_buffer->setSource(nullptr);
            m_mixer->setIsIdle(false);
            break;
        case RenderMode::Undefined:
            UNREACHABLE;
            break;
        }

        updateBufferConstraints();

        m_modeChanges.notify();
    }

    async::Notification modeChanged() const override
    {
        ONLY_AUDIO_WORKER_THREAD;

        return m_modeChanges;
    }

    MixerPtr mixer() const override
    {
        ONLY_AUDIO_WORKER_THREAD;
        return m_mixer;
    }

private:

    // void updateBufferConstraints();

    void updateBufferConstraints()
    {
        IF_ASSERT_FAILED(m_buffer) {
            return;
        }

        if (m_readBufferSize == 0) {
            return;
        }

        samples_t minSamplesToReserve = 0;

        if (m_currentMode == RenderMode::IdleMode) {
            minSamplesToReserve = std::max(m_readBufferSize, m_renderConsts.minSamplesToReserveWhenIdle);
        } else {
            minSamplesToReserve = std::max(m_readBufferSize, m_renderConsts.minSamplesToReserveInRealtime);
        }

        m_buffer->setMinSamplesPerChannelToReserve(minSamplesToReserve);
        m_buffer->setRenderStep(minSamplesToReserve);
    }

    bool m_inited = false;

    sample_rate_t m_sampleRate = 0;
    samples_t m_readBufferSize = 0;

    MixerPtr m_mixer = nullptr;
    std::shared_ptr<AudioBuffer> m_buffer = nullptr;
    RenderConstraints m_renderConsts;

    RenderMode m_currentMode = RenderMode::Undefined;
    async::Notification m_modeChanges;

    OnReadBufferChanged m_onReadBufferChanged;
};
}

#endif // MUSE_AUDIO_AUDIOENGINE_H
