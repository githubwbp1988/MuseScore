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

#pragma once

#include "global/modularity/ioc.h"
#include "global/iprocess.h"
#include "global/iglobalconfiguration.h"
#include "global/iinteractive.h"
#include "global/async/asyncable.h"

#include "../iregisteraudiopluginsscenario.h"
#include "../iknownaudiopluginsregister.h"
#include "../iaudiopluginsscannerregister.h"
#include "../iaudiopluginmetareaderregister.h"

#include <QCoreApplication>

#include "global/translation.h"

#include "audiopluginserrors.h"
#include "audiopluginsutils.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;

namespace muse::audioplugins {
class RegisterAudioPluginsScenario : public IRegisterAudioPluginsScenario, public Injectable, public async::Asyncable
{
public:
    Inject<IKnownAudioPluginsRegister> knownPluginsRegister = { this };
    Inject<IAudioPluginsScannerRegister> scannerRegister = { this };
    Inject<IAudioPluginMetaReaderRegister> metaReaderRegister = { this };
    Inject<IGlobalConfiguration> globalConfiguration = { this };
    Inject<IInteractive> interactive = { this };
    Inject<IProcess> process = { this };

public:
    RegisterAudioPluginsScenario(const modularity::ContextPtr& iocCtx)
        : Injectable(iocCtx) {}

    // void init();

    // Ret registerNewPlugins() override;
    // Ret registerPlugin(const io::path_t& pluginPath) override;
    // Ret registerFailedPlugin(const io::path_t& pluginPath, int failCode) override;

    void init()
    {
        TRACEFUNC;

        m_progress.canceled().onNotify(this, [this]() {
            m_aborted = true;
        });

        Ret ret = knownPluginsRegister()->load();
        if (!ret) {
            LOGE() << ret.toString();
        }
    }

    Ret registerNewPlugins() override
    {
        TRACEFUNC;

        io::paths_t newPluginPaths;

        for (const IAudioPluginsScannerPtr& scanner : scannerRegister()->scanners()) {
            io::paths_t paths = scanner->scanPlugins();

            for (const io::path_t& path : paths) {
                if (!knownPluginsRegister()->exists(path)) {
                    newPluginPaths.push_back(path);
                }
            }
        }

        if (newPluginPaths.empty()) {
            return muse::make_ok();
        }

        processPluginsRegistration(newPluginPaths);

        Ret ret = knownPluginsRegister()->load();
        return ret;
    }

    Ret registerPlugin(const muse::io::path_t& pluginPath) override
    {
        TRACEFUNC;

        IF_ASSERT_FAILED(!pluginPath.empty()) {
            return false;
        }

        IAudioPluginMetaReaderPtr reader = metaReader(pluginPath);
        if (!reader) {
            return make_ret(Err::UnknownPluginType);
        }

        RetVal<AudioResourceMetaList> metaList = reader->readMeta(pluginPath);
        if (!metaList.ret) {
            LOGE() << metaList.ret.toString();
            return metaList.ret;
        }

        for (const AudioResourceMeta& meta : metaList.val) {
            AudioPluginInfo info;
            info.type = audioPluginTypeFromCategoriesString(meta.attributeVal(audio::CATEGORIES_ATTRIBUTE));
            info.meta = meta;
            info.path = pluginPath;
            info.enabled = true;

            Ret ret = knownPluginsRegister()->registerPlugin(info);
            if (!ret) {
                return ret;
            }
        }

        return muse::make_ok();
    }

    Ret registerFailedPlugin(const muse::io::path_t& pluginPath, int failCode) override
    {
        TRACEFUNC;

        IF_ASSERT_FAILED(!pluginPath.empty()) {
            return false;
        }

        AudioPluginInfo info;
        info.meta.id = io::completeBasename(pluginPath).toStdString();

        info.meta.type = metaType(pluginPath);
        info.path = pluginPath;
        info.enabled = false;
        info.errorCode = failCode;

        Ret ret = knownPluginsRegister()->registerPlugin(info);
        return ret;
    }

private:
    // void processPluginsRegistration(const io::paths_t& pluginPaths);
    // IAudioPluginMetaReaderPtr metaReader(const io::path_t& pluginPath) const;
    // audio::AudioResourceType metaType(const io::path_t& pluginPath) const;

    void processPluginsRegistration(const muse::io::paths_t& pluginPaths) 
    {
        interactive()->showProgress(muse::trc("audio", "Scanning audio plugins"), &m_progress);

        m_aborted = false;
        m_progress.start();

        std::string appPath = globalConfiguration()->appBinPath().toStdString();
        int64_t pluginCount = static_cast<int64_t>(pluginPaths.size());

        for (int64_t i = 0; i < pluginCount; ++i) {
            if (m_aborted) {
                return;
            }

            const io::path_t& pluginPath = pluginPaths[i];
            std::string pluginPathStr = pluginPath.toStdString();

            m_progress.progress(i, pluginCount, io::filename(pluginPath).toStdString());
            qApp->processEvents();

            LOGD() << "--register-audio-plugin " << pluginPathStr;
            int code = process()->execute(appPath, { "--register-audio-plugin", pluginPathStr });
            if (code != 0) {
                code = process()->execute(appPath, { "--register-failed-audio-plugin", pluginPathStr, "--", std::to_string(code) });
            }

            if (code != 0) {
                LOGE() << "Could not register plugin: " << pluginPathStr << "\n error code: " << code;
            }
        }

        m_progress.finish(muse::make_ok());
    }
    IAudioPluginMetaReaderPtr metaReader(const muse::io::path_t& pluginPath) const
    {
        for (const IAudioPluginMetaReaderPtr& reader : metaReaderRegister()->readers()) {
            if (reader->canReadMeta(pluginPath)) {
                return reader;
            }
        }

        return nullptr;
    }

    muse::audio::AudioResourceType metaType(const muse::io::path_t& pluginPath) const
    {
        const IAudioPluginMetaReaderPtr reader = metaReader(pluginPath);
        return reader ? reader->metaType() : audio::AudioResourceType::Undefined;
    }

    Progress m_progress;
    bool m_aborted = false;
};
}
