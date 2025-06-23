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
#ifndef MUSE_AUDIO_AUDIOCONFIGURATION_H
#define MUSE_AUDIO_AUDIOCONFIGURATION_H

#include "global/modularity/ioc.h"
#include "global/io/ifilesystem.h"
#include "global/iglobalconfiguration.h"

#include "../iaudioconfiguration.h"

#include <QtGlobal>

#include "global/settings.h"

#include "../soundfonttypes.h"

#include "log.h"

using namespace muse;
// using namespace muse::audio;
using namespace muse::audio::synth;

static const audioch_t AUDIO_CHANNELS = 2;

//TODO: add other setting: audio device etc
static const Settings::Key AUDIO_API_KEY("audio", "io/audioApi");
static const Settings::Key AUDIO_OUTPUT_DEVICE_ID_KEY("audio", "io/outputDevice");
static const Settings::Key AUDIO_BUFFER_SIZE_KEY("audio", "io/bufferSize");
static const Settings::Key AUDIO_SAMPLE_RATE_KEY("audio", "io/sampleRate");
static const Settings::Key AUDIO_MEASURE_INPUT_LAG("audio", "io/measureInputLag");
static const Settings::Key AUDIO_DESIRED_THREAD_NUMBER_KEY("audio", "io/audioThreads");

static const Settings::Key USER_SOUNDFONTS_PATHS("midi", "application/paths/mySoundfonts");

static const AudioResourceId DEFAULT_SOUND_FONT_NAME = "MS Basic";
static const AudioResourceAttributes DEFAULT_AUDIO_RESOURCE_ATTRIBUTES = {
    { PLAYBACK_SETUP_DATA_ATTRIBUTE, muse::mpe::GENERIC_SETUP_DATA_STRING },
    { SOUNDFONT_NAME_ATTRIBUTE, String::fromStdString(DEFAULT_SOUND_FONT_NAME) } };

static const AudioResourceMeta DEFAULT_AUDIO_RESOURCE_META
    = { DEFAULT_SOUND_FONT_NAME, AudioResourceType::FluidSoundfont, "Fluid", DEFAULT_AUDIO_RESOURCE_ATTRIBUTES, false /*hasNativeEditor*/ };

namespace muse::audio {
class AudioConfiguration : public IAudioConfiguration, public Injectable
{
    Inject<IGlobalConfiguration> globalConfiguration = { this };
    Inject<io::IFileSystem> fileSystem = { this };

public:
    AudioConfiguration(const modularity::ContextPtr& iocCtx)
        : Injectable(iocCtx) {}

    // void init();

    // std::vector<std::string> availableAudioApiList() const override;

    // std::string currentAudioApi() const override;
    // void setCurrentAudioApi(const std::string& name) override;

    // std::string audioOutputDeviceId() const override;
    // void setAudioOutputDeviceId(const std::string& deviceId) override;
    // async::Notification audioOutputDeviceIdChanged() const override;

    // audioch_t audioChannelsCount() const override;

    // unsigned int driverBufferSize() const override;
    // void setDriverBufferSize(unsigned int size) override;
    // async::Notification driverBufferSizeChanged() const override;

    // msecs_t audioWorkerInterval(const samples_t samples, const sample_rate_t sampleRate) const override;
    // samples_t minSamplesToReserve(RenderMode mode) const override;

    // samples_t samplesToPreallocate() const override;
    // async::Channel<samples_t> samplesToPreallocateChanged() const override;

    // unsigned int sampleRate() const override;
    // void setSampleRate(unsigned int sampleRate) override;
    // async::Notification sampleRateChanged() const override;

    // size_t desiredAudioThreadNumber() const override;
    // size_t minTrackCountForMultithreading() const override;

    // // synthesizers
    // AudioInputParams defaultAudioInputParams() const override;

    // io::paths_t soundFontDirectories() const override;
    // io::paths_t userSoundFontDirectories() const override;
    // void setUserSoundFontDirectories(const io::paths_t& paths) override;
    // async::Channel<io::paths_t> soundFontDirectoriesChanged() const override;

    // bool shouldMeasureInputLag() const override;


    void init()
    {
        int defaultBufferSize = 0;
    #if defined(Q_OS_WASM)
        defaultBufferSize = 8192;
    #else
        defaultBufferSize = 1024;
    #endif
        settings()->setDefaultValue(AUDIO_BUFFER_SIZE_KEY, Val(defaultBufferSize));
        settings()->valueChanged(AUDIO_BUFFER_SIZE_KEY).onReceive(nullptr, [this](const Val&) {
            m_driverBufferSizeChanged.notify();
            updateSamplesToPreallocate();
        });

        settings()->setDefaultValue(AUDIO_API_KEY, Val("Core Audio"));

        settings()->setDefaultValue(AUDIO_OUTPUT_DEVICE_ID_KEY, Val(DEFAULT_DEVICE_ID));
        settings()->valueChanged(AUDIO_OUTPUT_DEVICE_ID_KEY).onReceive(nullptr, [this](const Val&) {
            m_audioOutputDeviceIdChanged.notify();
        });

        settings()->setDefaultValue(AUDIO_SAMPLE_RATE_KEY, Val(44100));
        settings()->setCanBeManuallyEdited(AUDIO_SAMPLE_RATE_KEY, false, Val(44100), Val(192000));
        settings()->valueChanged(AUDIO_SAMPLE_RATE_KEY).onReceive(nullptr, [this](const Val&) {
            m_driverSampleRateChanged.notify();
        });

        settings()->setDefaultValue(USER_SOUNDFONTS_PATHS, Val(globalConfiguration()->userDataPath() + "/SoundFonts"));
        settings()->valueChanged(USER_SOUNDFONTS_PATHS).onReceive(nullptr, [this](const Val&) {
            m_soundFontDirsChanged.send(soundFontDirectories());
        });

        for (const auto& path : userSoundFontDirectories()) {
            fileSystem()->makePath(path);
        }

        settings()->setDefaultValue(AUDIO_MEASURE_INPUT_LAG, Val(false));

        settings()->setDefaultValue(AUDIO_DESIRED_THREAD_NUMBER_KEY, Val(0));

        updateSamplesToPreallocate();
    }

    std::vector<std::string> availableAudioApiList() const override
    {
        std::vector<std::string> names {
            "Core Audio",
            "ALSA Audio",
            "PulseAudio",
            "JACK Audio Server"
        };

        return names;
    }

    std::string currentAudioApi() const override
    {
        return settings()->value(AUDIO_API_KEY).toString();
    }

    void setCurrentAudioApi(const std::string& name) override
    {
        settings()->setSharedValue(AUDIO_API_KEY, Val(name));
    }

    std::string audioOutputDeviceId() const override
    {
        return settings()->value(AUDIO_OUTPUT_DEVICE_ID_KEY).toString();
    }

    void setAudioOutputDeviceId(const std::string& deviceId) override
    {
        settings()->setSharedValue(AUDIO_OUTPUT_DEVICE_ID_KEY, Val(deviceId));
    }

    async::Notification audioOutputDeviceIdChanged() const override
    {
        return m_audioOutputDeviceIdChanged;
    }

    audioch_t audioChannelsCount() const override
    {
        return AUDIO_CHANNELS;
    }

    unsigned int driverBufferSize() const override
    {
        return settings()->value(AUDIO_BUFFER_SIZE_KEY).toInt();
    }

    void setDriverBufferSize(unsigned int size) override
    {
        settings()->setSharedValue(AUDIO_BUFFER_SIZE_KEY, Val(static_cast<int>(size)));
    }

    async::Notification driverBufferSizeChanged() const override
    {
        return m_driverBufferSizeChanged;
    }

    msecs_t audioWorkerInterval(const samples_t samples, const sample_rate_t sampleRate) const override
    {
        msecs_t interval = float(samples) / 4.f / float(sampleRate) * 1000.f;
        interval = std::max(interval, msecs_t(1));

        // Found experementaly on a slow laptop (2 core) running on battery power
        interval = std::min(interval, msecs_t(10));

        return interval;
    }

    samples_t minSamplesToReserve(RenderMode mode) const override
    {
        // Idle: render as little as possible for lower latency
        if (mode == RenderMode::IdleMode) {
            return 128;
        }

        // Active: render more for better quality (rendering is usually much heavier in this scenario)
        return 1024;
    }

    samples_t samplesToPreallocate() const override
    {
        return m_samplesToPreallocate;
    }

    async::Channel<samples_t> samplesToPreallocateChanged() const override
    {
        return m_samplesToPreallocateChanged;
    }

    unsigned int sampleRate() const override
    {
        return settings()->value(AUDIO_SAMPLE_RATE_KEY).toInt();
    }

    void setSampleRate(unsigned int sampleRate) override
    {
        settings()->setSharedValue(AUDIO_SAMPLE_RATE_KEY, Val(static_cast<int>(sampleRate)));
    }

    async::Notification sampleRateChanged() const override
    {
        return m_driverSampleRateChanged;
    }

    size_t desiredAudioThreadNumber() const override
    {
        return settings()->value(AUDIO_DESIRED_THREAD_NUMBER_KEY).toInt();
    }

    size_t minTrackCountForMultithreading() const override
    {
        // Start mutlithreading-processing only when there are more or equal number of tracks
        return 2;
    }

    AudioInputParams defaultAudioInputParams() const override
    {
        AudioInputParams result;
        result.resourceMeta = DEFAULT_AUDIO_RESOURCE_META;

        return result;
    }

    SoundFontPaths soundFontDirectories() const override
    {
        SoundFontPaths paths = userSoundFontDirectories();
        paths.push_back(globalConfiguration()->appDataPath());

        return paths;
    }

    io::paths_t userSoundFontDirectories() const override
    {
        std::string pathsStr = settings()->value(USER_SOUNDFONTS_PATHS).toString();
        return io::pathsFromString(pathsStr);
    }

    void setUserSoundFontDirectories(const muse::io::paths_t& paths) override
    {
        settings()->setSharedValue(USER_SOUNDFONTS_PATHS, Val(io::pathsToString(paths)));
    }

    async::Channel<io::paths_t> soundFontDirectoriesChanged() const override
    {
        return m_soundFontDirsChanged;
    }

    bool shouldMeasureInputLag() const override
    {
        return settings()->value(AUDIO_MEASURE_INPUT_LAG).toBool();
    }

private:
    // void updateSamplesToPreallocate();
    void updateSamplesToPreallocate()
    {
        samples_t minToReserve = minSamplesToReserve(RenderMode::RealTimeMode);
        samples_t driverBufSize = driverBufferSize();
        samples_t newValue = std::max(minToReserve, driverBufSize);

        if (m_samplesToPreallocate != newValue) {
            m_samplesToPreallocate = newValue;
            m_samplesToPreallocateChanged.send(newValue);
        }
    }

    async::Channel<io::paths_t> m_soundFontDirsChanged;
    async::Channel<samples_t> m_samplesToPreallocateChanged;

    async::Notification m_audioOutputDeviceIdChanged;
    async::Notification m_driverBufferSizeChanged;
    async::Notification m_driverSampleRateChanged;

    samples_t m_samplesToPreallocate = 0;
};
}

#endif // MUSE_AUDIO_AUDIOCONFIGURATION_H
