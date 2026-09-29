export module engine.gameplay.rts.stealth.resources.detections;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
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
		const auto found = std::lower_bound(m_detections.begin(), m_detections.end(), target, [](const Detection &detection, ecs::Entity entity) {
			return detection.target.index != entity.index ? detection.target.index < entity.index : detection.target.generation < entity.generation;
		});
		return found != m_detections.end() && found->target == target ? found->until : 0;
	}

	std::size_t Count() const noexcept { return m_detections.size(); }
	std::span<const Detection> All() const noexcept { return m_detections; }

private:
	std::vector<Detection> m_detections;
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
