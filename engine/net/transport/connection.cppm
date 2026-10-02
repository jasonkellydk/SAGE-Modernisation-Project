export module engine.net.transport.connection;
import std;

// A reliable, ordered message pipe between a client and the server. The
// lockstep layers only see Connection; loopback (in-process, used by single
// player and local hosts) and network transports are adapters.
export namespace engine::net
{
class Connection
{
public:
	virtual ~Connection() = default;
	virtual void Send(std::vector<std::byte> message) = 0;
	virtual std::optional<std::vector<std::byte>> Receive() = 0;
	virtual bool Open() const noexcept = 0;
	// Ends the link for both sides; what was already sent can still be received.
	virtual void Close() noexcept = 0;
};

namespace detail
{
// One direction of a loopback link: a thread-safe message queue.
class Pipe
{
public:
	void Push(std::vector<std::byte> message)
	{
		const std::lock_guard lock(m_mutex);
		m_messages.push_back(std::move(message));
	}

	std::optional<std::vector<std::byte>> Pop()
	{
		const std::lock_guard lock(m_mutex);
		if (m_messages.empty())
			return std::nullopt;
		std::vector<std::byte> message = std::move(m_messages.front());
		m_messages.pop_front();
		return message;
	}

private:
	std::mutex m_mutex;
	std::deque<std::vector<std::byte>> m_messages;
};

class LoopbackEnd final : public Connection
{
public:
	LoopbackEnd(std::shared_ptr<Pipe> out, std::shared_ptr<Pipe> in, std::shared_ptr<std::atomic<bool>> open) :
		m_out(std::move(out)), m_in(std::move(in)), m_open(std::move(open))
	{
	}
	// Like a socket going away: the other side sees the link closed.
	~LoopbackEnd() override { Close(); }
	LoopbackEnd(const LoopbackEnd &) = delete;
	LoopbackEnd &operator=(const LoopbackEnd &) = delete;

	void Send(std::vector<std::byte> message) override
	{
		if (Open())
			m_out->Push(std::move(message));
	}
	std::optional<std::vector<std::byte>> Receive() override { return m_in->Pop(); }
	bool Open() const noexcept override { return m_open->load(std::memory_order_acquire); }
	void Close() noexcept override { m_open->store(false, std::memory_order_release); }

private:
	std::shared_ptr<Pipe> m_out;
	std::shared_ptr<Pipe> m_in;
	std::shared_ptr<std::atomic<bool>> m_open;
};
}

// Two connected ends in this process (client end, server end). Messages are
// still serialized bytes, so the local game takes exactly the network path.
inline std::pair<std::unique_ptr<Connection>, std::unique_ptr<Connection>> MakeLoopback()
{
	auto toServer = std::make_shared<detail::Pipe>();
	auto toClient = std::make_shared<detail::Pipe>();
	auto open = std::make_shared<std::atomic<bool>>(true);
	return {std::make_unique<detail::LoopbackEnd>(toServer, toClient, open), std::make_unique<detail::LoopbackEnd>(toClient, toServer, open)};
}
}
