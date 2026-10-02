export module engine.gameplay.rts.containment.resources.cargo_manifest;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;
export import engine.ecs.core.entity_codec;
export import Engine.Core.Math.FixedVector;

// Who rides in which transport, in boarding order, plus the tick's boarding
// and unloading requests (emitted per chunk by the containment systems and
// applied by the session between ticks, since they change archetypes).
// Transports may be linked into networks (a player's tunnels: the original's
// TunnelTracker), in the order they were linked: one network's riders share
// its room, and a linked transport that goes takes nobody down while another
// of its network stands; they move to the first one left (onTunnelDestroyed).
export namespace engine::gameplay
{
struct BoardRequest
{
	ecs::Entity passenger;
	ecs::Entity transport;
	// At the transport and getting in; else still on its way (the transport knows it is awaited: an aircraft
	// comes down for it, as ChinookAIUpdate lands while hasObjectsWantingToEnterOrExit).
	bool arrived{true};
	bool touchOnly{false}; // Boarding::touchOnly: the game acts on it, never cargo
};

struct ExitRequest
{
	ecs::Entity transport;
};

// One rider let out of its container by name (HealContain: healed, out through a door).
struct RiderExit
{
	ecs::Entity transport;
	ecs::Entity rider;
};

using BoardRequests = ecs::ChunkOutputs<BoardRequest>;
using ExitRequests = ecs::ChunkOutputs<ExitRequest>;

// The next rider dropped out of a delivering transport (DeliverPayloadAIUpdate's DeliveringState: aiExit, then placed
// where it was plus the drop's variance and offset), and sent on to `moveTo` when it `moves` (not parachuting
// directly on to the target).
struct DropExit
{
	ecs::Entity transport;
	Engine::Math::FixedVector3 offset;
	Engine::Math::FixedVector2 moveTo;
	bool moves{false};
	// InheritTransportVelocity: the rider is pushed by the carrier's velocity taken as a force (applyForce).
	bool inherit{false};
	Engine::Math::FixedVector3 velocity;
};
using DropExits = ecs::ChunkOutputs<DropExit>;

// A rider taken out of its transport by name and put where it is told, facing as told, on physics (exitObjectViaDoor
// then setWorldTransform: a rappeller at the end of its rope; the transport aloft, it may fall: setAllowToFall).
struct PlacedExit
{
	ecs::Entity transport;
	ecs::Entity rider;
	Engine::Math::FixedVector3 at;
	Engine::Math::TurnAngle facing;
};
using PlacedExits = ecs::ChunkOutputs<PlacedExit>;

struct RiderExits : ecs::ChunkOutputs<RiderExit>
{
};

// Riders let out because they asked (ExitIntent), this tick.
struct IntentExits : ecs::ChunkOutputs<RiderExit>
{
};

// A rider put into or taken out of a container this tick (OpenContain::onContaining / onRemoving), in order.
struct CargoChange
{
	ecs::Entity container;
	ecs::Entity rider;
	bool entered{false};
	bool quiet{false}; // without its load sound (enableLoadSounds(FALSE): an initial payload)
};

class CargoManifest
{
public:
	void Board(ecs::Entity transport, ecs::Entity passenger, bool quiet = false)
	{
		m_cargo[Key(transport)].push_back(passenger);
		m_changes.push_back({transport, passenger, true, quiet});
	}

	// The next passenger to leave (the first aboard), if any.
	ecs::Entity TakeNext(ecs::Entity transport)
	{
		const auto found = m_cargo.find(Key(transport));
		if (found == m_cargo.end() || found->second.empty())
			return {};
		const ecs::Entity passenger = found->second.front();
		found->second.erase(found->second.begin());
		if (found->second.empty())
			m_cargo.erase(found);
		m_changes.push_back({transport, passenger, false});
		return passenger;
	}

	// That passenger, if aboard.
	ecs::Entity Take(ecs::Entity transport, ecs::Entity passenger)
	{
		const auto found = m_cargo.find(Key(transport));
		if (found == m_cargo.end())
			return {};
		const auto at = std::find(found->second.begin(), found->second.end(), passenger);
		if (at == found->second.end())
			return {};
		found->second.erase(at);
		if (found->second.empty())
			m_cargo.erase(found);
		m_changes.push_back({transport, passenger, false});
		return passenger;
	}

	// Everyone aboard, emptying the transport (it is gone). A networked one hands them to the first other transport
	// of its network instead, if any is left, and returns nobody.
	std::vector<ecs::Entity> TakeAll(ecs::Entity transport)
	{
		const std::optional<std::uint32_t> network = NetworkOf(transport);
		Unlink(transport);
		const auto found = m_cargo.find(Key(transport));
		if (found == m_cargo.end())
			return {};
		std::vector<ecs::Entity> passengers = std::move(found->second);
		m_cargo.erase(found);
		if (network)
			if (const auto others = Network(*network); !others.empty())
			{
				auto &home = m_cargo[Key(others.front())];
				home.insert(home.end(), passengers.begin(), passengers.end());
				return {};
			}
		for (const ecs::Entity passenger : passengers)
			m_changes.push_back({transport, passenger, false});
		return passengers;
	}

	// Everyone aboard `from` into `to`, after those already there.
	void MoveAll(ecs::Entity from, ecs::Entity to)
	{
		const auto found = m_cargo.find(Key(from));
		if (from == to || found == m_cargo.end())
			return;
		std::vector<ecs::Entity> passengers = std::move(found->second);
		m_cargo.erase(found);
		auto &home = m_cargo[Key(to)];
		home.insert(home.end(), passengers.begin(), passengers.end());
	}

	// One passenger aboard `from` into `to`, after those already there, as MoveAll (no rider in or out: a network's
	// transports share one list of riders). False: it was not aboard `from`.
	bool Move(ecs::Entity from, ecs::Entity to, ecs::Entity passenger)
	{
		const auto found = m_cargo.find(Key(from));
		if (from == to || found == m_cargo.end())
			return false;
		const auto at = std::find(found->second.begin(), found->second.end(), passenger);
		if (at == found->second.end())
			return false;
		found->second.erase(at);
		if (found->second.empty())
			m_cargo.erase(found);
		m_cargo[Key(to)].push_back(passenger);
		return true;
	}

	void Link(ecs::Entity transport, std::uint32_t network)
	{
		Unlink(transport);
		m_networks.emplace_back(transport, network);
	}

	void Unlink(ecs::Entity transport)
	{
		std::erase_if(m_networks, [&](const auto &link) { return link.first == transport; });
	}

	std::optional<std::uint32_t> NetworkOf(ecs::Entity transport) const
	{
		for (const auto &[linked, network] : m_networks)
			if (linked == transport)
				return network;
		return std::nullopt;
	}

	// A network's transports, first linked first.
	std::vector<ecs::Entity> Network(std::uint32_t network) const
	{
		std::vector<ecs::Entity> transports;
		for (const auto &[linked, id] : m_networks)
			if (id == network)
				transports.push_back(linked);
		return transports;
	}

	// Every network, in the order its first transport was linked.
	std::vector<std::uint32_t> Networks() const
	{
		std::vector<std::uint32_t> networks;
		for (const auto &[linked, id] : m_networks)
			if (std::find(networks.begin(), networks.end(), id) == networks.end())
				networks.push_back(id);
		return networks;
	}

	// How many ride anywhere in a network.
	std::size_t NetworkCount(std::uint32_t network) const
	{
		std::size_t count = 0;
		for (const ecs::Entity transport : Network(network))
			count += Count(transport);
		return count;
	}

	void Forget(ecs::Entity passenger)
	{
		Unlink(passenger);
		for (auto it = m_cargo.begin(); it != m_cargo.end();)
		{
			auto &list = it->second;
			if (std::find(list.begin(), list.end(), passenger) != list.end())
				m_changes.push_back({ecs::Entity{static_cast<ecs::EntityIndex>(it->first >> 32), static_cast<ecs::EntityGeneration>(it->first & 0xFFFFFFFFu)},
					passenger, false});
			list.erase(std::remove(list.begin(), list.end(), passenger), list.end());
			it = list.empty() ? m_cargo.erase(it) : std::next(it);
		}
	}

	// Who is aboard, first aboard first.
	std::span<const ecs::Entity> Aboard(ecs::Entity transport) const
	{
		const auto found = m_cargo.find(Key(transport));
		return found == m_cargo.end() ? std::span<const ecs::Entity>{} : std::span<const ecs::Entity>(found->second);
	}

	// This tick's riders in and out (not part of the saved state: the session clears them as each tick starts).
	std::span<const CargoChange> Changes() const noexcept { return m_changes; }
	void ClearChanges() noexcept { m_changes.clear(); }

	std::size_t Count(ecs::Entity transport) const
	{
		const auto found = m_cargo.find(Key(transport));
		return found == m_cargo.end() ? 0 : found->second.size();
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_cargo.size()));
		for (const auto &[transport, passengers] : m_cargo)
		{
			writer.U64(transport);
			writer.U32(static_cast<std::uint32_t>(passengers.size()));
			for (const ecs::Entity passenger : passengers)
				ecs::WriteEntity(writer, passenger);
		}
		writer.U32(static_cast<std::uint32_t>(m_networks.size()));
		for (const auto &[transport, network] : m_networks)
		{
			ecs::WriteEntity(writer, transport);
			writer.U32(network);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		std::map<std::uint64_t, std::vector<ecs::Entity>> cargo;
		const auto count = reader.U32();
		for (std::uint32_t index = 0; count && index < *count && !reader.Failed(); ++index)
		{
			auto &passengers = cargo[reader.U64().value_or(0)];
			const auto riders = reader.U32();
			for (std::uint32_t rider = 0; riders && rider < *riders && !reader.Failed(); ++rider)
				passengers.push_back(ecs::ReadEntity(reader).value_or(ecs::Entity{}));
		}
		std::vector<std::pair<ecs::Entity, std::uint32_t>> networks;
		const auto links = reader.U32();
		for (std::uint32_t index = 0; links && index < *links && !reader.Failed(); ++index)
		{
			const ecs::Entity transport = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			networks.emplace_back(transport, reader.U32().value_or(0));
		}
		if (reader.Failed() || !count || !links)
			return false;
		m_cargo = std::move(cargo);
		m_networks = std::move(networks);
		m_changes.clear();
		return true;
	}

private:
	static std::uint64_t Key(ecs::Entity entity) noexcept { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; }

	std::map<std::uint64_t, std::vector<ecs::Entity>> m_cargo;
	std::vector<std::pair<ecs::Entity, std::uint32_t>> m_networks; // linked transports and their network, in linking order
	std::vector<CargoChange> m_changes;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::CargoManifest>
{
	static constexpr std::string_view StableName = "engine.gameplay.cargo_manifest";
};

template<>
struct ResourceTraits<engine::gameplay::BoardRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.board_requests";
};

template<>
struct ResourceTraits<engine::gameplay::ExitRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.exit_requests";
};

template<>
struct ResourceTraits<engine::gameplay::DropExits>
{
	static constexpr std::string_view StableName = "engine.gameplay.drop_exits";
};
template<>
struct ResourceTraits<engine::gameplay::PlacedExits>
{
	static constexpr std::string_view StableName = "engine.gameplay.placed_exits";
};

template<>
struct ResourceTraits<engine::gameplay::RiderExits>
{
	static constexpr std::string_view StableName = "engine.gameplay.rider_exits";
};

template<>
struct ResourceTraits<engine::gameplay::IntentExits>
{
	static constexpr std::string_view StableName = "engine.gameplay.intent_exits";
};
}
