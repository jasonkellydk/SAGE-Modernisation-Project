export module games.generalszh.gameplay.containment.systems.heal_seek_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.containment.components.heal_seeker;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.parachute.components.parachute;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
import games.generalszh.content.objects.kind_of;

// A computer player's units going to be healed on their own (AutoFindHealingUpdate::update), each tick: human players'
// units never do. Otherwise the countdown to the next look runs down a tick at a time; once out it starts again at
// ScanRate and the unit looks, if it is no healthier than NeverHeal of its maximum and idle (AIUpdateInterface::isIdle):
// the nearest HEAL_PAD within ScanRange from its centre, anyone's (scanClosestTarget: the nearest, ties in entity order).
// It goes there to be healed (aiGetHealed -> privateGetHealed: aiEnter) when it may be healed there
// (ActionManager::canGetHealedAt: an ally's pad that stands, is not being built or sold; an infantry unit that is not
// being built and is not whole).
export namespace generalszh::gameplay
{
struct HealSeekSystem
{
	using Query = ecs::Query<ecs::Write<HealSeeker>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>,
		ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::AttackTarget>, ecs::Optional<engine::gameplay::Boarding>, ecs::Optional<engine::gameplay::UnderConstruction>,
		ecs::Exclude<engine::gameplay::OffMap>, ecs::Exclude<engine::gameplay::Dying>, ecs::Exclude<engine::gameplay::ParachuteRider>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Health>,
		ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::Sale>,
		ecs::Read<engine::gameplay::Transport>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::HarvestCatalog>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::HarvestCatalog &players = context.Read<gp::HarvestCatalog>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		static constexpr std::size_t HealPadBit = content::KindOfBit("HEAL_PAD");
		static constexpr std::size_t InfantryBit = content::KindOfBit("INFANTRY");
		query.ForEachChunk([&](auto chunk) {
			auto seekers = chunk.template Get<HealSeeker>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto healths = chunk.template Get<gp::Health>();
			const auto moves = chunk.template Get<gp::MoveOrder>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			const auto targets = chunk.template Get<gp::AttackTarget>();
			const auto boardings = chunk.template Get<gp::Boarding>();
			const auto building = chunk.template Get<gp::UnderConstruction>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < seekers.size(); ++row)
			{
				const std::uint32_t player = owners[row].player;
				if (!players.Computer(player))
					continue;
				HealSeeker &seeker = seekers[row];
				if (seeker.countdown > 0)
				{
					--seeker.countdown;
					continue;
				}
				seeker.countdown = seeker.scanTicks;
				const gp::Health &health = healths[row];
				if (health.current > health.maximum * seeker.neverHeal)
					continue;
				// AIUpdateInterface::isIdle: not moving, attacking or entering anything.
				if (moves[row].mode != gp::MoveMode::Idle || (!targets.empty() && targets[row].target != ecs::Entity{}) || !boardings.empty())
					continue;
				// scanClosestTarget: the first nearest heal pad within range (from centres, 2D).
				const auto &at = transforms[row].position;
				std::optional<std::pair<Fixed, ecs::Entity>> best;
				spatial.ForEachWithin(at.XY(), seeker.range, [&](const gp::SpatialEntry &entry) {
					const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), at.XY());
					if (entry.entity == entities[row] || distance > seeker.range * seeker.range)
						return;
					const auto *definition = lookup.template Get<gp::DefinitionRef>(entry.entity);
					if (definition == nullptr || !content::HasKindOf(templates.DefinitionAt(definition->index).kinds, HealPadBit))
						return;
					if (!best || distance < best->first || (distance == best->first && entry.entity.index < best->second.index))
						best.emplace(distance, entry.entity);
				});
				if (!best)
					continue;
				const ecs::Entity pad = best->second;
				// ActionManager::canGetHealedAt.
				const auto *padOwner = lookup.template Get<gp::Owner>(pad);
				if (padOwner == nullptr || !relationships.Allies(player, padOwner->player))
					continue;
				if (const auto *padHealth = lookup.template Get<gp::Health>(pad);
					lookup.template Get<gp::Dying>(pad) != nullptr || (padHealth != nullptr && gp::IsDead(*padHealth)))
					continue;
				if (!building.empty() || lookup.template Get<gp::UnderConstruction>(pad) != nullptr || lookup.template Get<gp::Sale>(pad) != nullptr)
					continue;
				if (!content::HasKindOf(templates.DefinitionAt(definitions[row].index).kinds, InfantryBit) || health.current == health.maximum)
					continue;
				if (lookup.template Get<gp::Transport>(pad) == nullptr)
					continue;
				commands.Add<gp::Boarding>(entities[row], gp::Boarding{pad});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::HealSeekSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.heal_seek";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the spatial index.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
