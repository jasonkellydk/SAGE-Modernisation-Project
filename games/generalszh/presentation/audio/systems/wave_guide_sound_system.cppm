export module games.generalszh.presentation.audio.systems.wave_guide_sound_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.core.component_registry;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import games.generalszh.gameplay.waveguide.components.wave_guide;
export import games.generalszh.gameplay.waveguide.resources.wave_guide_events;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// A flood wave's sounds (WaveGuideUpdate, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/WaveGuideUpdate.cpp):
//   - its LoopingSound (startMoving: addAudioEvent with the wave's object id), a side table on the wave's own entity:
//     started once, as the wave sets off, sounding where the wave is each frame until the wave goes (the audio of a gone
//     object stops: the side table's release); never started again;
//   - its RandomSplashSound (update: AudioEventRTS with the wave's object id), a one-off where the wave was, for each
//     splash cue of the tick, once however many frames show the tick (WaveGuideSoundsPlayed).
export namespace generalszh::presentation
{
struct WaveGuideSoundLoop
{
	std::uint64_t handle{0};
	std::uint32_t started{0};
	std::uint32_t reserved{0};
};

struct WaveGuideSoundsPlayed
{
	std::uint64_t tick{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::WaveGuideSoundLoop>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_sound_loop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};

template<>
struct ResourceTraits<generalszh::presentation::WaveGuideSoundsPlayed>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_sounds_played";
};
}

export namespace generalszh::presentation
{
struct WaveGuideSoundSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::WaveGuide>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<WaveGuideSoundLoop>>;
	using Resources = ecs::Resources<ecs::Read<AudioState>, ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>, ecs::Read<gameplay::WaveGuideCues>,
		ecs::Write<WaveGuideSoundsPlayed>, ecs::Write<AudioHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &loops = context.Side<SideTables, WaveGuideSoundLoop>();
		const auto where = [](const Engine::Math::FixedVector3 &at) {
			return engine::audio::Vec3{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
		};
		query.ForEachChunk([&](auto chunk) {
			const auto guides = chunk.template Get<gameplay::WaveGuide>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < guides.size(); ++row)
			{
				if (guides[row].initialized == 0)
					continue;
				const engine::audio::Vec3 at = where(transforms[row].position);
				WaveGuideSoundLoop *loop = loops.Get(entities[row]);
				if (loop == nullptr)
					loop = loops.Emplace(entities[row]);
				if (loop->started == 0)
				{
					loop->started = 1;
					if (const DefinitionLooks *looks = catalog.Of(definitions[row].index))
						if (const auto *sound = AllowedSound(content, state, looks->waveLoopingSound))
							loop->handle = PlaySound(player, state, *sound, at);
				}
				else if (loop->handle != 0 && player.Playing(loop->handle))
					player.Move(loop->handle, at);
			}
		});
		WaveGuideSoundsPlayed &played = context.Write<WaveGuideSoundsPlayed>();
		const std::uint64_t tick = context.Read<PresentationFrame>().tick;
		if (played.tick == tick)
			return;
		played.tick = tick;
		for (const gameplay::WaveGuideCue &cue : context.Read<gameplay::WaveGuideCues>().list)
			if (cue.kind == gameplay::WaveGuideCue::Kind::Splash)
				if (const DefinitionLooks *looks = catalog.Of(cue.definition))
					if (const auto *sound = AllowedSound(content, state, looks->waveSplashSound))
						PlaySound(player, state, *sound, where(cue.at));
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::WaveGuideSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
