export module games.generalszh.presentation.hud.systems.military_caption_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.military_caption_typing;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import engine.gameplay.common.identity.components.owner;

// Once a tick: the military caption types on (InGameUI::update), each letter with the MilitarySubtitlesTyping sound.
export namespace generalszh::presentation
{
struct MilitaryCaptionSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Write<MilitaryCaption>, ecs::Write<AudioCommands>>;

	void Execute(ecs::SystemContext &context) const
	{
		if (StepCaption(context.Write<MilitaryCaption>(), context.Tick()))
			context.Write<AudioCommands>().pending.push_back({AudioCommand::Kind::Interface, "MilitarySubtitlesTyping"});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MilitaryCaptionSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.military_caption";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
