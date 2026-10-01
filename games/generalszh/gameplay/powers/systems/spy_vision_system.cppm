export module games.generalszh.gameplay.powers.systems.spy_vision_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.powers.components.spy_vision;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.death.components.dying;

// SpyVisionUpdate for every object with one, each tick, module by module, in the original's order of events:
// - onDisabledEdge (any disabling): turned off while disabled (setDisabledUntilFrame(forever): its timers start again
//   once it is not: a self-powered one turns on at once when it has no interval, else after one);
// - onCapture: its spying moves from its old player to its new one;
// - onDelete (here as it dies): its spying ends;
// - activateSpyVision (an upgrade, or its SpyVisionSpecialPower): on for the time asked (0: for good), waking then;
// - update, when it wakes: after a reset, a self-powered one with no interval turns on for good, else waits an
//   interval; on and due off: off; off and self-powered: on for SelfPoweredDuration (0: for good); a self-powered one
//   then sleeps for its duration while on, its interval while off.
// Turning on or off (doActivationWork) goes out as SpyVisionEvents, applied after the step to every enemy player's
// things of its kinds. One retail quirk is fixed: a disabling's end no longer turns on a module still waiting for its
// upgrade (the Internet Center's satellite hacks spied after an EMP without them).
export namespace generalszh::gameplay
{
struct SpyVisionSystem
{
	using Query = ecs::Query<ecs::Write<SpyVision>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<SpyVisionEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<SpyVisionEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		constexpr std::uint64_t Never = SpyVisionState::Never;
		const SpyVisionCatalog &catalog = context.Read<ObjectTemplates>().spyVisions;
		auto &out = context.Write<SpyVisionEvents>().Slot(context);
		auto spies = chunk.Get<SpyVision>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto definitions = chunk.Get<gp::DefinitionRef>();
		const auto disabled = chunk.Get<gp::Disabled>();
		const auto dying = chunk.Get<gp::Dying>();
		const auto entities = chunk.Entities();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < spies.size(); ++row)
		{
			SpyVision &spy = spies[row];
			const std::uint32_t definition = definitions[row].index;
			const auto work = [&](std::uint8_t module, std::uint32_t player, bool setting) {
				out.push_back({entities[row], player, definition, module, static_cast<std::uint8_t>(setting ? 1 : 0)});
				spy.modules[module].active = setting ? 1 : 0;
			};
			// onDelete: its spying ends (once).
			if (!dying.empty())
			{
				for (std::uint8_t module = 0; module < spy.count; ++module)
					if (spy.modules[module].active != 0)
						work(module, spy.player, false);
				continue;
			}
			// onCapture.
			if (owners[row].player != spy.player)
			{
				for (std::uint8_t module = 0; module < spy.count; ++module)
					if (spy.modules[module].active != 0)
					{
						work(module, spy.player, false);
						work(module, owners[row].player, true);
					}
				spy.player = owners[row].player;
			}
			// setDisabledUntilFrame asked for (a sabotaged Internet Center: disableInternetCenterSpyVision): a later tick
			// turns it off until then, its timers starting again when it wakes.
			if (spy.disableUntil != 0)
			{
				const std::uint64_t until = spy.disableUntil;
				spy.disableUntil = 0;
				for (std::uint8_t module = 0; module < spy.count; ++module)
				{
					SpyVisionState &state = spy.modules[module];
					if (until > now)
					{
						if (state.active != 0)
							work(module, spy.player, false);
						state.disabledUntil = until;
						state.wakeTick = until;
					}
					else
					{
						state.disabledUntil = now;
						state.wakeTick = now;
					}
					state.resetTimers = 1;
				}
			}
			// onDisabledEdge -> setDisabledUntilFrame.
			const bool isDisabled = !disabled.empty() && disabled[row].mask != 0;
			if (isDisabled != (spy.wasDisabled != 0))
			{
				spy.wasDisabled = isDisabled ? 1 : 0;
				for (std::uint8_t module = 0; module < spy.count; ++module)
				{
					SpyVisionState &state = spy.modules[module];
					if (isDisabled)
					{
						if (state.active != 0)
							work(module, spy.player, false);
						state.disabledUntil = Never;
						state.resetTimers = 1;
						state.wakeTick = Never;
					}
					else
					{
						state.disabledUntil = now;
						state.resetTimers = 1;
						state.wakeTick = now + 1;
					}
				}
			}
			for (std::uint8_t module = 0; module < spy.count; ++module)
			{
				SpyVisionState &state = spy.modules[module];
				const SpyVisionConfig *config = catalog.Of(definition, module);
				if (config == nullptr)
					continue;
				// activateSpyVision (asked for Never: its SelfPoweredDuration, an upgrade's turn-on).
				if (state.activateAsked != 0)
				{
					state.activateAsked = 0;
					if (state.activateTicks == Never)
						state.activateTicks = config->durationTicks;
					state.deactivateTick = state.activateTicks == 0 ? Never : now + state.activateTicks;
					work(module, spy.player, true);
					state.wakeTick = state.activateTicks == 0 ? Never : now + state.activateTicks;
					continue;
				}
				if (state.wakeTick == Never || now < state.wakeTick)
					continue;
				// update.
				if (state.resetTimers != 0)
				{
					state.resetTimers = 0;
					if (config->selfPowered && (!config->needsUpgrade || state.upgraded != 0))
					{
						if (config->intervalTicks == 0)
						{
							work(module, spy.player, true);
							state.wakeTick = Never;
						}
						else
							state.wakeTick = now + config->intervalTicks;
						continue;
					}
				}
				const bool allowed = !config->needsUpgrade || state.upgraded != 0;
				if (state.active != 0 && state.deactivateTick <= now)
				{
					work(module, spy.player, false);
					state.deactivateTick = 0;
				}
				else if (state.active == 0 && config->selfPowered && allowed)
				{
					work(module, spy.player, true);
					state.deactivateTick = config->durationTicks == 0 ? Never : now + config->durationTicks;
				}
				if (config->selfPowered && allowed)
				{
					const std::uint64_t sleep = state.active != 0 ? config->durationTicks : config->intervalTicks;
					state.wakeTick = sleep == 0 ? Never : now + sleep;
				}
				else
					state.wakeTick = Never;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::SpyVisionSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spy_vision";
	// After the tick's upgrade effects (an upgrade's turn-on is carried out in its tick).
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
