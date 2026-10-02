export module games.generalszh.gameplay.combat.systems.pilot_kill_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.gameplay.combat.resources.unmanned_notices;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.content.combat.loadout_content;

// ActiveBody::attemptDamage's DAMAGE_KILLPILOT within the step, where the original does it (the hit's own frame): each
// living VEHICLE it hit this tick (in the order dealt; a rider-change container's rider is not ported) is set
// DISABLED_UNMANNED (Object::setDisabled); one not a DRONE whose template has a CARBOMB weapon set (findWeaponTemplateSet
// finds it) is killed outright there and then, dying with this tick's dead. What follows the disable outside the step
// (the rest of setDisabled, aiIdle, setTeam) is ApplyPilotKills'.
export namespace generalszh::gameplay
{
namespace pilot_kill_detail
{
inline bool HasCarBombSet(const content::ObjectDefinition &definition)
{
	const std::uint32_t carBomb = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
	const content::ObjectLoadout loadout = content::ReadObjectLoadout(definition);
	return std::ranges::any_of(loadout.weaponSets, [&](const content::WeaponSetContent &set) { return (set.conditions & carBomb) != 0; });
}

// Whether a KILL_PILOT hit takes this one's pilot: a VEHICLE without a rider-change container.
inline bool LosesPilot(const content::ObjectDefinition &definition)
{
	return definition.Is("VEHICLE") &&
		std::ranges::none_of(definition.modules, [](const content::ModuleEntry &module) { return module.type == "RiderChangeContain"; });
}
}

struct PilotKillSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Health>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Disabled>, ecs::Read<engine::gameplay::Dying>,
		ecs::Read<engine::gameplay::Health>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Hits>, ecs::Read<ObjectTemplates>, ecs::Write<engine::gameplay::KillRequests>,
		ecs::Write<UnmannedNotices>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto killPilot = content::DamageTypeIndex("KILL_PILOT");
		if (!killPilot)
			return;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		auto &kills = context.Write<gp::KillRequests>().entities;
		auto &unmanned = context.Write<UnmannedNotices>().list;
		std::vector<ecs::Entity> done;
		context.Read<gp::Hits>().ForEach([&](const gp::Hit &hit) {
			if (!hit.handled || hit.damageType != *killPilot || !lookup.IsAlive(hit.target) || lookup.Get<gp::Dying>(hit.target) != nullptr)
				return;
			if (const gp::Health *health = lookup.Get<gp::Health>(hit.target); health != nullptr && health->current <= Engine::Math::Fixed{})
				return;
			const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(hit.target);
			if (ref == nullptr)
				return;
			const content::ObjectDefinition &definition = templates.DefinitionAt(ref->index);
			if (!pilot_kill_detail::LosesPilot(definition))
				return;
			// Each hit's setDisabled(DISABLED_UNMANNED), its pilot's splatter (a second hit's too).
			unmanned.push_back({hit.target, ref->index});
			if (std::ranges::find(done, hit.target) != done.end())
				return;
			done.push_back(hit.target);
			const gp::Disabled *off = lookup.Get<gp::Disabled>(hit.target);
			if (off != nullptr)
				commands.Set<gp::Disabled>(hit.target, gp::Disabled{off->mask | gp::disabled_type::Unmanned});
			else
				commands.Add<gp::Disabled>(hit.target, gp::Disabled{gp::disabled_type::Unmanned});
			if (!definition.Is("DRONE") && pilot_kill_detail::HasCarBombSet(definition))
				kills.push_back(hit.target);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::PilotKillSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.pilot_kills";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::HealthSystem>;
};
}
