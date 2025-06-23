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

namespace muse::audio {
class WebAudioDriver : public IAudioDriver
{
public:
    WebAudioDriver();

    std::string name() const override;
    bool open(const Spec& spec, Spec* activeSpec) override;
    void close() override;
    bool isOpened() const override;
    void resume() override;
    void suspend() override;

    // std::string outputDevice() const override;
    muse::audio::AudioDeviceID outputDevice() const override;
    bool selectOutputDevice(const std::string& name) override;
    // std::vector<std::string> availableOutputDevices() const override;
    AudioDeviceList availableOutputDevices() const override;
    async::Notification availableOutputDevicesChanged() const override;


    void init() override;
    const muse::audio::WebAudioDriver::Spec& activeSpec() const override;
    bool resetToDefaultOutputDevice() override;
    async::Notification outputDeviceChanged() const override;

    unsigned int outputDeviceBufferSize() const override;
    bool setOutputDeviceBufferSize(unsigned int bufferSize) override;
    async::Notification outputDeviceBufferSizeChanged() const override;

    std::vector<unsigned int> availableOutputDeviceBufferSizes() const override;

    unsigned int outputDeviceSampleRate() const override;
    bool setOutputDeviceSampleRate(unsigned int sampleRate) override;
    async::Notification outputDeviceSampleRateChanged() const override;

    std::vector<unsigned int> availableOutputDeviceSampleRates() const override;

private:
    bool m_opened = false;
};
}

#endif // MUSE_AUDIO_WEBAUDIODRIVER_H
