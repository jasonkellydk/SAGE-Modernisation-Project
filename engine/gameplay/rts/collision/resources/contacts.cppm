export module engine.gameplay.rts.collision.resources.contacts;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.resources.spatial_index;
import engine.ecs.system.system;

// Where the bodies are this tick (every collider, not only targets), and
// who ran into whom: a moving crusher touching another body. Contacts are
// gathered per chunk, then ordered by the body run into, so each body's
// chunk finds its own (For).
export namespace engine::gameplay
{
struct ColliderIndex : SpatialIndex
{
	using SpatialIndex::SpatialIndex;
};

struct ColliderGather : ecs::ChunkOutputs<SpatialEntry>
{
};

struct Contact
{
	ecs::Entity other; // run into
	ecs::Entity mover;
	Engine::Math::FixedVector3 moverPosition;
	Engine::Math::Fixed moverSpeed; // per tick
	Engine::Math::Fixed moverRadius;
	Engine::Math::TurnAngle moverFacing; // it moves the way it faces
	std::uint32_t crusherLevel{0};
	std::uint32_t moverPlayer{0};
};

struct ContactOffers : ecs::ChunkOutputs<Contact>
{
};

class Contacts
{
public:
	void Gather(const ContactOffers &offers)
	{
		m_contacts.clear();
		offers.ForEach([&](const Contact &contact) { m_contacts.push_back(contact); });
		std::stable_sort(m_contacts.begin(), m_contacts.end(), [](const Contact &a, const Contact &b) {
			return a.other.index != b.other.index ? a.other.index < b.other.index : a.other.generation < b.other.generation;
		});
	}

	std::span<const Contact> For(ecs::Entity other) const
	{
		const auto first = std::lower_bound(m_contacts.begin(), m_contacts.end(), other, [](const Contact &contact, ecs::Entity entity) {
			return contact.other.index != entity.index ? contact.other.index < entity.index : contact.other.generation < entity.generation;
		});
		auto last = first;
		while (last != m_contacts.end() && last->other == other)
			++last;
		return {first, last};
	}

	std::span<const Contact> All() const noexcept { return m_contacts; }

private:
	std::vector<Contact> m_contacts;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ColliderIndex>
{
	static constexpr std::string_view StableName = "engine.gameplay.collider_index";
};
template<>
struct ResourceTraits<engine::gameplay::ColliderGather>
{
	static constexpr std::string_view StableName = "engine.gameplay.collider_gather";
};
template<>
struct ResourceTraits<engine::gameplay::ContactOffers>
{
	static constexpr std::string_view StableName = "engine.gameplay.contact_offers";
};
template<>
struct ResourceTraits<engine::gameplay::Contacts>
{
	static constexpr std::string_view StableName = "engine.gameplay.contacts";
};
}
