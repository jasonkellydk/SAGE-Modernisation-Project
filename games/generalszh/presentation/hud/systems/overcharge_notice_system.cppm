export module games.generalszh.presentation.hud.systems.overcharge_notice_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.hud.algorithms.radar_event_rules;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.rts.economy.resources.overcharge_events;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.transform;
import Engine.Core.Math.FixedPresentation;

// OverchargeBehavior::update, once a tick: an overcharge of the watcher's (isLocallyControlled) that ran out says so
// (GUI:OverchargeExhausted) and flashes on the radar where it is (RADAR_EVENT_INFORMATION).
export namespace generalszh::presentation
{
struct OverchargeNoticeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::OverchargeEvents>, ecs::Read<PresentationFrame>, ecs::Write<InGameMessages>,
		ecs::Write<RadarEvents>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const std::uint64_t tick = context.Tick();
		context.Read<gp::OverchargeEvents>().ForEach([&](const gp::OverchargeExhausted &event) {
			const gp::Owner *owner = lookup.IsAlive(event.entity) ? lookup.Get<gp::Owner>(event.entity) : nullptr;
			const gp::Transform *at = lookup.IsAlive(event.entity) ? lookup.Get<gp::Transform>(event.entity) : nullptr;
			if (owner == nullptr || at == nullptr || owner->player != viewer)
				return;
			InGameMessages &messages = context.Write<InGameMessages>();
			AddMessage(messages, messages.overchargeExhaustedText, tick);
			CreateRadarEvent(context.Write<RadarEvents>(),
				{Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y), Engine::Math::ToFloat(at->position.z)},
				RadarEventType::Information, tick);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::OverchargeNoticeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.overcharge_notices";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
