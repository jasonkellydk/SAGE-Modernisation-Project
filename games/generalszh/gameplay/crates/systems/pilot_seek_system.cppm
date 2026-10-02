export module games.generalszh.gameplay.crates.systems.pilot_seek_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.crates.components.pilot_seeker;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.parachute.components.parachute;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.veterancy.resources.experience_awards;

// A computer player's pilots climbing into its vehicles (PilotFindVehicleUpdate with VeterancyCrateCollide), each tick:
// a pilot going for a vehicle that runs into it (their footprints meet, the pilot on the ground) while the vehicle may
// still take it gives it its own levels (gainExpForLevel, unscaled) and is gone (destroyObject); one it may no longer
// join is given up. Every ScanRate an idle pilot (not moving, attacking or going for one) looks within ScanRange, nearest
// first, for its player's vehicle at least MinHealth healthy that it may join (CrateCollide / VeterancyCrateCollide::
// isValidToExecute) and goes for it (aiEnter). Human players' pilots look for none (they go for one their player sends
// them into: aiEnter); pilots hanging from a parachute stay put.
export namespace generalszh::gameplay
{
// CrateCollide / VeterancyCrateCollide::isValidToExecute for a pilot bringing `levels` to `vehicle` (with an AI, of its
// kinds, alive, on the ground, trainable and short of the top level; a pilot's: its player's, not flying), through
// `access` (a world or a system's lookup).
template<typename Access>
bool PilotMayJoin(const PilotSeeker &seeker, std::uint32_t player, std::uint32_t levels, ecs::Entity vehicle, const Access &access,
	const ObjectTemplates &templates, const engine::gameplay::GroundHeight &ground, Engine::Math::Fixed significant)
{
	namespace gp = engine::gameplay;
	if (!access.IsAlive(vehicle) || access.template Get<gp::Dying>(vehicle) != nullptr || access.template Get<gp::OffMap>(vehicle) != nullptr)
		return false;
	const auto *definition = access.template Get<gp::DefinitionRef>(vehicle);
	const auto *owner = access.template Get<gp::Owner>(vehicle);
	const auto *place = access.template Get<gp::Transform>(vehicle);
	if (definition == nullptr || owner == nullptr || place == nullptr)
		return false;
	const content::ObjectDefinition &object = templates.DefinitionAt(definition->index);
	if (!templates.HasAI(definition->index))
		return false;
	for (std::size_t word = 0; word < seeker.required.size(); ++word)
		if ((object.kinds[word] & seeker.required[word]) != seeker.required[word] || (object.kinds[word] & seeker.forbidden[word]) != 0)
			return false;
	if (const auto *health = access.template Get<gp::Health>(vehicle); health != nullptr && gp::IsDead(*health))
		return false;
	if (place->position.z - ground.At(place->position.XY()) > significant)
		return false;
	const auto *experience = access.template Get<gp::Experience>(vehicle);
	if (levels == 0 || experience == nullptr || !experience->trainable || experience->level + 1u >= gp::VeterancyLevelCount)
		return false;
	if (seeker.isPilot != 0)
	{
		if (owner->player != player)
			return false;
		if (const auto *motion = access.template Get<gp::Locomotion>(vehicle); motion != nullptr && gp::IsAirborne(motion->locomotor))
			return false;
	}
	return true;
}

struct PilotSeekSystem
{
	using Query = ecs::Query<ecs::Write<PilotSeeker>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>,
		ecs::Read<engine::gameplay::Experience>, ecs::Write<engine::gameplay::MoveOrder>, ecs::Optional<engine::gameplay::Collider>,
		ecs::Optional<engine::gameplay::AttackTarget>, ecs::Exclude<engine::gameplay::OffMap>, ecs::Exclude<engine::gameplay::Dying>,
		ecs::Exclude<engine::gameplay::ParachuteRider>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>,
		ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Experience>, ecs::Read<engine::gameplay::Dying>,
		ecs::Read<engine::gameplay::OffMap>, ecs::Read<engine::gameplay::Locomotion>, ecs::Read<engine::gameplay::Collider>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::PhysicsSettings>, ecs::Read<engine::gameplay::HarvestCatalog>, ecs::Write<engine::gameplay::ExperienceAwards>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const Fixed significant = context.Read<gp::PhysicsSettings>().SignificantHeight();
		const gp::HarvestCatalog &players = context.Read<gp::HarvestCatalog>();
		auto &awards = context.Write<gp::ExperienceAwards>().list;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto seekers = chunk.template Get<PilotSeeker>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto experiences = chunk.template Get<gp::Experience>();
			auto moves = chunk.template Get<gp::MoveOrder>();
			const auto colliders = chunk.template Get<gp::Collider>();
			const auto targets = chunk.template Get<gp::AttackTarget>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < seekers.size(); ++row)
			{
				const std::uint32_t player = owners[row].player;
				PilotSeeker &seeker = seekers[row];
				const gp::Transform &at = transforms[row];
				const bool pilotAloft = at.position.z - ground.At(at.position.XY()) > Fixed{};
				// VeterancyCrateCollide::getLevelsToGain: its own levels (AddsOwnerVeterancy), else one.
				const std::uint32_t levels = seeker.addsOwnerVeterancy != 0 ? experiences[row].level : 1u;
				// CrateCollide / VeterancyCrateCollide::isValidToExecute, for the vehicle (and the pilot on the ground).
				const auto joinable = [&](ecs::Entity vehicle) {
					return PilotMayJoin(seeker, player, levels, vehicle, lookup, templates, ground, significant);
				};
				// Going for a vehicle: joined on meeting it, given up once it may no longer be joined.
				if (seeker.goal != ecs::Entity{})
				{
					if (!joinable(seeker.goal))
						seeker.goal = {};
					else
					{
						const auto &there = lookup.template Get<gp::Transform>(seeker.goal)->position;
						const auto *vehicleBody = lookup.template Get<gp::Collider>(seeker.goal);
						const Fixed reach = (colliders.empty() ? Fixed{} : colliders[row].radius) + (vehicleBody != nullptr ? vehicleBody->radius : Fixed{});
						if (!pilotAloft && Engine::Math::DistanceSquared(at.position.XY(), there.XY()) <= reach * reach)
						{
							awards.push_back({seeker.goal, static_cast<std::uint8_t>(levels), false});
							commands.Add<gp::Lifetime>(entities[row], gp::Lifetime{tick, 1, 0});
							seeker.goal = {};
							continue;
						}
						// aiEnter follows it.
						if (Engine::Math::DistanceSquared(moves[row].destination, there.XY()) > Fixed::FromInt(100) || moves[row].mode == gp::MoveMode::Idle)
							moves[row] = gp::Replanned(gp::MoveToPoint(there.XY(), gp::GoalClaim::None)); // AIEnterState: no adjusting, no claim
						continue;
					}
				}
				// PilotFindVehicleUpdate::update: an AI-only behaviour.
				if (!players.Computer(player) || tick < seeker.nextScan)
					continue;
				seeker.nextScan = tick + std::max<std::uint64_t>(seeker.scanTicks, 1);
				// AIUpdateInterface::isIdle.
				if (moves[row].mode != gp::MoveMode::Idle || (!targets.empty() && targets[row].target != ecs::Entity{}))
					continue;
				// scanClosestTarget: its player's vehicles within range (from their centres), nearest first.
				std::vector<std::pair<Fixed, ecs::Entity>> near;
				spatial.ForEachWithin(at.position.XY(), seeker.range, [&](const gp::SpatialEntry &entry) {
					const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), at.position.XY());
					if (entry.player == player && entry.entity != entities[row] && distance <= seeker.range * seeker.range)
						near.emplace_back(distance, entry.entity);
				});
				std::sort(near.begin(), near.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first < b.first : a.second.index < b.second.index; });
				for (const auto &[distance, vehicle] : near)
				{
					const auto *health = lookup.template Get<gp::Health>(vehicle);
					if (health == nullptr || health->current < health->maximum * seeker.minHealth || pilotAloft || !joinable(vehicle))
						continue;
					seeker.goal = vehicle;
					moves[row] = gp::Replanned(gp::MoveToPoint(lookup.template Get<gp::Transform>(vehicle)->position.XY(), gp::GoalClaim::None));
					break;
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::PilotSeekSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.pilot_seek";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the spatial index and before the veterancy pass.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
