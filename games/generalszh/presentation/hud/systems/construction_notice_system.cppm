export module games.generalszh.presentation.hud.systems.construction_notice_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.hud.algorithms.radar_event_rules;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.rts.construction.resources.sales;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
import Engine.Core.Math.FixedPresentation;

// DozerActionDoActionState's feedback once a tick:
// - DOZER_TASK_BUILD finished by a builder of the watcher's (isLocallyControlled): DOZER:ConstructionComplete with the
//   structure's name (none: INI:MissingDisplayName with its template name), the builder's VoiceTaskComplete where it
//   is, and a construction event on the radar where the structure stands (RADAR_EVENT_CONSTRUCTION);
// - DOZER_TASK_REPAIR found whole: DOZER:RepairComplete. The original (InGameUI::message, no isLocallyControlled test)
//   showed every builder's on every screen, the enemy's too; that retail quirk is fixed: only the builder's player is
//   told, as for construction.
export namespace generalszh::presentation
{
struct ConstructionNoticeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::ConstructionsDone>, ecs::Read<generalszh::gameplay::ObjectTemplates>,
		ecs::Read<PresentationFrame>, ecs::Write<InGameMessages>, ecs::Write<SoundRequests>, ecs::Write<RadarEvents>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto lookup = context.Lookup<Lookup>();
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const std::uint64_t tick = context.Tick();
		const auto &done = context.Read<gp::ConstructionsDone>();
		const auto where = [&](ecs::Entity entity) {
			const gp::Transform *at = lookup.Get<gp::Transform>(entity);
			return at != nullptr ? std::array<float, 3>{Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y), Engine::Math::ToFloat(at->position.z)}
								 : std::array<float, 3>{};
		};
		const auto mine = [&](ecs::Entity builder) {
			const gp::Owner *owner = lookup.IsAlive(builder) ? lookup.Get<gp::Owner>(builder) : nullptr;
			return owner != nullptr && owner->player == viewer;
		};
		for (const gp::ConstructionDone &built : done.list)
		{
			if (!mine(built.builder))
				continue;
			InGameMessages &messages = context.Write<InGameMessages>();
			std::u16string name;
			if (const auto *ref = lookup.IsAlive(built.structure) ? lookup.Get<gp::DefinitionRef>(built.structure) : nullptr)
			{
				const auto &definition = templates.DefinitionAt(ref->index);
				if (const auto found = messages.displayNames.find(definition.displayName); !definition.displayName.empty() && found != messages.displayNames.end())
					name = found->second;
				else
					name = FormatWithName(messages.missingDisplayNameText, std::u16string(definition.name.begin(), definition.name.end()));
			}
			AddMessage(messages, FormatWithName(messages.constructionCompleteText, name), tick);
			if (const auto *ref = lookup.Get<gp::DefinitionRef>(built.builder))
				if (const std::string_view voice = templates.DefinitionAt(ref->index).Sound("VoiceTaskComplete"); !voice.empty())
					context.Write<SoundRequests>().pending.push_back({std::string(voice), where(built.builder), viewer});
			CreateRadarEvent(context.Write<RadarEvents>(), where(built.structure), RadarEventType::Construction, tick);
		}
		for (const gp::RepairDone &repaired : done.repairs)
			if (mine(repaired.builder))
			{
				InGameMessages &messages = context.Write<InGameMessages>();
				AddMessage(messages, messages.repairCompleteText, tick);
			}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ConstructionNoticeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.construction_notices";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
