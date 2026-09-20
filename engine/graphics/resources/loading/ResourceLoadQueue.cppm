module;
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

export module Graphics.Resources.Loading.Queue;

namespace Graphics
{
export class ResourceLoadJob
{
public:
    virtual ~ResourceLoadJob() = default;
    virtual bool Prepare() = 0;
    virtual bool Decode() = 0;
    virtual void Complete(bool decoded) noexcept = 0;
};

// The source owner and its factory are destroyed on the queue's owning thread.
// Requests retain only a weak source until that thread creates the job. The job
// then owns everything needed by decoding and publication, including references
// that must be acquired and released on the owning thread.
export struct ResourceLoadSource final
{
    using Factory = std::function<std::unique_ptr<ResourceLoadJob>()>;
    explicit ResourceLoadSource(Factory factory) : create(std::move(factory)) {}
    const Factory create;
};

export enum class ResourceLoadPriority { Background, Immediate };

export class ResourceLoadQueue final
{
public:
    ResourceLoadQueue() = default;
    ResourceLoadQueue(const ResourceLoadQueue&) = delete;
    ResourceLoadQueue& operator=(const ResourceLoadQueue&) = delete;
    ~ResourceLoadQueue() { Shutdown(); }

    bool Start(unsigned workers = 1)
    {
        std::lock_guard lock(m_mutex);
        if (!m_workers.empty()) return m_accepting && m_owner==std::this_thread::get_id();
        m_owner=std::this_thread::get_id();
        m_stopping=false;
        m_accepting=true;
        for (unsigned i=0;i<(workers ? workers : 1);++i)
            m_workers.emplace_back([this] { Worker(); });
        return true;
    }

    bool Is_Owner_Thread() const
    {
        std::lock_guard lock(m_mutex);
        return m_owner==std::this_thread::get_id();
    }

    bool Pending(const std::shared_ptr<const ResourceLoadSource>& source) const
    {
        std::lock_guard lock(m_mutex);
        return m_entries.contains(source);
    }

    bool Request(const std::shared_ptr<const ResourceLoadSource>& source, ResourceLoadPriority priority)
    {
        if (!source) return false;
        std::shared_ptr<Entry> entry;
        bool owner=false,created=false;
        {
            std::lock_guard lock(m_mutex);
            if (!m_accepting) return false;
            owner=m_owner==std::this_thread::get_id();
            auto [position,inserted]=m_entries.try_emplace(source);
            if (inserted) {
                created=true;
                position->second=std::make_shared<Entry>();
                position->second->source=source;
                m_pending.push_back(position->second);
            }
            entry=position->second;
            if (priority==ResourceLoadPriority::Immediate && !entry->immediate) {
                entry->immediate=true;
                if (!inserted && !owner) m_pending.push_back(entry);
            }
        }
        m_changed.notify_all();
        if (owner && (created || priority==ResourceLoadPriority::Immediate))
            Process(entry,priority==ResourceLoadPriority::Immediate);
        return true;
    }

    // Limit publication work during rendering. Drain explicitly opts out while
    // pumping progress even when a worker has not completed another image yet.
    void Update(void (*progress)() = nullptr,
        std::chrono::steady_clock::duration budget = std::chrono::milliseconds(2))
    {
        if (!Is_Owner_Thread()) return;
        auto last_progress=std::chrono::steady_clock::now();
        const auto deadline=last_progress+budget;
        for (;;) {
            std::shared_ptr<Entry> entry;
            {
                std::lock_guard lock(m_mutex);
                if (!m_pending.empty()) { entry=m_pending.front(); m_pending.pop_front(); }
                else if (!m_ready.empty()) { entry=m_ready.front(); m_ready.pop_front(); }
                else break;
            }
            const auto now=std::chrono::steady_clock::now();
            if (progress && now-last_progress>std::chrono::milliseconds(20)) {
                progress(); last_progress=now;
            }
            Process(entry,false);
            if (std::chrono::steady_clock::now()>=deadline) break;
        }
    }

    bool Drain(void (*progress)() = nullptr)
    {
        if (!Is_Owner_Thread()) return false;
        for (;;) {
            Update(progress,std::chrono::seconds(1));
            std::unique_lock lock(m_mutex);
            if (m_entries.empty()) return true;
            m_changed.wait_for(lock,std::chrono::milliseconds(20),
                [this] { return !m_pending.empty() || !m_ready.empty() || m_entries.empty(); });
            lock.unlock();
            if (progress) progress();
        }
    }

    bool Shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_workers.empty()) return true;
            if (m_owner!=std::this_thread::get_id()) return false;
            m_accepting=false;
        }
        Drain();
        {
            std::lock_guard lock(m_mutex);
            m_stopping=true;
        }
        m_changed.notify_all();
        for (auto& worker : m_workers) worker.join();
        m_workers.clear();
        std::lock_guard lock(m_mutex);
        m_pending.clear(); m_decode.clear(); m_ready.clear();
        m_owner={};
        return true;
    }

private:
    enum class Phase { Queued, Preparing, Prepared, Decoding, Ready, Completing, Complete };
    struct Entry final
    {
        std::weak_ptr<const ResourceLoadSource> source;
        std::unique_ptr<ResourceLoadJob> job;
        Phase phase=Phase::Queued;
        bool immediate=false;
        bool decoded=false;
    };

    void Process(const std::shared_ptr<Entry>& entry, bool immediate)
    {
        bool prepare=false,queued_decode=false;
        {
            std::lock_guard lock(m_mutex);
            immediate=immediate || entry->immediate;
            if (entry->phase==Phase::Queued) { entry->phase=Phase::Preparing; prepare=true; }
        }
        if (prepare) {
            bool prepared=false;
            try {
                if (const auto source=entry->source.lock()) {
                    entry->job=source->create();
                    if (entry->job) prepared=entry->job->Prepare();
                }
            } catch (...) { prepared=false; }
            std::lock_guard lock(m_mutex);
            if (prepared) {
                entry->phase=Phase::Prepared;
                if (!immediate) {m_decode.push_back(entry);queued_decode=true;}
            } else entry->phase=Phase::Ready;
        }
        m_changed.notify_all();

        // A fast worker may finish before Request returns. Publication still
        // belongs to the next budgeted Update, never this unbudgeted request.
        if(queued_decode) return;

        bool decode=false;
        {
            std::unique_lock lock(m_mutex);
            if (entry->phase==Phase::Prepared && immediate) {
                entry->phase=Phase::Decoding; decode=true;
            } else if (entry->phase==Phase::Decoding && immediate) {
                m_changed.wait(lock,[&] { return entry->phase!=Phase::Decoding; });
            }
        }
        if (decode) {
            bool decoded=false;
            try { decoded=entry->job->Decode(); } catch (...) {}
            std::lock_guard lock(m_mutex);
            entry->decoded=decoded;
            entry->phase=Phase::Ready;
        }
        {
            std::lock_guard lock(m_mutex);
            if (entry->phase!=Phase::Ready) return;
            entry->phase=Phase::Completing;
        }
        if (entry->job) entry->job->Complete(entry->decoded);
        entry->job.reset();
        {
            std::lock_guard lock(m_mutex);
            entry->phase=Phase::Complete;
            m_entries.erase(entry->source);
        }
        m_changed.notify_all();
    }

    void Worker()
    {
        for (;;) {
            std::shared_ptr<Entry> entry;
            {
                std::unique_lock lock(m_mutex);
                // Do not decode an entire map into RAM while the owner is busy.
                m_changed.wait(lock,[this] { return m_stopping || (!m_decode.empty() && m_ready.size()<32); });
                if (m_stopping) return;
                entry=m_decode.front(); m_decode.pop_front();
                if (entry->phase!=Phase::Prepared) continue;
                entry->phase=Phase::Decoding;
            }
            bool decoded=false;
            try { decoded=entry->job->Decode(); } catch (...) {}
            {
                std::lock_guard lock(m_mutex);
                entry->decoded=decoded;
                entry->phase=Phase::Ready;
                m_ready.push_back(entry);
            }
            m_changed.notify_all();
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::thread> m_workers;
    std::thread::id m_owner;
    bool m_accepting=false;
    bool m_stopping=false;
    std::map<std::weak_ptr<const ResourceLoadSource>,std::shared_ptr<Entry>,
        std::owner_less<std::weak_ptr<const ResourceLoadSource>>> m_entries;
    std::deque<std::shared_ptr<Entry>> m_pending, m_decode, m_ready;
};

export ResourceLoadQueue& Get_Resource_Load_Queue()
{
    static ResourceLoadQueue queue;
    return queue;
}
}
