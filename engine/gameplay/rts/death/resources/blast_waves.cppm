export module engine.gameplay.rts.death.resources.blast_waves;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import engine.gameplay.common.health.resources.incoming_damage;
import engine.ecs.system.system;

// The tick's blast waves (see BlastWaveDefinition), in missile order: the pushes that topple what is in reach, the
// damage they deal, the scorch waves that burn the look of what is in reach, and the scorch marks left on the ground
// (for presentation). Remade every tick.
export namespace engine::gameplay
{
struct BlastPush
{
	Engine::Math::FixedVector3 center;
	Engine::Math::Fixed radius;
	Engine::Math::Fixed toppleSpeed;
};

struct BlastBurn
{
	Engine::Math::FixedVector3 center;
	Engine::Math::Fixed radius;
};

struct BlastScorchMark
{
	ecs::Entity source;
	Engine::Math::FixedVector3 position;
	Engine::Math::Fixed size;
};

struct BlastWaves
{
	std::vector<BlastPush> pushes;
	std::vector<DamageRecord> damage;
	std::vector<BlastBurn> burns;
	std::vector<BlastScorchMark> marks;

	void Clear() noexcept
	{
		pushes.clear();
		damage.clear();
		burns.clear();
		marks.clear();
	}

	// Within a wave's reach: centre to centre, flat (PartitionManager FROM_CENTER_2D).
	static bool Reaches(Engine::Math::FixedVector3 center, Engine::Math::Fixed radius, Engine::Math::FixedVector3 at) noexcept
	{
		return Engine::Math::DistanceSquared(at.XY(), center.XY()) <= radius * radius;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::BlastWaves>
{
	static constexpr std::string_view StableName = "engine.gameplay.blast_waves";
};
}
