export module games.generalszh.presentation.hud.systems.production_presentation_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.hud.algorithms.radar_event_rules;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.gameplay.production.resources.production_notices;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.rts.production.systems.production_system;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
import Engine.Core.Math.FixedPresentation;

// ProductionUpdate's feedback once a tick: the first unit of each production says its VoiceCreate where it is; an
// upgrade finished at the watcher's (one named for the screen) is told (UPGRADE:UpgradeComplete with its name) and
// flashes on the radar there, plays its ResearchSound at the building (without one EVA says it: EvaSystem), then its UnitSpecificSound there.
export namespace generalszh::presentation
{
struct ProductionPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::ProductionNotices>, ecs::Read<engine::gameplay::ProductionDone>,
		ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<PresentationFrame>, ecs::Write<InGameMessages>, ecs::Write<SoundRequests>,
		ecs::Read<AudioHandle>, ecs::Write<RadarEvents>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto lookup = context.Lookup<Lookup>();
		const auto &content = context.Read<generalszh::gameplay::ObjectTemplates>();
		auto &sounds = context.Write<SoundRequests>().pending;
		const auto where = [&](ecs::Entity entity) {
			const gp::Transform *at = lookup.Get<gp::Transform>(entity);
			return at != nullptr ? std::array<float, 3>{Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y), Engine::Math::ToFloat(at->position.z)}
								 : std::array<float, 3>{};
		};
		const auto ownerOf = [&](ecs::Entity entity) {
			const gp::Owner *owner = lookup.Get<gp::Owner>(entity);
			return owner != nullptr ? owner->player : SoundRequest::NoOwner;
		};
		for (const ecs::Entity made : context.Read<generalszh::gameplay::ProductionNotices>().created)
		{
			const gp::DefinitionRef *ref = lookup.IsAlive(made) ? lookup.Get<gp::DefinitionRef>(made) : nullptr;
			if (ref == nullptr)
				continue;
			if (const std::string_view voice = content.DefinitionAt(ref->index).Sound("VoiceCreate"); !voice.empty())
				sounds.push_back({std::string(voice), where(made), ownerOf(made)});
		}
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto &upgrades = content.Content().upgrades.upgrades;
		InGameMessages &messages = context.Write<InGameMessages>();
		const AudioHandle &audio = context.Read<AudioHandle>();
		const std::uint64_t tick = context.Tick();
		context.Read<gp::ProductionDone>().ForEach([&](const gp::Produced &done) {
			if (done.kind != gp::ProductionKind::Upgrade || done.definition >= upgrades.size() || !lookup.IsAlive(done.factory))
				return;
			const content::UpgradeContent &upgrade = upgrades[done.definition];
			if (ownerOf(done.factory) != viewer || upgrade.displayName.empty())
				return;
			const std::u16string name = done.definition < messages.upgradeNames.size() ? messages.upgradeNames[done.definition] : std::u16string{};
			AddMessage(messages, FormatWithName(messages.upgradeCompleteText, name), tick);
			// Upgrades are a rarer event: a radar event where it was made.
			CreateRadarEvent(context.Write<RadarEvents>(), where(done.factory), RadarEventType::Upgrade, tick);
			if (!upgrade.researchSound.empty() && audio.content != nullptr && audio.content->Find(upgrade.researchSound) != nullptr)
				sounds.push_back({upgrade.researchSound, where(done.factory), ownerOf(done.factory)});
			if (!upgrade.unitSpecificSound.empty())
				sounds.push_back({upgrade.unitSpecificSound, where(done.factory), ownerOf(done.factory)});
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ProductionPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.production";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
