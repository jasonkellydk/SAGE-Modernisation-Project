export module games.generalszh.presentation.objects.systems.ability_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.stealth.components.undetected_defector;
export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.gameplay.abilities.resources.ability_notices;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.algorithms.tint_envelope;
export import games.generalszh.presentation.objects.algorithms.flash_phases;
import games.generalszh.content.powers.special_ability_content;
import Engine.Core.Math.FixedPresentation;

// Abilities, powers and defections seen and heard, once a tick after the simulation:
//   sounds where the unit is (SpecialAbilityUpdate: UnpackSound, PackSound, TriggerSound; packing after its effect, its
//   task-complete voice: the Black Lotus' capture, vehicle hack and cash hack their own; PrepSoundLoop from its
//   preparation to its end), a power's InitiateSound on its source and InitiateAtLocationSound where it lands
//   (SpecialPowerModule::aboutToDoSpecialPower), a defector's VoiceDefect, flash and timer tick (Object::defect);
//   a capture's flashes (SpecialAbilityUpdate::continuePreparation, DoCaptureFX): while it prepares its phase grows by a
//   third of how far along it is, and each time it leaves an odd whole number the target flashes in the capturer's colour
//   saturated by SelectionFlashSaturationFactor, with the defector timer's tick;
//   a defector's cover (ObjectDefectionHelper::update): each tick its phase grows by half of how far into the ten second
//   most its cover is, flashing (flashAsSelected: its player's colour, or white, saturated) with a tick as it leaves an odd
//   whole number; its time up, it flashes white with the timer's ding.
export namespace generalszh::presentation
{
namespace ability_presentation_detail
{
namespace gp = engine::gameplay;

inline std::array<float, 3> At(const gp::Transform &transform)
{
	return {Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y), Engine::Math::ToFloat(transform.position.z)};
}

// Drawable::saturateRGB.
inline std::array<float, 3> Saturated(std::array<float, 3> color, float factor)
{
	for (float &channel : color)
		channel = channel * factor - factor * 0.5f;
	return color;
}

// Drawable::flashAsSelected: the selection flash plays the colour (no attack, a four frame decay); a thing without one
// yet gets it (in `added`, given to it once the tick's flashes are in).
template<typename Flashes>
inline void Flash(Flashes &flashes, std::vector<std::pair<ecs::Entity, SelectionFlash>> &added, ecs::Entity entity, const std::array<float, 3> &color)
{
	SelectionFlash *flash = flashes.Get(entity);
	for (auto &[who, fresh] : added)
		if (flash == nullptr && who == entity)
			flash = &fresh;
	if (flash == nullptr)
		flash = &added.emplace_back(entity, SelectionFlash{}).second;
	PlayTint(flash->envelope, color, 0, 4, 0.0f);
}

// flashAsSelected without a colour: its player's (SelectionFlashHouseColor) or white, saturated.
inline std::array<float, 3> OwnFlashColor(const LookCatalog &catalog, std::uint32_t player)
{
	std::array<float, 3> color{1.0f, 1.0f, 1.0f};
	if (catalog.selectionFlashHouseColor)
	{
		const auto house = catalog.ColorOf(player);
		color = {house[0], house[1], house[2]};
	}
	return Saturated(color, catalog.selectionFlashSaturation);
}
}

struct AbilityFeedbackSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::SpecialAbilities>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::UndetectedDefector>>;
	using SideTables = ecs::SideTables<ecs::Write<SelectionFlash>, ecs::Write<CaptureFlash>, ecs::Write<DefectorFlash>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::AbilityNotices>, ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<LookCatalog>,
		ecs::Write<SoundRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		namespace ga = generalszh::gameplay;
		using namespace ability_presentation_detail;
		const auto lookup = context.Lookup<Lookup>();
		const auto &templates = context.Read<ga::ObjectTemplates>();
		const content::GameContent &content = templates.Content();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &sounds = context.Write<SoundRequests>().pending;
		auto &flashes = context.Side<SideTables, SelectionFlash>();
		auto &captures = context.Side<SideTables, CaptureFlash>();
		auto &defectors = context.Side<SideTables, DefectorFlash>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		std::vector<std::pair<ecs::Entity, SelectionFlash>> added;
		const auto play = [&](std::string_view sound, ecs::Entity entity) {
			const auto *at = lookup.IsAlive(entity) ? lookup.Get<gp::Transform>(entity) : nullptr;
			if (sound.empty() || sound == "NoSound" || at == nullptr)
				return;
			const auto *owner = lookup.Get<gp::Owner>(entity);
			sounds.push_back({std::string(sound), At(*at), owner != nullptr ? owner->player : SoundRequest::NoOwner});
		};
		const ga::AbilityNotices &notices = context.Read<ga::AbilityNotices>();
		// The unit's SpecialAbilityUpdate for the power (its sounds).
		const auto moduleOf = [&](ecs::Entity unit, std::uint32_t power) -> std::optional<content::SpecialAbilityContent> {
			const auto *ref = lookup.IsAlive(unit) ? lookup.Get<gp::DefinitionRef>(unit) : nullptr;
			if (ref == nullptr || power >= content.powers.templates.size())
				return std::nullopt;
			for (content::SpecialAbilityContent &module : content::ReadSpecialAbilities(templates.DefinitionAt(ref->index), templates.Step()))
				if (module.power == content.powers.templates[power].name)
					return std::move(module);
			return std::nullopt;
		};
		for (const ga::AbilityNotice &notice : notices.abilities)
		{
			if (notice.cue == ga::AbilityCue::PreparationStart || notice.cue == ga::AbilityCue::PreparationEnd)
				continue; // PrepSoundLoop: no ported ability has one yet
			const auto module = moduleOf(notice.unit, notice.power);
			if (!module)
				continue;
			switch (notice.cue)
			{
			case ga::AbilityCue::Unpack: play(module->unpackSound, notice.unit); break;
			case ga::AbilityCue::Trigger: play(module->triggerSound, notice.unit); break;
			case ga::AbilityCue::Pack:
			{
				play(module->packSound, notice.unit);
				if (!notice.success)
					break;
				const auto *ref = lookup.Get<gp::DefinitionRef>(notice.unit);
				const content::ObjectDefinition &unit = templates.DefinitionAt(ref->index);
				const std::string &type = content.powers.templates[notice.power].type;
				const std::string_view voice = type == "SPECIAL_BLACKLOTUS_CAPTURE_BUILDING" ? unit.Sound("VoiceCaptureBuildingComplete")
					: type == "SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK"                   ? unit.Sound("VoiceDisableVehicleComplete")
					: type == "SPECIAL_BLACKLOTUS_STEAL_CASH_HACK"                        ? unit.Sound("VoiceStealCashComplete")
																						  : unit.Sound("VoiceTaskComplete");
				play(voice, notice.unit);
				break;
			}
			default: break;
			}
		}
		for (const ga::PowerTrigger &power : notices.powers)
		{
			if (power.power >= content.powers.templates.size())
				continue;
			const content::SpecialPowerTemplate &kind = content.powers.templates[power.power];
			play(kind.initiateSound, power.source);
			if (power.at && !kind.initiateAtLocationSound.empty() && kind.initiateAtLocationSound != "NoSound")
				sounds.push_back({kind.initiateAtLocationSound, {Engine::Math::ToFloat(power.at->x), Engine::Math::ToFloat(power.at->y), Engine::Math::ToFloat(power.at->z)},
					power.player});
		}
		// ConvertToHijackedVehicleCrateCollide: HijackDriver on the hijacker.
		for (const ecs::Entity hijacker : notices.hijacks)
			if (lookup.IsAlive(hijacker))
				play("HijackDriver", hijacker);
		// CrateCollide::doSabotageFeedbackFX: its MiscAudio sound at the building, and the building flashes as selected.
		for (const ga::AbilityNotices::Sabotage &sabotage : notices.sabotages)
		{
			using Sound = ga::AbilityNotices::Sabotage::Sound;
			if (!lookup.IsAlive(sabotage.building))
				continue;
			const std::string_view field = sabotage.sound == Sound::ResetTimer ? "SabotageResetTimeBuilding"
				: sabotage.sound == Sound::Withdraw ? "MoneyWithdrawSound" : "SabotageShutDownBuilding";
			if (const auto found = content.miscAudio.find(field); found != content.miscAudio.end())
				play(found->second, sabotage.building);
			const auto *owner = lookup.Get<gp::Owner>(sabotage.building);
			Flash(flashes, added, sabotage.building, OwnFlashColor(catalog, owner != nullptr ? owner->player : 0u));
		}
		for (const ecs::Entity defector : notices.defected)
		{
			if (!lookup.IsAlive(defector))
				continue;
			// Its timer's effects from the next tick (startDefectionTimer).
			if (const auto *cover = lookup.Get<gp::UndetectedDefector>(defector); cover != nullptr && cover->fx != 0 && defectors.Get(defector) == nullptr)
				commands.Add<DefectorFlash>(defector, DefectorFlash{0.0f, 0u, cover->until});
			if (const auto *ref = lookup.Get<gp::DefinitionRef>(defector))
				play(templates.DefinitionAt(ref->index).Sound("VoiceDefect"), defector);
			const auto *owner = lookup.Get<gp::Owner>(defector);
			Flash(flashes, added, defector, OwnFlashColor(catalog, owner != nullptr ? owner->player : 0u));
			play(catalog.defectorTickSound, defector);
		}
		// Captures under way: the targets flash as the capturers' phases turn over.
		query.ForEachChunk([&](auto chunk) {
			const auto abilities = chunk.template Get<ga::SpecialAbilities>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < abilities.size(); ++row)
			{
				const ga::SpecialAbilities &own = abilities[row];
				CaptureFlash *phases = nullptr;
				for (std::uint8_t index = 0; index < own.count; ++index)
				{
					const ga::AbilitySlot &slot = own.slots[index];
					const bool capture = slot.kind == ga::AbilityKind::InfantryCaptureBuilding || slot.kind == ga::AbilityKind::BlackLotusCaptureBuilding;
					// continuePreparation ran: active, preparing, past its first preparing tick.
					if (!capture || !slot.Option(ga::ability_option::DoCaptureFx) || !slot.Has(ga::ability_flag::Active) || slot.prepTicks == 0 ||
						slot.prepTicks >= slot.preparationTicks || !lookup.IsAlive(slot.target))
						continue;
					if (phases == nullptr)
					{
						phases = captures.Get(entities[row]);
						if (phases == nullptr)
						{
							commands.Add<CaptureFlash>(entities[row], CaptureFlash{});
							break; // from its next tick
						}
					}
					if (StepCaptureFlash(phases->phase[index], slot.prepTicks, slot.preparationTicks))
					{
						const auto house = catalog.ColorOf(owners[row].player);
						Flash(flashes, added, slot.target, Saturated({house[0], house[1], house[2]}, catalog.selectionFlashSaturation));
						play(catalog.defectorTickSound, slot.target);
					}
				}
			}
		});
		// Undetected defectors' timers (ObjectDefectionHelper::update): covered, the flash phases; the cover gone, when its
		// time ran out (not ended by firing or death), a white flash and the ding.
		std::vector<ecs::Entity> ended;
		defectors.ForEach([&](ecs::Entity entity, DefectorFlash &flash) {
			const auto *cover = lookup.IsAlive(entity) ? lookup.Get<gp::UndetectedDefector>(entity) : nullptr;
			if (cover == nullptr || tick >= cover->until)
			{
				ended.push_back(entity);
				if (lookup.IsAlive(entity) && tick >= flash.until)
				{
					Flash(flashes, added, entity, {1.0f, 1.0f, 1.0f});
					play(catalog.defectorDingSound, entity);
				}
				return;
			}
			flash.until = cover->until;
			if (StepDefectorFlash(flash.phase, cover->until - tick))
			{
				const auto *owner = lookup.Get<gp::Owner>(entity);
				Flash(flashes, added, entity, OwnFlashColor(catalog, owner != nullptr ? owner->player : 0u));
				play(catalog.defectorTickSound, entity);
			}
		});
		for (const ecs::Entity entity : ended)
			commands.Remove<DefectorFlash>(entity);
		for (const auto &[entity, flash] : added)
			commands.Add<SelectionFlash>(entity, flash);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::AbilityFeedbackSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.ability_feedback";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
