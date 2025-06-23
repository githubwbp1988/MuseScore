/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore BVBA and others
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

#ifndef MUSE_AUDIO_MUSEFXRESOLVER_H
#define MUSE_AUDIO_MUSEFXRESOLVER_H

#include "../../abstractfxresolver.h"

#include "reverb/reverbprocessor.h"

#include "../../audioutils.h"

using namespace muse::audio;

namespace muse::audio::fx {

IFxProcessorPtr createFxProcessor(const AudioFxParams& fxParams)
{
    if (fxParams.resourceMeta.id == MUSE_REVERB_ID) {
        return std::make_shared<ReverbProcessor>(fxParams);
    }

    return nullptr;
}

class MuseFxResolver : public AbstractFxResolver
{
public:
    // AudioResourceMetaList resolveResources() const override;

    AudioResourceMetaList resolveResources() const override
    {
        AudioResourceMetaList result;
        result.emplace_back(makeReverbMeta());

        return result;
    }

private:
    // IFxProcessorPtr createMasterFx(const AudioFxParams& fxParams) const override;
    // IFxProcessorPtr createTrackFx(const TrackId trackId, const AudioFxParams& fxParams) const override;

    IFxProcessorPtr createMasterFx(const AudioFxParams& fxParams) const override
    {
        return createFxProcessor(fxParams);
    }

    IFxProcessorPtr createTrackFx(const TrackId, const AudioFxParams& fxParams) const override
    {
        return createFxProcessor(fxParams);
    }
};
}

#endif // MUSE_AUDIO_MUSEFXRESOLVER_H
