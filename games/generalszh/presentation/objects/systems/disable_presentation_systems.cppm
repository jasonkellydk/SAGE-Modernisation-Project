export module games.generalszh.presentation.objects.systems.disable_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.weapons.components.temp_weapon_bonus;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.containment.components.mount;
export import engine.gameplay.rts.emp.resources.emp_strikes;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.components.effect_attachments;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.presentation.objects.algorithms.emp_sparks;
export import games.generalszh.presentation.objects.algorithms.tint_envelope;
import Engine.Core.Math.FixedPresentation;

// Disabled things heard and seen, once a tick after the simulation:
//   DisabledSoundSystem (Object::setDisabledUntil): a structure or vehicle that loses its power (underpowered, EMP,
//   subdued or hacked, when none of those held it yet) plays MiscAudio's BuildingDisabled or VehicleDisabled where it is;
//   anything but a drone left unmanned (its pilot sniped) plays SplatterVehiclePilotsBrain there;
//   EmpSparkSystem (EMPUpdate::doDisableAttack): each victim an EMP pulse's sphere disabled crackles with the pulse's
//   DisableFXParticleSystem, EmpSparkCount emitters riding on it at EmpSparkOffset, each starting 1 to 100 frames on and
//   running for the disable less a second (DisabledDuration - 30 frames, at least none: the original's unsigned
//   subtraction would wrap for a shorter disable); an aircraft it killed is painted black for good (setTintStatus);
//   TintStatusSystem, each drawn frame (Drawable::updateDrawable): a thing disabled by anything but being held,
//   script-disabled or unmanned (Object::setDisabledUntil / clearDisabled: TINT_STATUS_DISABLED) fades to dark grey
//   (-0.5 each channel) over 30 logic frames and holds it; once that ends it fades back over 30; stepped by the logic
//   frames the drawn frame covers (at most 1); its selection flash steps alongside (m_selectionFlashEnvelope). Not so
//   darkened, a thing gaining subdual damage (TINT_STATUS_GAINING_SUBDUAL_DAMAGE) fades to SUBDUAL_DAMAGE_COLOR
//   (-0.2, -0.2, 0.8) over 150 frames and holds it, fading back over 150 once it stops.
//   RiderTintSystem, each drawn frame after the tints stepped (W3DOverlordAircraftDraw / W3DOverlordTankDraw /
//   W3DOverlordTruckDraw::doDrawModule): a portable structure mounted on a carrier drawn by one of those takes the
//   carrier's tint (Drawable::setColorTintEnvelope: copied whole, and only onto a rider that has a tint of its own).
export namespace generalszh::presentation
{
inline constexpr std::uint32_t PowerLossTypes = engine::gameplay::disabled_type::Underpowered | engine::gameplay::disabled_type::Emp |
	engine::gameplay::disabled_type::Subdued | engine::gameplay::disabled_type::Hacked;

struct RiderTintSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Mounted>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<TintEnvelope>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &tints = context.Side<SideTables, TintEnvelope>();
		if (tints.Size() == 0)
			return;
		const auto lookup = context.Lookup<Lookup>();
		query.ForEachChunk([&](auto chunk) {
			const auto mounts = chunk.template Get<engine::gameplay::Mounted>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < entities.size(); ++row)
			{
				const ecs::Entity carrier = mounts[row].carrier;
				const auto *definition = lookup.IsAlive(carrier) ? lookup.Get<engine::gameplay::DefinitionRef>(carrier) : nullptr;
				const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
				if (looks == nullptr || !looks->ridersTakeTint)
					continue;
				const TintEnvelope *from = tints.Get(carrier);
				TintEnvelope *onto = tints.Get(entities[row]);
				if (from != nullptr && onto != nullptr)
					*onto = *from;
			}
		});
	}
};

struct DisabledSoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Disabled>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Write<DisableHeard>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Write<SoundRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &sounds = context.Write<SoundRequests>().pending;
		auto &heard = context.Side<SideTables, DisableHeard>();
		query.ForEachChunk([&](auto chunk) {
			const auto disabled = chunk.template Get<engine::gameplay::Disabled>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < disabled.size(); ++row)
			{
				constexpr std::uint32_t Unmanned = engine::gameplay::disabled_type::Unmanned;
				const std::uint32_t now = disabled[row].mask & (PowerLossTypes | Unmanned);
				DisableHeard *seen = heard.Get(entities[row]);
				const std::uint32_t before = seen != nullptr ? seen->mask : 0u;
				if (seen != nullptr)
					seen->mask = now;
				else if (now != 0)
					context.Commands().Add<DisableHeard>(entities[row], DisableHeard{now});
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr)
					continue;
				const auto &at = transforms[row].position;
				const std::array<float, 3> where{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
				if ((now & Unmanned) != 0 && (before & Unmanned) == 0 && !looks->drone && !catalog.pilotSplatterSound.empty())
					sounds.push_back({catalog.pilotSplatterSound, where});
				// Object::clearDisabled: the last of those taken away, it plays BuildingReenabled or VehicleReenabled.
				if ((before & PowerLossTypes) != 0 && (now & PowerLossTypes) == 0)
				{
					const std::string &back = looks->structure ? catalog.buildingReenabledSound : looks->vehicle ? catalog.vehicleReenabledSound : std::string{};
					if (!back.empty())
						sounds.push_back({back, where});
					continue;
				}
				if ((before & PowerLossTypes) != 0 || (now & PowerLossTypes) == 0)
					continue;
				const std::string &sound = looks->structure ? catalog.buildingDisabledSound : looks->vehicle ? catalog.vehicleDisabledSound : std::string{};
				if (sound.empty())
					continue;
				sounds.push_back({sound, where});
			}
		});
	}
};

inline constexpr std::uint32_t UntintedDisables =
	engine::gameplay::disabled_type::Held | engine::gameplay::disabled_type::ScriptDisabled | engine::gameplay::disabled_type::Unmanned;
inline constexpr std::array<float, 3> DarkGrayDisabledColor{-0.5f, -0.5f, -0.5f};
inline constexpr std::array<float, 3> SubdualDamageColor{-0.2f, -0.2f, 0.8f};
// Drawable.cpp FRENZY_COLOR and FRENZY_COLOR_INFANTRY.
inline constexpr std::array<float, 3> FrenzyColor{0.2f, -0.2f, -0.2f};
inline constexpr std::array<float, 3> FrenzyInfantryColor{0.0f, -0.7f, -0.7f};

struct TintStatusSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::Subdual>,
		ecs::Optional<engine::gameplay::TempWeaponBonus>>;
	using SideTables = ecs::SideTables<ecs::Write<TintEnvelope>, ecs::Write<SelectionFlash>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const float timeScale = std::min(1.0f, context.Read<PresentationFrame>().seconds * 30.0f);
		auto &tints = context.Side<SideTables, TintEnvelope>();
		auto &flashes = context.Side<SideTables, SelectionFlash>();
		const auto disabled = chunk.Get<engine::gameplay::Disabled>();
		const auto subduals = chunk.Get<engine::gameplay::Subdual>();
		const auto frenzies = chunk.Get<engine::gameplay::TempWeaponBonus>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto frenzyColor = [&](std::size_t row) {
			const DefinitionLooks *looks = catalog.Of(refs[row].index);
			return looks != nullptr && looks->infantry ? FrenzyInfantryColor : FrenzyColor;
		};
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
		{
			if (SelectionFlash *flash = flashes.Get(entities[row]))
				StepTint(flash->envelope, timeScale);
			const std::uint32_t mask = disabled.empty() ? 0u : disabled[row].mask & engine::gameplay::disabled_type::All;
			const bool darkened = (mask & ~UntintedDisables) != 0;
			const bool gaining = !subduals.empty() && subduals[row].gaining != 0;
			// TINT_STATUS_FRENZY: while a temporary weapon bonus holds (TempWeaponBonusHelper).
			const bool frenzied = !frenzies.empty() && frenzies[row].bit != 0;
			TintEnvelope *tint = tints.Get(entities[row]);
			if (tint == nullptr)
			{
				if (!darkened && !gaining && !frenzied)
					continue;
				TintEnvelope fresh;
				fresh.status = darkened ? 1 : gaining ? 2 : 3;
				if (darkened)
					PlayTint(fresh, DarkGrayDisabledColor, 30, 30, static_cast<float>(0xFFFFFFFEu));
				else if (gaining)
					PlayTint(fresh, SubdualDamageColor, 150, 150, static_cast<float>(0xFFFFFFFEu));
				else
					PlayTint(fresh, frenzyColor(row), 30, 30, static_cast<float>(0xFFFFFFFEu));
				StepTint(fresh, timeScale);
				context.Commands().Add<TintEnvelope>(entities[row], fresh);
				continue;
			}
			const std::uint32_t status = darkened || tint->forced != 0 ? 1u : gaining ? 2u : frenzied ? 3u : 0u;
			if (status != tint->status)
			{
				if (status == 1)
					PlayTint(*tint, DarkGrayDisabledColor, 30, 30, static_cast<float>(0xFFFFFFFEu));
				else if (status == 2)
					PlayTint(*tint, SubdualDamageColor, 150, 150, static_cast<float>(0xFFFFFFFEu));
				else if (status == 3)
					PlayTint(*tint, frenzyColor(row), 30, 30, static_cast<float>(0xFFFFFFFEu));
				else
					ReleaseTint(*tint);
				tint->status = status;
			}
			StepTint(*tint, timeScale);
		}
	}
};

struct EmpSparkSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Write<FxEmission>, ecs::Write<TintEnvelope>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::EmpStrikes>, ecs::Read<LookCatalog>, ecs::Write<ParticleWorldHandle>,
		ecs::Write<PresentationRandom>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto &tints = context.Side<SideTables, TintEnvelope>();
		for (const ecs::Entity aircraft : context.Read<engine::gameplay::EmpStrikes>().blackened)
		{
			if (!lookup.IsAlive(aircraft))
				continue;
			if (TintEnvelope *tint = tints.Get(aircraft))
				tint->forced = 1;
			else
			{
				TintEnvelope blackened;
				blackened.forced = 1;
				context.Commands().Add<TintEnvelope>(aircraft, blackened);
			}
		}
		const auto &strikes = context.Read<engine::gameplay::EmpStrikes>().list;
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (strikes.empty() || particles.world == nullptr || particles.content == nullptr)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &random = context.Write<PresentationRandom>().engine;
		auto &riding = context.Side<SideTables, FxEmission>();
		std::vector<std::pair<ecs::Entity, std::vector<AttachedSystem>>> added; // to objects without any yet
		for (const engine::gameplay::EmpStrike &strike : strikes)
		{
			const auto *pulseRef = lookup.IsAlive(strike.pulse) ? lookup.Get<engine::gameplay::DefinitionRef>(strike.pulse) : nullptr;
			const auto *victimRef = lookup.IsAlive(strike.victim) ? lookup.Get<engine::gameplay::DefinitionRef>(strike.victim) : nullptr;
			const auto *transform = lookup.IsAlive(strike.victim) ? lookup.Get<engine::gameplay::Transform>(strike.victim) : nullptr;
			const DefinitionLooks *pulse = pulseRef != nullptr ? catalog.Of(pulseRef->index) : nullptr;
			const DefinitionLooks *victim = victimRef != nullptr ? catalog.Of(victimRef->index) : nullptr;
			if (pulse == nullptr || victim == nullptr || transform == nullptr || pulse->empSparks.empty())
				continue;
			const engine::effects::ParticleSystemDefinition *definition = particles.content->particles.Find(pulse->empSparks);
			if (definition == nullptr)
				continue;
			const float facing = static_cast<float>(transform->facing.units) * (6.283185307179586f / 4294967296.0f);
			const float c = std::cos(facing), s = std::sin(facing);
			const std::array<float, 3> at{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z)};
			const auto lifetime = static_cast<std::uint32_t>(strike.duration > 30 ? strike.duration - 30 : 0);
			std::vector<AttachedSystem> systems;
			for (std::uint32_t emitter = 0, count = EmpSparkCount(pulse->empSparksPerCubicFoot, *victim); emitter < count; ++emitter)
			{
				const std::array<float, 3> offset = EmpSparkOffset(*victim, random);
				const auto id = particles.world->Create(*definition,
					engine::effects::EmitterTransform::At(at[0] + offset[0] * c - offset[1] * s, at[1] + offset[0] * s + offset[1] * c, at[2] + offset[2], facing));
				particles.world->SetSystemLifetime(id, lifetime);
				particles.world->SetInitialDelay(id, static_cast<std::uint32_t>(std::uniform_int_distribution<int>(1, 100)(random)));
				systems.push_back({id, offset, 0.0f});
			}
			if (FxEmission *emission = riding.Get(strike.victim))
				emission->systems.insert(emission->systems.end(), systems.begin(), systems.end());
			else
			{
				auto found = std::find_if(added.begin(), added.end(), [&](const auto &entry) { return entry.first == strike.victim; });
				auto &list = found != added.end() ? found->second : added.emplace_back(strike.victim, std::vector<AttachedSystem>{}).second;
				list.insert(list.end(), systems.begin(), systems.end());
			}
		}
		for (auto &[object, systems] : added)
			context.Commands().Add<FxEmission>(object, FxEmission{std::move(systems), 0});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::DisabledSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.disabled_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::RiderTintSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rider_tint";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::TintStatusSystem>;
};
template<>
struct SystemTraits<generalszh::presentation::TintStatusSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tint_status";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::EmpSparkSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.emp_sparks";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
