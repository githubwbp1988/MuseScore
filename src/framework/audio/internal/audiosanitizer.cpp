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
#include "audiosanitizer.h"

#include <thread>

#include "containers.h"

using namespace muse::audio;

// static std::thread::id s_as_mainThreadID;
// static std::thread::id s_as_workerThreadID;
// static std::set<std::thread::id> s_mixerThreadIdSet;

static AudioSanitizer::thread_id_type s_as_mainThreadID{};
static AudioSanitizer::thread_id_type s_as_workerThreadID{};
static std::set<AudioSanitizer::thread_id_type> s_mixerThreadIdSet;

void AudioSanitizer::setupMainThread()
{
    // s_as_mainThreadID = std::this_thread::get_id();
    s_as_mainThreadID = AudioSanitizer::THREAD_MAIN;
    s_currentThreadId = AudioSanitizer::THREAD_MAIN; // Ensure the current thread is marked as the main thread
}

// std::thread::id AudioSanitizer::mainThread()
AudioSanitizer::thread_id_type AudioSanitizer::mainThread()
{
    return s_as_mainThreadID;
}

bool AudioSanitizer::isMainThread()
{
    // return std::this_thread::get_id() == s_as_mainThreadID;
    return emscripten_is_main_browser_thread() || (getCurrentThreadId() == s_as_mainThreadID);
}

void AudioSanitizer::setupWorkerThread()
{
    // s_as_workerThreadID = std::this_thread::get_id();

    s_as_workerThreadID = getCurrentThreadId();
    // If there is no ID yet, allocate one
    if (s_as_workerThreadID == 0) {
        s_as_workerThreadID = AudioSanitizer::THREAD_WORKER;
        s_currentThreadId = AudioSanitizer::THREAD_WORKER;
    }
}

// void AudioSanitizer::setMixerThreads(const std::set<std::thread::id>& threadIdSet)
void AudioSanitizer::setMixerThreads(const std::set<AudioSanitizer::thread_id_type>& threadIdSet)
{
    s_mixerThreadIdSet = threadIdSet;
}

// std::thread::id AudioSanitizer::workerThread()
AudioSanitizer::thread_id_type AudioSanitizer::workerThread()
{
    return s_as_workerThreadID;
}

bool AudioSanitizer::isWorkerThread()
{
    // std::thread::id id = std::this_thread::get_id();

    // return id == s_as_workerThreadID || muse::contains(s_mixerThreadIdSet, id);

    AudioSanitizer::thread_id_type id = getCurrentThreadId();
        
    // Check if it is a worker thread
    if (id == s_as_workerThreadID) {
        return true;
    }
    
    // Check if it is in the mixer thread set
    return s_mixerThreadIdSet.find(id) != s_mixerThreadIdSet.end();
}
