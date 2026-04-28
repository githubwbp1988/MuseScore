/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <QString>
#include <QStringList>

#include "async/asyncable.h"
#include "async/channel.h"

class QLocalServer;

namespace muse::mi {
//! Returns true if a running instance accepted `args`; the caller should exit.
bool activateExistingInstance(const QString& applicationId, const QStringList& args);

class SingleInstanceListener : public async::Asyncable
{
public:
    SingleInstanceListener();
    ~SingleInstanceListener();

    SingleInstanceListener(const SingleInstanceListener&) = delete;
    SingleInstanceListener& operator=(const SingleInstanceListener&) = delete;

    bool start(const QString& applicationId);
    void stop();
    bool isListening() const;

    async::Channel<QStringList> messageReceived() const;

private:
    void onNewConnection();

    QLocalServer* m_server = nullptr;
    async::Channel<QStringList> m_messageReceived;
};
}
