export module engine.gameplay.rts.containment.components.garrison_points;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Where a container's occupants stand to shoot (a garrison's fire points), in its own frame, for each damage state it
// may be in (pristine, damaged, really damaged: the model's bones differ): each occupant with a victim takes the free
// one of `condition`'s set nearest it. As the original, every set has all Max places: those past its bones stand in
// its middle.
export namespace engine::gameplay
{
namespace garrison_condition
{
inline constexpr std::uint32_t Pristine = 0;
inline constexpr std::uint32_t Damaged = 1;
inline constexpr std::uint32_t ReallyDamaged = 2;
inline constexpr std::uint32_t Count = 3;
}

struct GarrisonPoints
{
	static constexpr std::size_t Max = 40;
	std::array<std::array<Engine::Math::FixedVector3, Max>, garrison_condition::Count> points{};
	std::array<std::uint32_t, garrison_condition::Count> counts{};
	std::uint32_t condition{garrison_condition::Pristine}; // the set in use: the container's damage state
};

// Where a transport's riders stand (OpenContain::putObjAtNextFirePoint), in its own frame: its FIREPOINT bones, taken
// in turn by its riders from the first in; with `inTurret`, turned with its turret about `turretPivot`.
struct TransportFirePoints
{
	static constexpr std::size_t Max = 32; // MAX_FIRE_POINTS
	std::array<Engine::Math::FixedVector3, Max> points{};
	Engine::Math::FixedVector3 turretPivot;
	std::uint32_t count{0};
	std::uint32_t inTurret{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::TransportFirePoints>
{
	static constexpr std::string_view StableName = "engine.gameplay.transport_fire_points";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ComponentTraits<engine::gameplay::GarrisonPoints>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_points";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::GarrisonPoints &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.condition);
		for (std::size_t set = 0; set < value.points.size(); ++set)
		{
			hasher.AppendU64(value.counts[set]);
			for (std::size_t index = 0; index < value.counts[set] && index < value.points[set].size(); ++index)
			{
				hasher.AppendU64(static_cast<std::uint64_t>(value.points[set][index].x.Raw()));
				hasher.AppendU64(static_cast<std::uint64_t>(value.points[set][index].y.Raw()));
				hasher.AppendU64(static_cast<std::uint64_t>(value.points[set][index].z.Raw()));
			}
		}
	}
};
}
