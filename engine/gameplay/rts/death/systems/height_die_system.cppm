export module engine.gameplay.rts.death.systems.height_die_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.death.components.height_die;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.common.spatial.components.body_extent;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;

// HeightDieUpdate::update: from the first look plus InitialDelay, an object not carried (off the map) dies once it is
// below TargetHeight above the ground (while falling, when it must be), put on the ground if it snaps or went below it;
// carried, it only notes where it is. Its kill joins this tick's deaths (Object::kill). TargetHeightIncludesStructures:
// the structures whose bounding spheres come within its bounding circle's radius of its own (FROM_BOUNDINGSPHERE_3D),
// the tallest of them (getMaxHeightAbovePosition) above the ground under it is its target when taller than TargetHeight
// (bridge layers are not in the port). Falling, or dead, below DestroyAttachedParticlesAtHeight its attached particle
// systems go, once.
export namespace engine::gameplay
{
struct HeightDieSystem
{
	using Query = ecs::Query<ecs::Write<HeightDie>, ecs::Write<Transform>, ecs::Optional<OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<BodyExtent>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>, ecs::Read<SpatialIndex>, ecs::Write<KillRequests>, ecs::Write<ParticleClears>>;

	// The tallest structure whose bounding sphere comes within `range` of `self`'s (none: 0).
	static Engine::Math::Fixed TallestStructure(const SpatialIndex &spatial, const auto &lookup, ecs::Entity self, const Engine::Math::FixedVector3 &at,
		const HeightDie &own, Engine::Math::Fixed range)
	{
		using Engine::Math::Fixed;
		const Engine::Math::FixedVector3 centre{at.x, at.y, at.z + own.centerZ};
		Fixed tallest{};
		// Horizontal reach well past any structure's bounding sphere; each checked exactly below.
		spatial.ForEachWithin(at.XY(), range + own.sphereRadius + Fixed::FromInt(200), [&](const SpatialEntry &entry) {
			if (entry.entity == self || (entry.classes & target_class::Structure) == 0)
				return;
			const BodyExtent *extent = lookup.template Get<BodyExtent>(entry.entity);
			if (extent == nullptr)
				return;
			const Engine::Math::FixedVector3 other{entry.position.x, entry.position.y, entry.position.z + extent->centerZ};
			Fixed apart = Engine::Math::Length(other - centre) - own.sphereRadius - extent->sphereRadius;
			if (apart < Fixed{})
				apart = Fixed{};
			if (apart < range && extent->maxHeight > tallest)
				tallest = extent->maxHeight;
		});
		return tallest;
	}

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const GroundHeight &ground = context.Read<GroundHeight>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const auto lookup = context.Lookup<Lookup>();
		auto &kills = context.Write<KillRequests>().entities;
		auto &clears = context.Write<ParticleClears>().entities;
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto dies = chunk.template Get<HeightDie>();
			auto transforms = chunk.template Get<Transform>();
			const auto carried = chunk.template Get<OffMap>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < dies.size(); ++row)
			{
				HeightDie &die = dies[row];
				if (die.earliestDeath == std::numeric_limits<std::uint64_t>::max())
					die.earliestDeath = now + die.initialDelay;
				if (die.earliestDeath > now)
					continue;
				auto &position = transforms[row].position;
				if (!carried.empty())
				{
					die.lastZ = position.z;
					continue;
				}
				bool directionOk = true;
				if (die.died == 0)
				{
					directionOk = die.onlyWhenMovingDown == 0 || position.z < die.lastZ;
					const Engine::Math::Fixed terrain = ground.At(position.XY());
					Engine::Math::Fixed target = terrain + die.targetHeight;
					if (die.includeStructures != 0)
					{
						const Engine::Math::Fixed tallest = TallestStructure(spatial, lookup, entities[row], position, die, die.circleRadius);
						if (tallest > die.targetHeight)
							target = tallest + terrain;
					}
					if (position.z < target && directionOk)
					{
						if (die.snapToGround != 0 || position.z < terrain)
							position.z = terrain;
						kills.push_back(entities[row]);
						die.died = 1;
					}
				}
				if (die.particlesGone == 0 && position.z < die.particlesAt && (die.died != 0 || directionOk))
				{
					clears.push_back(entities[row]);
					die.particlesGone = 1;
				}
				die.lastZ = position.z;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HeightDieSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.height_die";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The composition orders it after cargo is let go and before the tick's deaths are carried out.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
