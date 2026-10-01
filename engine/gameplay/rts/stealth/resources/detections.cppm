export module engine.gameplay.rts.stealth.resources.detections;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Stealth detection this tick: who each detector reveals until when (per
// chunk), then gathered by target for the reveal pass (the latest wins).
export namespace engine::gameplay
{
struct Detection
{
	ecs::Entity target;
	std::uint64_t until{0};
	ecs::Entity detector; // who saw it (the latest)
	Engine::Math::FixedVector3 position; // where it was found (a garrison's rider: the garrison's spot)
	std::uint32_t player{0};             // whose it is
	std::uint32_t reserved{0};
};

// Each detector's scan this tick, and whether it found anything (for the
// presentation's IR pings); per chunk, in chunk order.
struct DetectorPing
{
	ecs::Entity detector;
	std::uint32_t found{0};
	std::uint32_t reserved{0};
};

struct DetectorPings : ecs::ChunkOutputs<DetectorPing>
{
};

struct DetectionOffers : ecs::ChunkOutputs<Detection>
{
};

class Detections
{
public:
	void Gather(const DetectionOffers &offers)
	{
		m_detections.clear();
		offers.ForEach([&](const Detection &detection) { m_detections.push_back(detection); });
		std::sort(m_detections.begin(), m_detections.end(), [](const Detection &a, const Detection &b) {
			if (a.target.index != b.target.index)
				return a.target.index < b.target.index;
			return a.target.generation != b.target.generation ? a.target.generation < b.target.generation : a.until > b.until;
		});
		m_detections.erase(std::unique(m_detections.begin(), m_detections.end(), [](const Detection &a, const Detection &b) { return a.target == b.target; }),
			m_detections.end());
	}

	// Until when the target is revealed this tick (0: not).
	std::uint64_t For(ecs::Entity target) const
	{
		const Detection *found = Find(target);
		return found != nullptr ? found->until : 0;
	}

	// The target's detection this tick (the latest-lasting), if any.
	const Detection *Find(ecs::Entity target) const
	{
		const auto found = std::lower_bound(m_detections.begin(), m_detections.end(), target, [](const Detection &detection, ecs::Entity entity) {
			return detection.target.index != entity.index ? detection.target.index < entity.index : detection.target.generation < entity.generation;
		});
		return found != m_detections.end() && found->target == target ? &*found : nullptr;
	}

	std::size_t Count() const noexcept { return m_detections.size(); }
	std::span<const Detection> All() const noexcept { return m_detections; }

private:
	std::vector<Detection> m_detections;
};

// Something revealed this tick (StealthUpdate::markAsDetected) whose OrderIdleEnemiesToAttackMeUponReveal wakes its idle
// enemies that see it: where it is, whose it is and that player's default team (per chunk, in chunk order).
struct StealthReveal
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
	std::uint32_t player{0};
	std::uint32_t team{0xFFFFFFFFu};
};

// Reveals by its own update (a disguiser near its victim: RevealDistanceFromTarget).
struct RevealWakes : ecs::ChunkOutputs<StealthReveal>
{
};

// Reveals by stealth detectors.
struct DetectionWakes : ecs::ChunkOutputs<StealthReveal>
{
};

// A disguiser's look changed this tick (changeVisualDisguise), for the presentation's FX and sounds: disguised (DisguiseFX,
// DisguiseStarted), or back to its own (DisguiseRevealFX; DisguiseRevealedSuccess with a victim, else
// DisguiseRevealedFailure). Per chunk, in chunk order.
struct DisguiseEvent
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
	std::uint8_t disguised{0};
	std::uint8_t success{0};
	std::uint8_t reserved[6]{};
};

struct DisguiseEvents : ecs::ChunkOutputs<DisguiseEvent>
{
};

// Temporary stealth granted this tick (receiveGrant(TRUE, frames): a supply centre's GrantTemporaryStealth), sorted by
// entity for the reveal pass. Its game's grant pass is its one writer.
struct TemporaryStealthGrant
{
	ecs::Entity entity;
	std::uint32_t frames{0};
	std::uint32_t reserved{0};
};

struct TemporaryStealthGrants
{
	std::vector<TemporaryStealthGrant> list;

	void Sort()
	{
		std::ranges::sort(list, [](const TemporaryStealthGrant &a, const TemporaryStealthGrant &b) {
			return a.entity.index != b.entity.index ? a.entity.index < b.entity.index : a.entity.generation < b.entity.generation;
		});
	}

	const TemporaryStealthGrant *Find(ecs::Entity entity) const
	{
		const auto found = std::lower_bound(list.begin(), list.end(), entity, [](const TemporaryStealthGrant &grant, ecs::Entity key) {
			return grant.entity.index != key.index ? grant.entity.index < key.index : grant.entity.generation < key.generation;
		});
		return found != list.end() && found->entity == entity ? &*found : nullptr;
	}
};

// Stealth granted this tick (per chunk), then gathered for the reveal pass.
struct GrantOffers : ecs::ChunkOutputs<ecs::Entity>
{
};

class StealthGrants
{
public:
	void Gather(const GrantOffers &offers)
	{
		m_granted.clear();
		offers.ForEach([&](ecs::Entity entity) { m_granted.push_back(entity); });
		std::sort(m_granted.begin(), m_granted.end(), Less);
		m_granted.erase(std::unique(m_granted.begin(), m_granted.end()), m_granted.end());
	}

	bool Contains(ecs::Entity entity) const { return std::binary_search(m_granted.begin(), m_granted.end(), entity, Less); }
	std::size_t Count() const noexcept { return m_granted.size(); }

private:
	static bool Less(ecs::Entity a, ecs::Entity b) noexcept { return a.index != b.index ? a.index < b.index : a.generation < b.generation; }

	std::vector<ecs::Entity> m_granted;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::TemporaryStealthGrants>
{
	static constexpr std::string_view StableName = "engine.gameplay.temporary_stealth_grants";
};
template<>
struct ResourceTraits<engine::gameplay::DetectionOffers>
{
	static constexpr std::string_view StableName = "engine.gameplay.detection_offers";
};
template<>
struct ResourceTraits<engine::gameplay::DetectorPings>
{
	static constexpr std::string_view StableName = "engine.gameplay.detector_pings";
};
template<>
struct ResourceTraits<engine::gameplay::Detections>
{
	static constexpr std::string_view StableName = "engine.gameplay.detections";
};
template<>
struct ResourceTraits<engine::gameplay::RevealWakes>
{
	static constexpr std::string_view StableName = "engine.gameplay.reveal_wakes";
};
template<>
struct ResourceTraits<engine::gameplay::DetectionWakes>
{
	static constexpr std::string_view StableName = "engine.gameplay.detection_wakes";
};
template<>
struct ResourceTraits<engine::gameplay::DisguiseEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.disguise_events";
};
template<>
struct ResourceTraits<engine::gameplay::GrantOffers>
{
	static constexpr std::string_view StableName = "engine.gameplay.grant_offers";
};
template<>
struct ResourceTraits<engine::gameplay::StealthGrants>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_grants";
};
}
