export module Graphics.Resources.Loading.Queue;
import std;

namespace Graphics
{
export class ResourceLoadJob
{
public:
    virtual ~ResourceLoadJob() = default;
    virtual bool Prepare() = 0;
    virtual bool Decode() = 0;
    virtual void Complete(bool decoded) noexcept = 0;
    // Cooperative cancellation; no waiting or destruction of worker-owned data.
    virtual void Cancel() noexcept {}
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

    bool Start()
    {
        std::lock_guard lock(m_mutex);
        if (m_worker.joinable()) return m_accepting && m_owner==std::this_thread::get_id();
        m_owner=std::this_thread::get_id();
        m_stopping=false;
        m_accepting=true;
        m_worker=std::thread([this] { Worker(); });
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
        bool owner=false;
        {
            std::lock_guard lock(m_mutex);
            if (!m_accepting) return false;
            owner=m_owner==std::this_thread::get_id();
            auto [position,inserted]=m_entries.try_emplace(source);
            if (inserted) {
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
        if (owner) Process(entry,priority==ResourceLoadPriority::Immediate);
        return true;
    }

    void Update(void (*progress)() = nullptr)
    {
        Update_Until(std::chrono::steady_clock::time_point::max(), (std::numeric_limits<std::size_t>::max)(), progress);
    }

    // A frame retires a bounded number of jobs and checks its deadline between
    // jobs. Prepare/Complete must themselves be short owner-thread operations.
    std::size_t Update_Until(std::chrono::steady_clock::time_point deadline, std::size_t maximum_jobs,
        void (*progress)() = nullptr)
    {
        if (!Is_Owner_Thread()) return 0;
        auto last_progress=std::chrono::steady_clock::now();
        std::size_t processed=0;
        while (processed<maximum_jobs && std::chrono::steady_clock::now()<deadline) {
            std::shared_ptr<Entry> entry;
            {
                std::lock_guard lock(m_mutex);
                if (!m_pending.empty()) { entry=m_pending.front(); m_pending.pop_front(); }
                else if (!m_ready.empty()) { entry=m_ready.front(); m_ready.pop_front(); }
                else break;
            }
            Process(entry,false);
            ++processed;
            const auto now=std::chrono::steady_clock::now();
            if (progress && now-last_progress>std::chrono::milliseconds(20)) {
                progress(); last_progress=now;
            }
        }
        return processed;
    }

    bool Cancel(const std::shared_ptr<const ResourceLoadSource>& source)
    {
        if (!Is_Owner_Thread()) return false;
        ResourceLoadJob* job=nullptr;
        {
            std::lock_guard lock(m_mutex);
            const auto found=m_entries.find(source);
            if (found==m_entries.end()) return false;
            auto& entry=*found->second;
            entry.cancelled=true;
            job=entry.job.get();
            if (entry.phase==Phase::Queued || entry.phase==Phase::Prepared) {
                entry.phase=Phase::Ready;
                m_ready.push_back(found->second);
            }
        }
        // Only the owner publishes/destroys jobs, so this pointer stays alive.
        if (job) job->Cancel();
        m_changed.notify_all();
        return true;
    }

    bool Drain(void (*progress)() = nullptr)
    {
        if (!Is_Owner_Thread()) return false;
        for (;;) {
            Update(progress);
            std::unique_lock lock(m_mutex);
            if (m_entries.empty()) return true;
            m_changed.wait(lock,[this] { return !m_pending.empty() || !m_ready.empty() || m_entries.empty(); });
        }
    }

    bool Shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            if (!m_worker.joinable()) return true;
            if (m_owner!=std::this_thread::get_id()) return false;
            m_accepting=false;
        }
        Drain();
        {
            std::lock_guard lock(m_mutex);
            m_stopping=true;
        }
        m_changed.notify_all();
        m_worker.join();
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
        bool cancelled=false;
    };

    void Process(const std::shared_ptr<Entry>& entry, bool immediate)
    {
        bool prepare=false;
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
                if (!immediate) m_decode.push_back(entry);
            } else entry->phase=Phase::Ready;
        }
        m_changed.notify_all();

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
        if (entry->job) entry->job->Complete(entry->decoded && !entry->cancelled);
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
                m_changed.wait(lock,[this] { return m_stopping || !m_decode.empty(); });
                if (m_stopping) return;
                entry=m_decode.back(); m_decode.pop_back();
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
    std::thread m_worker;
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
