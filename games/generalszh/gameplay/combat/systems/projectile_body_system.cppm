export module games.generalszh.gameplay.combat.systems.projectile_body_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.systems.missile_flight_system;
export import engine.gameplay.rts.combat.systems.projectile_flight_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.rts.death.components.dying;
export import games.generalszh.gameplay.objects.resources.object_templates;

// A missile in flight is an object with a body, as in the original: from the
// tick after it is launched it can be found (its kinds: SMALL_MISSILE, ...),
// hurt (its ActiveBody and armor) and killed by its die modules (a point
// defense laser's LASERED death disintegrates it); one with a SubdualDamageCap can be jammed.
export namespace generalszh::gameplay
{
struct ProjectileBodySystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::MissileFlight>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::Health>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < definitions.size(); ++row)
			if (const ObjectTemplates::ProjectileBody *body = templates.BodyOf(definitions[row].index))
			{
				commands.Add<engine::gameplay::Targetable>(entities[row], engine::gameplay::Targetable{body->radius, body->classes});
				commands.Add<engine::gameplay::Health>(entities[row], engine::gameplay::Health{body->health, body->health, body->armor});
				commands.Add<engine::gameplay::Mortality>(entities[row], engine::gameplay::Mortality{templates.DeathOf(definitions[row].index)});
				if (body->subdualCap > Engine::Math::Fixed{})
				{
					engine::gameplay::Subdual subdual;
					subdual.cap = body->subdualCap;
					subdual.healAmount = body->subdualHealAmount;
					subdual.healTicks = body->subdualHealTicks;
					commands.Add<engine::gameplay::Subdual>(entities[row], subdual);
				}
			}
	}
};
}

namespace generalszh::gameplay
{
// A lobbed shell that dies as it goes off (DumbProjectileBehavior DetonateCallsKill) is an object with a body and die
// modules too, from the tick after it is fired: its ActiveBody's health and its death (FireWeaponWhenDeadBehavior: a
// Scorpion's toxin shells). Others go away as they go off and need none.
export struct ShellBodySystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::ProjectileFlight>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::Health>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto flights = chunk.Get<engine::gameplay::ProjectileFlight>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < definitions.size(); ++row)
			if (flights[row].arc.callsKill != 0)
				if (const ObjectTemplates::ProjectileBody *body = templates.BodyOf(definitions[row].index))
				{
					commands.Add<engine::gameplay::Health>(entities[row], engine::gameplay::Health{body->health, body->health, body->armor});
					commands.Add<engine::gameplay::Mortality>(entities[row], engine::gameplay::Mortality{templates.DeathOf(definitions[row].index)});
				}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ShellBodySystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.shell_bodies";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::ProjectileFlightSystem>;
};

template<>
struct SystemTraits<generalszh::gameplay::ProjectileBodySystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.projectile_bodies";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's missiles flew (it only reads them).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MissileFlightSystem>;
};
}
