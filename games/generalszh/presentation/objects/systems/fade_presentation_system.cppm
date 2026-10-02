export module games.generalszh.presentation.objects.systems.fade_presentation_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.owner;
export import games.generalszh.gameplay.effects.resources.effect_cues;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.resources.presentation_resources;
import Engine.Core.Math.FixedPresentation;

// Drawable::fadeIn / fadeOut asked for by the logic (an object creation list's FadeIn / FadeOut: the Rebel Ambush's rebels
// fading in), once a tick: the object's fade starts now (ObjectFade), and its FadeSound plays on the source.
export namespace generalszh::presentation
{
// Drawable::updateDrawable's fade: opacity elapsed / time fading in, (time - elapsed) / time fading out, in frames (here
// the presentation's real time at 30 frames a second), held at its end.
inline float FadeOpacity(const ObjectFade &fade, double clock) noexcept
{
	if (fade.frames <= 0.0f)
		return fade.in != 0 ? 1.0f : 0.0f;
	const float done = std::clamp(static_cast<float>((clock - fade.start) * 30.0) / fade.frames, 0.0f, 1.0f);
	return fade.in != 0 ? done : 1.0f - done;
}

struct FadePresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using SideTables = ecs::SideTables<ecs::Write<ObjectFade>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::EffectCues>, ecs::Read<PresentationFrame>, ecs::Write<SoundRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto &cues = context.Read<generalszh::gameplay::EffectCues>().list;
		const double clock = context.Read<PresentationFrame>().clock;
		auto &fades = context.Side<SideTables, ObjectFade>();
		auto &sounds = context.Write<SoundRequests>().pending;
		for (const generalszh::gameplay::EffectCue &cue : cues)
		{
			if (cue.fade == 0)
				continue;
			ObjectFade *fade = fades.Get(cue.on);
			if (fade == nullptr)
				fade = fades.Emplace(cue.on);
			*fade = ObjectFade{clock, static_cast<float>(cue.fadeTicks), static_cast<std::uint8_t>(cue.fade == 1 ? 1 : 0)};
			if (!cue.effect.empty() && cue.effect != "NoSound")
				sounds.push_back({cue.effect, {Engine::Math::ToFloat(cue.at.x), Engine::Math::ToFloat(cue.at.y), Engine::Math::ToFloat(cue.at.z)}});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::FadePresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_fades";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
