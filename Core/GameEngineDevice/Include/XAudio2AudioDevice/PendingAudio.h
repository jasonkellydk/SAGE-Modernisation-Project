#pragma once

#include "AudioEvent.h"
#include <chrono>
#include <future>
#include <list>
#include <string>
#include <utility>

// Presentation requests own their filename and controls while decoding. The
// caller retains the decode jobs independently, so cancellation never joins a
// worker on the game thread.
class PendingAudioQueue {
public:
    struct Entry {
        AudioHandle handle;
        AudioEvent event;
        std::string filename;
        std::shared_future<bool> decoded;
        bool paused = false;
        bool succeeded = false;
        AudioCompletionCallback callback = nullptr;
        void* callbackData = nullptr;
    };
    void Add(AudioHandle handle, const AudioEvent& event, std::shared_future<bool> decoded) {
        entries.push_back({handle,event,event.filename,std::move(decoded)});
        entries.back().event.filename = nullptr;
    }
    Entry* Find(AudioHandle handle) {
        for (auto& entry : entries) if (entry.handle==handle) return &entry;
        return nullptr;
    }
    const Entry* Find(AudioHandle handle) const {
        for (const auto& entry : entries) if (entry.handle==handle) return &entry;
        return nullptr;
    }
    bool Remove(AudioHandle handle) {
        for (auto it=entries.begin(); it!=entries.end(); ++it) if (it->handle==handle) {
            entries.erase(it);
            return true;
        }
        return false;
    }
    void Clear() { entries.clear(); }
    std::list<Entry> TakeReady() {
        std::list<Entry> result;
        for (auto it=entries.begin(); it!=entries.end();) {
            if (it->paused || it->decoded.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
                ++it;
                continue;
            }
            try { it->succeeded = it->decoded.get(); }
            catch (...) { it->succeeded = false; }
            const auto ready=it++;
            result.splice(result.end(),entries,ready);
        }
        return result;
    }
private:
    std::list<Entry> entries;
};
