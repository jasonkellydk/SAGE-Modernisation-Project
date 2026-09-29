export module games.generalszh.presentation.hud.systems.radar_event_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.radar_event_rules;
export import games.generalszh.presentation.hud.algorithms.eva_queue;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.death.components.dying;
export import games.generalszh.gameplay.eva.resources.eva_notices;
export import games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import Engine.Core.Math.FixedPresentation;

// The radar once a tick of the logic (Radar::update: events past their time go), and Object::attemptDamage's
// tryUnderAttackEvent: real damage (not PENALTY or HEALING) to something of the watcher's on the radar, from another
// player, not NO_ATTACK_WARNING, tries an under attack event where it is; made, the watcher hears of it: an infantry or
// vehicle (a harvester its own message and sound; the others the structure sound, as the retail code has it), a
// victory-counting structure (EVA: base under attack; an ally's: ally under attack), anything else. Radar::
// tryInfiltrationEvent: an infiltration of the watcher's (a capture begun, a defection) is an infiltration event where it
// is, with its message and sound. BattlePlanUpdate::setStatus: a plan unpacking is a battle plan event on its player's radar
// where the center is, and its message (TheInGameUI->message: shown whoever's center it is).
export namespace generalszh::presentation
{
struct RadarEventSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Hits>, ecs::Write<RadarEvents>, ecs::Read<RadarFeedback>, ecs::Read<PresentationFrame>,
		ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<engine::gameplay::Relationships>, ecs::Write<InGameMessages>, ecs::Write<AudioCommands>,
		ecs::Write<EvaState>, ecs::Read<generalszh::gameplay::InfiltrationNotices>, ecs::Read<generalszh::gameplay::BattlePlanCues>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		RadarEvents &radar = context.Write<RadarEvents>();
		const std::uint64_t frame = context.Tick();
		UpdateRadarEvents(radar, frame);
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		if (viewer == PresentationFrame::NoViewer)
			return;
		const RadarFeedback &feedback = context.Read<RadarFeedback>();
		for (const generalszh::gameplay::InfiltrationNotice &notice : context.Read<generalszh::gameplay::InfiltrationNotices>().list)
		{
			if (notice.player != viewer)
				continue;
			CreateRadarEvent(radar, {Engine::Math::ToFloat(notice.position.x), Engine::Math::ToFloat(notice.position.y), Engine::Math::ToFloat(notice.position.z)},
				RadarEventType::Infiltration, frame);
			AddMessage(context.Write<InGameMessages>(), feedback.infiltration, frame);
			if (!feedback.infiltrationSound.empty())
				context.Write<AudioCommands>().pending.push_back({AudioCommand::Kind::Interface, feedback.infiltrationSound});
		}
		for (const generalszh::gameplay::BattlePlanCue &cue : context.Read<generalszh::gameplay::BattlePlanCues>().list)
		{
			if (cue.kind != generalszh::gameplay::BattlePlanCue::Kind::Unpack || cue.plan == generalszh::gameplay::PlanStatus::None)
				continue;
			if (cue.player == viewer)
				CreateRadarEvent(radar, {Engine::Math::ToFloat(cue.at.x), Engine::Math::ToFloat(cue.at.y), Engine::Math::ToFloat(cue.at.z)},
					RadarEventType::BattlePlan, frame);
			if (cue.definition < feedback.battlePlanMessages.size())
				if (const std::u16string &text = feedback.battlePlanMessages[cue.definition][static_cast<std::size_t>(cue.plan) - 1]; !text.empty())
					AddMessage(context.Write<InGameMessages>(), text, frame);
		}
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const auto lookup = context.Lookup<Lookup>();
		std::vector<gp::Hit> hits;
		context.Read<gp::Hits>().AppendTo(hits);
		for (const gp::Hit &hit : hits)
		{
			if (hit.amount <= Engine::Math::Fixed{} || hit.damageType == feedback.penaltyDamage || hit.damageType == feedback.healingDamage)
				continue;
			if (!lookup.IsAlive(hit.target) || lookup.Get<gp::Dying>(hit.target) != nullptr)
				continue;
			const gp::Owner *owner = lookup.Get<gp::Owner>(hit.target);
			const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(hit.target);
			if (owner == nullptr || ref == nullptr || owner->player != viewer)
				continue;
			// From another player (the damage's source player mask: its source's player while it is there).
			const gp::Owner *source = lookup.IsAlive(hit.source) ? lookup.Get<gp::Owner>(hit.source) : nullptr;
			if (source != nullptr && source->player == owner->player)
				continue;
			if (ref->index >= feedback.onRadar.size() || !feedback.onRadar[ref->index])
				continue;
			const content::ObjectDefinition &kind = templates.DefinitionAt(ref->index);
			if (kind.Is("NO_ATTACK_WARNING"))
				continue;
			const gp::Transform *at = lookup.Get<gp::Transform>(hit.target);
			const std::array<float, 3> where = at != nullptr
				? std::array<float, 3>{Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y), Engine::Math::ToFloat(at->position.z)}
				: std::array<float, 3>{};
			if (!TryRadarEvent(radar, RadarEventType::UnderAttack, where, frame))
				continue;
			InGameMessages &messages = context.Write<InGameMessages>();
			auto &audio = context.Write<AudioCommands>().pending;
			const auto say = [&](const std::u16string &text, const std::string &sound) {
				AddMessage(messages, text, frame);
				if (!sound.empty())
					audio.push_back({AudioCommand::Kind::Interface, sound});
			};
			if (kind.Is("INFANTRY") || kind.Is("VEHICLE"))
			{
				if (kind.Is("HARVESTER"))
					say(feedback.harvesterUnderAttack, feedback.harvesterSound);
				else
					say(feedback.unitUnderAttack, feedback.structureSound);
			}
			else if (kind.Is("STRUCTURE") && kind.Is("MP_COUNT_FOR_VICTORY"))
			{
				// Its own (it is: the watcher's) says base under attack.
				AskEva(context.Write<EvaState>(), *content::EvaMessageOf("BASEUNDERATTACK"));
				say(feedback.structureUnderAttack, feedback.structureSound);
			}
			else
				say(feedback.underAttack, feedback.structureSound);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RadarEventSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_events";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
