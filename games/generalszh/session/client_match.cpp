module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <chrono>
#include <optional>
#include <utility>

module games.generalszh.session.session_view;

import games.generalszh.session.lockstep_session;
import engine.net.lockstep.local_match;
import engine.net.lockstep.lockstep_server;
import engine.net.transport.reliable_udp;

namespace generalszh::session
{
namespace
{
class LocalClientMatch final : public ClientMatch
{
public:
	LocalClientMatch(const engine::level::Level &level, const content::GameContent &content, const SessionOptions &options) :
		m_simulation(level, content, options), m_match(options.seed)
	{
	}
	// Resumed: the relay goes on from the checkpoint's tick.
	LocalClientMatch(const engine::level::Level &level, const content::GameContent &content, const SessionOptions &options,
		std::span<const std::byte> checkpoint) :
		m_simulation(level, content, options, checkpoint), m_match(options.seed, m_simulation.Valid() ? m_simulation.CurrentTick() : 0)
	{
	}
	bool Valid() const noexcept { return m_simulation.Valid(); }

	bool Advance() override { return m_match.Advance(m_simulation) != 0; }
	std::uint64_t Generation() const noexcept override { return m_simulation.Generation(); }
	SessionView &View() noexcept override { return m_simulation.Game(); }
	void Submit(const commands::GameCommand &command) override
	{
		engine::net::CommandEnvelope envelope = commands::Encode(command);
		m_match.Submit(envelope.type, std::move(envelope.payload));
	}
	std::vector<std::byte> Checkpoint() const override { return m_simulation.SaveCheckpoint(); }

private:
	LockstepSession m_simulation;
	engine::net::LocalMatch m_match;
};
}

// A LAN game: the lockstep relay's messages over reliable UDP. The hosting machine runs the relay (its own player over
// loopback, the others as they connect); every machine claims its seat and plays the ticks the relay releases.
class NetworkClientMatch final : public ClientMatch
{
public:
	NetworkClientMatch(const engine::level::Level &level, const content::GameContent &content, const SessionOptions &options, const NetworkMatchOptions &network) :
		m_simulation(level, content, options), m_socket(engine::net::UdpSocket::Open(network.hostAddress ? 0 : network.port, false))
	{
		if (!m_socket)
			return;
		m_transport = std::make_unique<engine::net::ReliableUdpHost>(engine::net::LinkOf(*m_socket));
		std::unique_ptr<engine::net::Connection> connection;
		if (network.hostAddress)
			connection = m_transport->Connect({*network.hostAddress, network.port});
		else
		{
			engine::net::ServerOptions relay;
			relay.players = network.players;
			relay.seed = options.seed;
			relay.inputDelay = network.inputDelay;
			m_server = std::make_unique<engine::net::LockstepServer>(relay);
			m_transport->Listen(true);
			auto [client, host] = engine::net::MakeLoopback();
			m_server->Accept(std::move(host));
			connection = std::move(client);
		}
		m_client = std::make_unique<engine::net::LockstepClient>(std::move(connection), engine::net::Hello{engine::net::Role::Player, network.seat});
		m_peer = std::make_unique<engine::net::LockstepPeer>(*m_client);
	}
	bool Valid() const noexcept { return m_client != nullptr; }

	bool Advance() override
	{
		Pump();
		const bool ran = m_peer->Advance(m_simulation, 1) != 0;
		Pump();
		return ran;
	}
	std::uint64_t Generation() const noexcept override { return m_simulation.Generation(); }
	SessionView &View() noexcept override { return m_simulation.Game(); }
	void Submit(const commands::GameCommand &command) override
	{
		engine::net::CommandEnvelope envelope = commands::Encode(command);
		m_client->Submit(envelope.type, std::move(envelope.payload));
	}
	std::vector<std::byte> Checkpoint() const override { return m_simulation.SaveCheckpoint(); }

private:
	void Pump()
	{
		const auto now = std::chrono::steady_clock::now();
		m_transport->Poll(now);
		if (m_server)
		{
			for (auto &accepted : m_transport->Accept())
				m_server->Accept(std::move(accepted));
			m_server->Update(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()));
		}
		m_client->Update();
		m_transport->Poll(now);
	}

	LockstepSession m_simulation;
	std::unique_ptr<engine::net::UdpSocket> m_socket;
	std::unique_ptr<engine::net::ReliableUdpHost> m_transport;
	std::unique_ptr<engine::net::LockstepServer> m_server;
	std::unique_ptr<engine::net::LockstepClient> m_client;
	std::unique_ptr<engine::net::LockstepPeer> m_peer;
};

std::unique_ptr<ClientMatch> MakeClientMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options)
{
	return std::make_unique<LocalClientMatch>(level, content, options);
}

std::unique_ptr<ClientMatch> MakeNetworkMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	NetworkMatchOptions network)
{
	auto match = std::make_unique<NetworkClientMatch>(level, content, options, network);
	if (!match->Valid())
		return nullptr;
	return match;
}

std::unique_ptr<ClientMatch> ResumeClientMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	std::span<const std::byte> checkpoint)
{
	auto match = std::make_unique<LocalClientMatch>(level, content, options, checkpoint);
	if (!match->Valid())
		return nullptr;
	return match;
}
}
