export module games.generalszh.presentation.hud.systems.rally_notice_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.gameplay.production.resources.rally_notices;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedPresentation;

// doSetRallyPoint's feedback, to the player who set it (isLocallyControlled): set, GUI:RallyPointSet with the factory's
// name and the RallyPointSet sound where it is; refused (no path), GUI:RallyPointNoPath and UnableToSetRallyPoint there.
export namespace generalszh::presentation
{
struct RallyNoticeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::RallyNotices>, ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<PresentationFrame>,
		ecs::Write<InGameMessages>, ecs::Write<SoundRequests>>;

	void Execute(ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const std::uint64_t tick = context.Tick();
		for (const generalszh::gameplay::RallyNotice &notice : context.Read<generalszh::gameplay::RallyNotices>().list)
		{
			if (notice.player != viewer)
				continue;
			InGameMessages &messages = context.Write<InGameMessages>();
			const std::array<float, 3> at{Engine::Math::ToFloat(notice.at.x), Engine::Math::ToFloat(notice.at.y), 0.0f};
			if (!notice.set)
			{
				AddMessage(messages, messages.rallyNoPathText, tick);
				context.Write<SoundRequests>().pending.push_back({"UnableToSetRallyPoint", at, notice.player});
				continue;
			}
			std::u16string name;
			if (const auto *ref = lookup.IsAlive(notice.factory) ? lookup.Get<engine::gameplay::DefinitionRef>(notice.factory) : nullptr)
				if (const auto found = messages.displayNames.find(templates.DefinitionAt(ref->index).displayName); found != messages.displayNames.end())
					name = found->second;
			AddMessage(messages, FormatWithName(messages.rallySetText, name), tick);
			context.Write<SoundRequests>().pending.push_back({"RallyPointSet", at, notice.player});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RallyNoticeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rally_notices";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
