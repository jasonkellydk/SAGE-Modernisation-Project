export module games.generalszh.presentation.hud.systems.beacon_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.appearance.components.indicator_color;
export import games.generalszh.gameplay.beacons.resources.beacons;
export import games.generalszh.presentation.objects.components.beacon_look;
export import games.generalszh.presentation.objects.components.object_presentation;
export import engine.gameplay.common.spatial.components.object_shroud;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.hud.algorithms.radar_event_rules;
export import games.generalszh.presentation.hud.algorithms.eva_queue;
import engine.core.text.utf;
import engine.effects.particles.simulation.particle_world;
import Engine.Core.Math.FixedPresentation;

// Multiplayer beacons on this client, once a tick of the logic (BeaconSystem).
//   First the client side of GameLogic::onPlaceBeacon / onRemoveBeacon / onSetBeaconText for whoever
//     watches (the local player; NoViewer: an observer). A beacon placed by an ally of the watcher, or with an
//     observer watching: GUI:BeaconPlaced with its player's name and the BeaconPlaced sound there, an information
//     event on the radar where it stands, and EVA's BeaconDetected for a watcher allied to its player; placed by
//     anyone else: hidden from the watcher (hideBeacon). Too many up: GUI:TooManyBeacons and BeaconPlacementFailed, to
//     the player who tried. None to place (its side has none, or it was defeated): GUI:BeaconPlacementFailed and the
//     same sound; the original tells every client, which is a quirk (it has no local-player check where the other
//     two have one): here only the player who tried. Another's beacon told to go by the watcher: hidden from it. A
//     caption set: its text (empty: none).
//   Then BeaconClientUpdate::clientUpdate for each beacon: its smoke (BeaconSmoke<its indicator colour as
//     six hex digits>, riding on it; without that system the original makes BeaconSmokeFFFFFF tinted, but every
//     player colour and the white have their own system in the shipped data, so that fallback is left out and no
//     smoke shows), and while it shows, a beacon pulse on the radar (RADAR_EVENT_BEACON_PULSE lasting
//     RadarPulseDuration) once more than RadarPulseFrequency ticks have passed since the last (at first, since it was
//     first seen). hideBeacon stops its smoke.
export namespace generalszh::presentation
{
namespace beacon_detail
{
inline std::array<float, 3> ToFloats(const Engine::Math::FixedVector3 &at)
{
	return {Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
}
}

struct BeaconSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::IndicatorColor>, ecs::Read<engine::gameplay::ObjectShroud>>;
	using SideTables = ecs::SideTables<ecs::Write<BeaconLook>, ecs::Write<BeaconCaption>, ecs::Read<ShroudSight>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::BeaconCues>, ecs::Read<PresentationFrame>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Write<InGameMessages>, ecs::Write<SoundRequests>, ecs::Write<RadarEvents>, ecs::Write<EvaState>, ecs::Read<LookCatalog>,
		ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		// Beacons this client hides before it has seen them (placed by a non-ally this tick).
		std::vector<ecs::Entity> hiddenUnseen;
		Feedback(context, hiddenUnseen);
		const LookCatalog &catalog = context.Read<LookCatalog>();
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const auto lookup = context.Lookup<Lookup>();
		auto &looks = context.Side<SideTables, BeaconLook>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < definitions.size(); ++row)
			{
				const DefinitionLooks *definition = catalog.Of(definitions[row].index);
				if (definition == nullptr || !definition->beacon)
					continue;
				const ecs::Entity entity = entities[row];
				const auto at = beacon_detail::ToFloats(transforms[row].position);
				BeaconLook *look = looks.Get(entity);
				const auto unseen = std::ranges::find(hiddenUnseen, entity);
				BeaconLook fresh{0, tick, static_cast<std::uint8_t>(unseen != hiddenUnseen.end() ? 1 : 0), {}};
				if (unseen != hiddenUnseen.end())
					hiddenUnseen.erase(unseen);
				BeaconLook &state = look != nullptr ? *look : fresh;
				if (state.smoke == 0 && particles.world != nullptr && particles.content != nullptr)
				{
					// Object::getIndicatorColor: a colour a script gave it, else its player's.
					std::uint32_t rgb = 0;
					if (const auto *custom = lookup.Get<engine::gameplay::IndicatorColor>(entity); custom != nullptr && custom->argb != 0)
						rgb = custom->argb & 0xFFFFFFu;
					else
					{
						const auto color = catalog.ColorOf(owners[row].player);
						for (std::size_t channel = 0; channel < 3; ++channel)
							rgb = (rgb << 8) | static_cast<std::uint32_t>(std::lround(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f));
					}
					char name[24];
					std::snprintf(name, sizeof name, "BeaconSmoke%06X", rgb);
					if (const auto *system = particles.content->particles.Find(name))
					{
						state.smoke = particles.world->Create(*system, engine::effects::EmitterTransform::At(at[0], at[1], at[2], 0.0f));
						if (state.hidden != 0)
							particles.world->Stop(state.smoke);
					}
				}
				// The smoke rides the beacon's drawable (attachToDrawable): it emits nothing while the drawable is fully
				// obscured by the viewer's shroud (ParticleSystem::update's isShrouded: getFullyObscuredByShroud, fogged
				// or shrouded and more than 2 seconds since the viewer last saw it clear: GameClient::update).
				if (state.smoke != 0 && state.hidden == 0 && particles.world != nullptr)
				{
					const PresentationFrame &frame = context.Read<PresentationFrame>();
					const auto *shroud = lookup.Get<engine::gameplay::ObjectShroud>(entity);
					const ShroudSight *sight = context.SideRead<SideTables, ShroudSight>().Get(entity);
					const bool fogged = frame.viewer != PresentationFrame::NoViewer && frame.viewer < 64 && shroud != nullptr && !shroud->SeenBy(frame.viewer);
					const bool lingering = sight != nullptr && frame.clock < sight->lastClear + 2.0;
					particles.world->SetObscured(state.smoke, fogged && !lingering);
				}
				if (state.hidden == 0 && tick > state.lastPulse + definition->beaconPulseEvery)
				{
					CreateRadarEvent(context.Write<RadarEvents>(), at, RadarEventType::BeaconPulse, tick,
						static_cast<float>(definition->beaconPulseFor) * (1.0f / 30.0f));
					state.lastPulse = tick;
				}
				if (look == nullptr)
					commands.Add<BeaconLook>(entity, state);
			}
		});
		// Hidden before this client knew them as beacons: hidden when it does.
		std::ranges::sort(hiddenUnseen, {}, [](ecs::Entity entity) { return (std::uint64_t{entity.index} << 32) | entity.generation; });
		hiddenUnseen.erase(std::unique(hiddenUnseen.begin(), hiddenUnseen.end()), hiddenUnseen.end());
		for (const ecs::Entity entity : hiddenUnseen)
			if (looks.Get(entity) == nullptr)
				commands.Add<BeaconLook>(entity, BeaconLook{0, tick, 1, {}});
	}
private:
	static void Feedback(ecs::SystemContext &context, std::vector<ecs::Entity> &hiddenUnseen)
	{
		using generalszh::gameplay::BeaconCue;
		const auto &cues = context.Read<generalszh::gameplay::BeaconCues>();
		if (cues.list.empty())
			return;
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const bool observer = viewer == PresentationFrame::NoViewer;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const std::uint64_t tick = context.Tick();
		auto &looks = context.Side<SideTables, BeaconLook>();
		auto &captions = context.Side<SideTables, BeaconCaption>();
		auto &commands = context.Commands();
		const auto hide = [&](ecs::Entity beacon) {
			if (BeaconLook *look = looks.Get(beacon))
			{
				look->hidden = 1;
				ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
				if (look->smoke != 0 && particles.world != nullptr)
					particles.world->Stop(look->smoke);
			}
			else
				hiddenUnseen.push_back(beacon);
		};
		for (const BeaconCue &cue : cues.list)
		{
			const auto at = beacon_detail::ToFloats(cue.at);
			switch (cue.kind)
			{
			case BeaconCue::Kind::Placed:
				if (observer || relationships.Allies(cue.player, viewer))
				{
					InGameMessages &messages = context.Write<InGameMessages>();
					const std::u16string name = cue.player < messages.playerNames.size() ? messages.playerNames[cue.player] : std::u16string{};
					AddMessage(messages, FormatWithName(messages.beaconPlacedText, name), tick);
					context.Write<SoundRequests>().pending.push_back({"BeaconPlaced", at, cue.player});
					CreateRadarEvent(context.Write<RadarEvents>(), at, RadarEventType::Information, tick);
					if (!observer && relationships.Allies(viewer, cue.player))
						if (const auto message = content::EvaMessageOf("BEACONDETECTED"))
							AskEva(context.Write<EvaState>(), *message);
				}
				else
					hide(cue.beacon);
				break;
			case BeaconCue::Kind::TooMany:
			case BeaconCue::Kind::Failed:
				if (cue.player == viewer)
				{
					InGameMessages &messages = context.Write<InGameMessages>();
					AddMessage(messages, cue.kind == BeaconCue::Kind::TooMany ? messages.tooManyBeaconsText : messages.beaconFailedText, tick);
					context.Write<SoundRequests>().pending.push_back({"BeaconPlacementFailed", at, cue.player});
				}
				break;
			case BeaconCue::Kind::Hidden:
				if (cue.player == viewer)
					hide(cue.beacon);
				break;
			case BeaconCue::Kind::Text:
				if (cue.text.empty())
				{
					if (captions.Get(cue.beacon) != nullptr)
						commands.Remove<BeaconCaption>(cue.beacon);
				}
				else if (BeaconCaption *caption = captions.Get(cue.beacon))
					caption->text = engine::core::text::FromUtf8(cue.text);
				else
					commands.Add<BeaconCaption>(cue.beacon, BeaconCaption{engine::core::text::FromUtf8(cue.text)});
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::BeaconSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.beacons";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
