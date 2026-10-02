export module games.generalszh.gameplay.waveguide.systems.wave_guide_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.waveguide.components.wave_guide;
export import games.generalszh.gameplay.waveguide.resources.wave_guide_events;
export import games.generalszh.gameplay.waveguide.algorithms.wave_shape;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.status.components.disabled_until;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.common.health.components.pending_damage;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.identity.components.object_id;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;
export import engine.gameplay.rts.topple.components.topple;
export import engine.gameplay.rts.topple.resources.topple_events;
import Engine.Core.Math.FixedRandom;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.combat.combat_catalog;

// WaveGuideUpdate::update (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/WaveGuideUpdate.cpp) for every flood
// wave, once a tick after the units have moved (one batch: there is one wave to a map, and a victim made wet by one of
// its sample points is spared by the next, in order):
//   - its first update disables it (setDisabled(DISABLED_DEFAULT): DamDie enables it when the dam dies); while
//     disabled it does nothing;
//   - the first update it is enabled is its active frame; it waits while (now - active) < WaveDelay (a fraction of a
//     frame kept: 22.5 frames waits 22 and starts on the 23rd);
//   - starting (initWaveGuide / startMoving): a bad path destroys it; else it sets off along its path (Start), its
//     front's shape is made (WaveShapePoints) and its sprays start riding on it (the presentation's, on `initialized`);
//   - on the first frame more than 15 after its last try (m_splashSoundFrame, 0 at first) it tries a splash sound:
//     GameLogicRandomValue(1, 100) above RandomSplashSoundFrequency plays it (SplashRoll: the wave's own sequence, in
//     the order of its tries);
//   - its front put in the world (transformWaveShape: each point on the ground);
//   - within 100 (2D) of its path's end: WaveSplash01 where it is, and it is destroyed (TheRadar->refreshTerrain is
//     the radar's: the water's heights are not changed here);
//   - doShapeEffects and doWaterMotion are presentation's (the sprays hugging the ground; the water grid, which only the
//     unshipped nVidia demo map enables: no water moves);
//   - doShoreEffects, on even frames only (ShoreSplashes);
//   - doDamage: around each front point (PartitionManager FROM_CENTER_2D within DamageRadius: the spatial index's
//     things, in its order), everything but WAVEGUIDE and BRIDGE_TOWER things, standing no higher than PreferredHeight
//     (a BRIDGE at any height), "behind" the wave (BehindWave) and not yet wet: WaveHit01 riding on the wave at the
//     point's place in its frame (at the victim's height), made wet (Wet: set once the systems have run), toppled away
//     from the point at ToppleForce (NO_BOUNCE | NO_FX: TopplePushes, before the toppling), DamageAmount of DAMAGE_WATER /
//     DEATH_FLOODED from the wave (its pending damage, dealt this tick), and a bridge replaced by WaterWaveBridge
//     (BridgeHit).
export namespace generalszh::gameplay
{
struct WaveGuideSystem
{
	using Query = ecs::Query<ecs::Write<WaveGuide>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::ObjectId>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::StatusFlags>, ecs::Read<engine::gameplay::PendingDamage>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::WaypointGraph>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Read<ObjectTemplates>,
		ecs::Write<engine::gameplay::DisableRequests>, ecs::Write<engine::gameplay::TopplePushes>, ecs::Write<WaveGuideEvents>, ecs::Write<WaveGuideCues>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		using Engine::Math::FixedVector3;
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const gp::WaypointGraph &waypoints = context.Read<gp::WaypointGraph>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto &disables = context.Write<gp::DisableRequests>().list;
		auto &pushes = context.Write<gp::TopplePushes>().list;
		auto &events = context.Write<WaveGuideEvents>().list;
		auto &cues = context.Write<WaveGuideCues>().list;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		static constexpr std::uint64_t wet = std::uint64_t{1} << content::ObjectStatusBit("WET");
		const std::uint32_t water = content::DamageTypeIndex("WATER").value_or(0);
		const std::uint32_t flooded = content::DeathTypeIndex("FLOODED").value_or(0);
		// Made wet this tick (their status is set once the systems have run).
		std::vector<ecs::Entity> wetted;
		query.ForEachChunk([&](auto chunk) {
			auto guides = chunk.template Get<WaveGuide>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto refs = chunk.template Get<gp::DefinitionRef>();
			const auto disabledRows = chunk.template Get<gp::Disabled>();
			const auto ids = chunk.template Get<gp::ObjectId>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < guides.size(); ++row)
			{
				WaveGuide &guide = guides[row];
				const ecs::Entity self = entities[row];
				if (guide.needDisable != 0)
				{
					guide.needDisable = 0;
					disables.push_back({self, gp::disabled_type::Default, 0, gp::DisabledForever});
					continue;
				}
				if (!disabledRows.empty() && disabledRows[row].mask != 0)
					continue;
				if (guide.activeTick == 0)
					guide.activeTick = tick;
				if (Fixed::FromInt(static_cast<std::int64_t>(tick - guide.activeTick)) < guide.delay)
					continue;
				// Where it is for this update (startMoving puts it at its path's start at once).
				FixedVector3 position = transforms[row].position;
				Engine::Math::TurnAngle facing = transforms[row].facing;
				if (guide.initialized == 0)
				{
					const WavePath path = StartAlongPath(waypoints, ground);
					if (path.bad)
					{
						events.push_back({WaveGuideEvent::Kind::Destroy, self});
						continue;
					}
					if (path.found)
					{
						guide.finalDestination = path.final;
						position = path.start;
						facing = path.facing;
						events.push_back({WaveGuideEvent::Kind::Start, self, {}, path.start, path.facing, path.first});
					}
					guide.initialized = 1;
				}
				if (tick - guide.splashTick > WaveSplashFrames)
				{
					guide.splashTick = tick;
					const std::int64_t roll = SplashRoll(seed, ids.empty() ? 0u : ids[row].value, guide.splashTries++);
					if (SplashPlays(roll, guide.splashFrequency))
						cues.push_back({WaveGuideCue::Kind::Splash, self, refs[row].index, position});
				}
				const WaveShape shape = WaveShapePoints(guide.ySize, guide.spacing, guide.bend);
				std::array<FixedVector3, MaxWaveShapePoints> front{};
				for (std::uint32_t point = 0; point < shape.count; ++point)
				{
					const FixedVector2 at = WavePoint(position.XY(), facing, shape.points[point]);
					front[point] = {at.x, at.y, ground.At(at)};
				}
				const Fixed toEndX = guide.finalDestination.x - position.x, toEndY = guide.finalDestination.y - position.y;
				const Fixed reach = Fixed::FromInt(WaveEndDistance);
				if (toEndX * toEndX + toEndY * toEndY <= reach * reach)
				{
					WaveGuideCue end{WaveGuideCue::Kind::End, self, refs[row].index, position};
					end.yaw = facing;
					cues.push_back(end);
					events.push_back({WaveGuideEvent::Kind::Destroy, self});
					continue;
				}
				if ((tick & 1u) == 0)
					for (const ShoreSplash &splash : ShoreSplashes(shape, position, facing, guide.shoreline, guide.preferredHeight,
							 [&](FixedVector2 at) { return ground.At(at); }))
						cues.push_back({splash.right ? WaveGuideCue::Kind::ShoreRight : WaveGuideCue::Kind::ShoreLeft, self, refs[row].index, splash.at});
				const Fixed radiusSquared = guide.damageRadius * guide.damageRadius;
				for (std::uint32_t point = 0; point < shape.count; ++point)
				{
					const FixedVector3 sample = front[point];
					spatial.ForEachWithin(sample.XY(), guide.damageRadius, [&](const gp::SpatialEntry &entry) {
						if (Engine::Math::DistanceSquared(entry.position.XY(), sample.XY()) > radiusSquared)
							return;
						const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(entry.entity);
						if (ref == nullptr)
							return;
						const content::ObjectDefinition &kind = templates.DefinitionAt(ref->index);
						if (kind.Is("WAVEGUIDE") || kind.Is("BRIDGE_TOWER"))
							return;
						const bool bridge = kind.Is("BRIDGE");
						if (entry.position.z > guide.preferredHeight && !bridge)
							return;
						if (!BehindWave(entry.position.XY(), sample))
							return;
						const gp::StatusFlags *status = lookup.Get<gp::StatusFlags>(entry.entity);
						if ((status != nullptr && (status->bits & wet) != 0) || std::ranges::find(wetted, entry.entity) != wetted.end())
							return;
						wetted.push_back(entry.entity);
						WaveGuideCue hit{WaveGuideCue::Kind::Hit, self, refs[row].index, position};
						hit.local = {shape.points[point].x, shape.points[point].y, entry.position.z};
						hit.yaw = facing;
						cues.push_back(hit);
						events.push_back({WaveGuideEvent::Kind::Wet, self, entry.entity, entry.position});
						pushes.push_back({entry.entity, entry.position.XY() - sample.XY(), guide.toppleForce, gp::topple_option::NoBounce | gp::topple_option::NoFx});
						// attemptDamage: dealt with the tick's damage (with what was already waiting of the same kind, as
						// DamageFrom keeps it).
						gp::PendingDamage damage{self, guide.damageAmount, water, flooded};
						if (const gp::PendingDamage *waiting = lookup.Get<gp::PendingDamage>(entry.entity))
						{
							if (waiting->damageType == water && waiting->deathType == flooded && waiting->source == self)
								damage.amount += waiting->amount;
							commands.Set<gp::PendingDamage>(entry.entity, damage);
						}
						else
							commands.Add<gp::PendingDamage>(entry.entity, damage);
						if (bridge)
							events.push_back({WaveGuideEvent::Kind::BridgeHit, self, entry.entity, entry.position});
					});
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::WaveGuideSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wave_guides";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
