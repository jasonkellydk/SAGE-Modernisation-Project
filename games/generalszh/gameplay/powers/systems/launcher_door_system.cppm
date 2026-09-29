export module games.generalszh.gameplay.powers.systems.launcher_door_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.powers.algorithms.launcher_doors;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.construction.components.under_construction;

// MissileLauncherBuildingUpdate::update, each tick for built superweapons (a batch: few of them; their effects go in
// one list): a timeout passed switches the door on; a door not open whose power is ready is popped open; a closed one
// starts opening DoorOpenTime before the power is ready (getReadyFrame: pushed back while paused or disabled).
export namespace generalszh::gameplay
{
struct LauncherDoorSystem
{
	using Query = ecs::Query<ecs::Write<LauncherDoor>, ecs::Read<engine::gameplay::SpecialPowerTimers>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Transform>, ecs::Optional<engine::gameplay::Disabled>,
		ecs::Exclude<engine::gameplay::UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpecialPowerRules>, ecs::Read<engine::gameplay::SharedPowerTimers>,
		ecs::Write<LauncherDoorEffects>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpecialPowerRules &rules = context.Read<gp::SpecialPowerRules>();
		const gp::SharedPowerTimers &shared = context.Read<gp::SharedPowerTimers>();
		LauncherDoorEffects &effects = context.Write<LauncherDoorEffects>();
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto doors = chunk.template Get<LauncherDoor>();
			const auto timerSets = chunk.template Get<gp::SpecialPowerTimers>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto disabledRows = chunk.template Get<gp::Disabled>();
			for (std::size_t row = 0; row < doors.size(); ++row)
			{
				const LauncherDoorConfig *config = templates.LauncherDoorOf(definitions[row].index);
				const gp::SpecialPowerTimer *timer = config != nullptr ? timerSets[row].Find(config->power) : nullptr;
				if (timer == nullptr)
					continue;
				const bool disabled = !disabledRows.empty() && disabledRows[row].mask != 0;
				const std::uint64_t ready = gp::PeekReadyFrame(*timer, disabled, rules, shared, owners[row].player, now);
				const std::uint64_t startOpening = ready >= config->openTicks ? ready - config->openTicks : 0;
				LauncherDoor &door = doors[row];
				const auto &at = transforms[row].position;
				if (door.timeoutTick != 0 && now > door.timeoutTick)
					SwitchLauncherDoor(door, *config, door.timeoutState, now, ready, at, &effects);
				if (door.state != LauncherDoorState::Open && gp::PeekIsReady(*timer, rules, shared, owners[row].player, now))
					SwitchLauncherDoor(door, *config, LauncherDoorState::Open, now, ready, at, &effects);
				else if (door.state == LauncherDoorState::Closed && now >= startOpening)
					SwitchLauncherDoor(door, *config, LauncherDoorState::Opening, now, ready, at, &effects);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::LauncherDoorSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.launcher_door";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
