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
#ifndef MUSE_AUDIO_WEBAUDIODRIVER_H
#define MUSE_AUDIO_WEBAUDIODRIVER_H

#include "../../../iaudiodriver.h"

#include "log.h"

#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/html5.h>

using namespace emscripten;

namespace muse::audio::web {
using let = emscripten::val;
static val context = val::global();
static IAudioDriver::Spec* format = nullptr;
static std::vector<float> buffer;

void audioCallback(emscripten::val event)
{
    let sampleBuffer = event["outputBuffer"];
    int length = sampleBuffer["length"].as<int>();
    int channels = sampleBuffer["numberOfChannels"].as<int>();
    int sampleCount = length * channels;
    int bytes = sampleCount * sizeof(float);

    if (!format) {
        return;
    }
    if (buffer.size() != sampleCount) {
        buffer.resize(sampleCount);
    }
    format->callback(nullptr, reinterpret_cast<uint8_t*>(buffer.data()), bytes);

    for (int j = 0; j < channels; ++j) {
        let channelData = sampleBuffer.call<val>("getChannelData", j);
        for (int i = 0; i < length; ++i) {
            channelData.set(i, buffer[i * channels + j]);
        }
    }
}

EM_BOOL mouseCallback(int eventType, const EmscriptenMouseEvent* e, void* userData)
{
    if (context["state"].as<std::string>() == std::string("suspended")) {
        context.call<val>("resume");
    }
    return true;
}
}
EMSCRIPTEN_BINDINGS(events)
{
    function("audioCallback", web::audioCallback);
}

namespace muse::audio {
class WebAudioDriver : public IAudioDriver
{
public:
    // WebAudioDriver();

    // std::string name() const override;
    // bool open(const Spec& spec, Spec* activeSpec) override;
    // void close() override;
    // bool isOpened() const override;
    // void resume() override;
    // void suspend() override;

    // // std::string outputDevice() const override;
    // muse::audio::AudioDeviceID outputDevice() const override;
    // bool selectOutputDevice(const std::string& name) override;
    // // std::vector<std::string> availableOutputDevices() const override;
    // AudioDeviceList availableOutputDevices() const override;
    // async::Notification availableOutputDevicesChanged() const override;


    // void init() override;
    // const muse::audio::WebAudioDriver::Spec& activeSpec() const override;
    // bool resetToDefaultOutputDevice() override;
    // async::Notification outputDeviceChanged() const override;

    // unsigned int outputDeviceBufferSize() const override;
    // bool setOutputDeviceBufferSize(unsigned int bufferSize) override;
    // async::Notification outputDeviceBufferSizeChanged() const override;

    // std::vector<unsigned int> availableOutputDeviceBufferSizes() const override;

    // unsigned int outputDeviceSampleRate() const override;
    // bool setOutputDeviceSampleRate(unsigned int sampleRate) override;
    // async::Notification outputDeviceSampleRateChanged() const override;

    // std::vector<unsigned int> availableOutputDeviceSampleRates() const override;

    WebAudioDriver()
    {
    }

    std::string name() const override
    {
        return "MUAUDIO(WEB)";
    }

    bool open(const Spec& spec, Spec* activeSpec) override
    {
        if (isOpened()) {
            return true;
        }

        auto AudioContext = val::global("AudioContext");
        if (!AudioContext.as<bool>()) {
            AudioContext = val::global("webkitAudioContext");
        }

        web::context = AudioContext.new_();
        unsigned int sampleRate = web::context["sampleRate"].as<int>();
        *activeSpec = spec;
        activeSpec->format = Format::AudioF32;
        activeSpec->sampleRate = sampleRate;
        web::format = activeSpec;

        auto audioNode = web::context.call<val>("createScriptProcessor", spec.samples, 0, spec.channels);
        if (!audioNode.as<bool>()) {
            LOGE() << "can't create audio script processor";
            return false;
        }
        web::buffer.resize(spec.samples * spec.channels, 0.f);

        audioNode.set("onaudioprocess", val::module_property("audioCallback"));
        audioNode.call<val>("connect", web::context["destination"]);

        emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, web::mouseCallback);
        m_opened = true;
        return m_opened;
    }

    void close() override
    {
        web::context.call<val>("close");
    }

    bool isOpened() const override
    {
        return m_opened;
    }

    muse::audio::AudioDeviceID outputDevice() const override
    {
        NOT_SUPPORTED;
        return "default";
    }

    bool selectOutputDevice(const std::string& name) override
    {
        NOT_SUPPORTED;
        return false;
    }

    AudioDeviceList availableOutputDevices() const override
    {
        NOT_SUPPORTED;
        AudioDeviceList deviceList;
        deviceList.push_back({ "default", muse::trc("audio", "System default") });
        return deviceList;
    }

    async::Notification availableOutputDevicesChanged() const override
    {
        NOT_SUPPORTED;
        return async::Notification();
    }

    void resume() override
    {
        web::context.call<val>("resume");
    }

    void suspend() override
    {
        web::context.call<val>("suspend");
    }

    ///////////////////////////////////////////////////
    void init() override
    {
    }

    const muse::audio::WebAudioDriver::Spec& activeSpec() const override
    {
        struct muse::audio::WebAudioDriver::Spec spec;
        return spec;
    }

    bool resetToDefaultOutputDevice() override {
        return false;
    }
    async::Notification outputDeviceChanged() const override {
        return async::Notification();
    }

    unsigned int outputDeviceBufferSize() const override
    {
        if (!isOpened()) {
            return 0;
        }

        return 0;
    }

    bool setOutputDeviceBufferSize(unsigned int bufferSize) override
    {
        return false;
    }

    async::Notification outputDeviceBufferSizeChanged() const override
    {
        return async::Notification();
    }

    std::vector<unsigned int> availableOutputDeviceBufferSizes() const override
    {
        std::vector<unsigned int> result;

        return result;
    }

    unsigned int outputDeviceSampleRate() const override
    {
        if (!isOpened()) {
            return 0;
        }

        return 0;
    }

    bool setOutputDeviceSampleRate(unsigned int sampleRate) override
    {
        return false;
    }

    async::Notification outputDeviceSampleRateChanged() const override
    {
        return async::Notification();
    }

    std::vector<unsigned int> availableOutputDeviceSampleRates() const override
    {
        return {
            44100,
            48000,
            88200,
            96000,
        };
    }
    ///////////////////////////////////////////////////

private:
    bool m_opened = false;
};
}

#endif // MUSE_AUDIO_WEBAUDIODRIVER_H
