export module games.generalszh.gameplay.appearance.components.steering_look;
import std;

export import engine.ecs.core.component_registry;

// A vehicle showing its turns (AnimationSteeringUpdate): the turn look it is in
// and the tick it may change it next.
export namespace generalszh::gameplay
{
enum class SteeringPose : std::uint8_t
{
	Straight,
	CenterToRight,
	CenterToLeft,
	RightToCenter,
	LeftToCenter,
};

struct SteeringLook
{
	std::uint64_t nextTick{0};
	std::uint64_t transitionTicks{0};
	SteeringPose pose{SteeringPose::Straight};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SteeringLook>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.steering_look";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::SteeringLook &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.nextTick);
		hasher.AppendU64(value.transitionTicks << 8 | static_cast<std::uint64_t>(value.pose));
	}
};
}
