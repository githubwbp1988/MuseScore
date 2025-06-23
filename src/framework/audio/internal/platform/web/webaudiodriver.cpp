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
#include "webaudiodriver.h"
// #include "log.h"

// #include <emscripten.h>
// #include <emscripten/val.h>
// #include <emscripten/bind.h>
// #include <emscripten/html5.h>

// using namespace muse::audio;
// using namespace emscripten;

// namespace muse::audio::web {
// using let = emscripten::val;
// static val context = val::global();
// static IAudioDriver::Spec* format = nullptr;
// static std::vector<float> buffer;

// void audioCallback(emscripten::val event)
// {
//     let sampleBuffer = event["outputBuffer"];
//     int length = sampleBuffer["length"].as<int>();
//     int channels = sampleBuffer["numberOfChannels"].as<int>();
//     int sampleCount = length * channels;
//     int bytes = sampleCount * sizeof(float);

//     if (!format) {
//         return;
//     }
//     if (buffer.size() != sampleCount) {
//         buffer.resize(sampleCount);
//     }
//     format->callback(nullptr, reinterpret_cast<uint8_t*>(buffer.data()), bytes);

//     for (int j = 0; j < channels; ++j) {
//         let channelData = sampleBuffer.call<val>("getChannelData", j);
//         for (int i = 0; i < length; ++i) {
//             channelData.set(i, buffer[i * channels + j]);
//         }
//     }
// }

// EM_BOOL mouseCallback(int eventType, const EmscriptenMouseEvent* e, void* userData)
// {
//     if (context["state"].as<std::string>() == std::string("suspended")) {
//         context.call<val>("resume");
//     }
//     return true;
// }
// }
// EMSCRIPTEN_BINDINGS(events)
// {
//     function("audioCallback", web::audioCallback);
// }

// WebAudioDriver::WebAudioDriver()
// {
// }

// std::string WebAudioDriver::name() const
// {
//     return "MUAUDIO(WEB)";
// }

// bool WebAudioDriver::open(const Spec& spec, Spec* activeSpec)
// {
//     if (isOpened()) {
//         return true;
//     }

//     auto AudioContext = val::global("AudioContext");
//     if (!AudioContext.as<bool>()) {
//         AudioContext = val::global("webkitAudioContext");
//     }

//     web::context = AudioContext.new_();
//     unsigned int sampleRate = web::context["sampleRate"].as<int>();
//     *activeSpec = spec;
//     activeSpec->format = Format::AudioF32;
//     activeSpec->sampleRate = sampleRate;
//     web::format = activeSpec;

//     auto audioNode = web::context.call<val>("createScriptProcessor", spec.samples, 0, spec.channels);
//     if (!audioNode.as<bool>()) {
//         LOGE() << "can't create audio script processor";
//         return false;
//     }
//     web::buffer.resize(spec.samples * spec.channels, 0.f);

//     audioNode.set("onaudioprocess", val::module_property("audioCallback"));
//     audioNode.call<val>("connect", web::context["destination"]);

//     emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, web::mouseCallback);
//     m_opened = true;
//     return m_opened;
// }

// void WebAudioDriver::close()
// {
//     web::context.call<val>("close");
// }

// bool WebAudioDriver::isOpened() const
// {
//     return m_opened;
// }

// // std::string WebAudioDriver::outputDevice() const
// muse::audio::AudioDeviceID WebAudioDriver::outputDevice() const
// {
//     NOT_SUPPORTED;
//     return { "default", muse::trc("audio", "System default") };
// }

// bool WebAudioDriver::selectOutputDevice(const std::string& name)
// {
//     NOT_SUPPORTED;
//     return false;
// }

// // std::vector<std::string> WebAudioDriver::availableOutputDevices() const
// AudioDeviceList WebAudioDriver::availableOutputDevices() const
// {
//     NOT_SUPPORTED;
//     // return { "default" };
//     AudioDeviceList deviceList;
//     deviceList.push_back({ "default", muse::trc("audio", "System default") });
//     return deviceList;
// }

// async::Notification WebAudioDriver::availableOutputDevicesChanged() const
// {
//     NOT_SUPPORTED;
//     return async::Notification();
// }

// void WebAudioDriver::resume()
// {
//     web::context.call<val>("resume");
// }

// void WebAudioDriver::suspend()
// {
//     web::context.call<val>("suspend");
// }

// ///////////////////////////////////////////////////
// void WebAudioDriver::init()
// {
// }

// const muse::audio::WebAudioDriver::Spec& WebAudioDriver::activeSpec() const
// {
//     struct muse::audio::WebAudioDriver::Spec spec;
//     return spec;
// }

// unsigned int WebAudioDriver::outputDeviceBufferSize() const
// {
//     if (!isOpened()) {
//         return 0;
//     }

//     return 0;
// }

// bool WebAudioDriver::setOutputDeviceBufferSize(unsigned int bufferSize)
// {
//     return false;
// }

// async::Notification WebAudioDriver::outputDeviceBufferSizeChanged() const
// {
//     return async::Notification();
// }

// std::vector<unsigned int> WebAudioDriver::availableOutputDeviceBufferSizes() const
// {
//     std::vector<unsigned int> result;

//     return result;
// }

// unsigned int WebAudioDriver::outputDeviceSampleRate() const
// {
//     if (!isOpened()) {
//         return 0;
//     }

//     return 0;
// }

// bool WebAudioDriver::setOutputDeviceSampleRate(unsigned int sampleRate)
// {
//     return false;
// }

// async::Notification WebAudioDriver::outputDeviceSampleRateChanged() const
// {
//     return async::Notification();
// }

// std::vector<unsigned int> WebAudioDriver::availableOutputDeviceSampleRates() const
// {
//     return {
//         44100,
//         48000,
//         88200,
//         96000,
//     };
// }
// ///////////////////////////////////////////////////
