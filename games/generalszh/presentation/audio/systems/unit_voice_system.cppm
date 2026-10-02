export module games.generalszh.presentation.audio.systems.unit_voice_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.core.world;
export import games.generalszh.commands.game_commands;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.resources.unit_voice_cues;
export import games.generalszh.presentation.interaction.algorithms.unit_voices;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
import games.generalszh.content.objects.object_status;
import Engine.Core.Math.FixedPresentation;

// pickAndPlayUnitVoiceResponse (CommandXlat.cpp), each frame, for what the player told the selection this frame
// (UnitVoiceCues, in order): the selection in InGameUI's order (the most recently selected first; for
// MSG_CREATE_SELECTED_GROUP only what the local player controls, nothing when that is none), what the original asks of
// each, the answer picked (PickUnitVoice) played on the one that says it (setObjectID: where it is, its player's), then the
// car bomb's line on it (TerroristInCar*Voice). The cues are used up.
export namespace generalszh::presentation
{
struct UnitVoiceSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::TeamMember>,
		ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::StatusFlags>, ecs::Read<engine::gameplay::Armament>,
		ecs::Read<engine::gameplay::WeaponSlots>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Locomotion>>;
	using SideTables = ecs::SideTables<ecs::Read<Selected>>;
	using Resources = ecs::Resources<ecs::Write<UnitVoiceCues>, ecs::Read<UnitVoiceSounds>, ecs::Read<LocalPlayer>,
		ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::PlayerUpgrades>,
		ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Write<SoundRequests>>;

	// Whether the definition has a special power module (not an update module) for a power of `type`, and its
	// InitiateSound (SpecialPowerModuleData::m_initiateSound).
	static std::optional<std::string_view> PowerModuleSound(const content::GameContent &game, const content::ObjectDefinition &definition,
		std::string_view type)
	{
		for (const content::ModuleEntry &module : definition.modules)
		{
			// The SpecialPowerModule kinds (not the updates and upgrades naming a power too).
			static constexpr std::array<std::string_view, 10> PowerModules{"BaikonurLaunchPower", "CashBountyPower", "CashHackSpecialPower",
				"CleanupAreaPower", "DefectorSpecialPower", "DemoralizeSpecialPower", "FireWeaponPower", "OCLSpecialPower", "SpecialAbility",
				"SpyVisionSpecialPower"};
			if (module.block == nullptr || std::ranges::find(PowerModules, std::string_view(module.type)) == PowerModules.end())
				continue;
			const auto *power = module.block->Find("SpecialPowerTemplate");
			if (power == nullptr || power->values.empty())
				continue;
			const std::string_view name = power->Value();
			const auto found = std::ranges::find_if(game.powers.templates, [&](const content::SpecialPowerTemplate &kind) { return kind.name == name; });
			if (found == game.powers.templates.end() || found->type != type)
				continue;
			const auto *sound = module.block->Find("InitiateSound");
			return sound != nullptr && !sound->values.empty() ? sound->Value() : std::string_view{};
		}
		return std::nullopt;
	}

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		auto &cues = context.Write<UnitVoiceCues>().pending;
		if (cues.empty())
			return;
		const auto lookup = context.Lookup<Lookup>();
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const content::GameContent &gameContent = templates.Content();
		const auto &relationships = context.Read<gp::Relationships>();
		const auto &upgrades = context.Read<gp::PlayerUpgrades>();
		const auto &weapons = context.Read<gp::WeaponCatalog>();
		const UnitVoiceSounds &misc = context.Read<UnitVoiceSounds>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		auto &sounds = context.Write<SoundRequests>().pending;
		// isValidAudioEvent: a named event the audio knows (no event is called NoSound).
		const auto valid = [](std::string_view name) { return !name.empty() && name != "NoSound"; };
		const std::uint64_t carBombBit = std::uint64_t{1} << content::ObjectStatusBit("IS_CARBOMB");
		const auto shoes = gameContent.upgrades.Find("Upgrade_GLAWorkerShoes");
		const content::WeaponContent *rocketPods = gameContent.weapons.Find("ComancheRocketPodWeapon");
		// InGameUI's selected list: drawables are put at its front as they are selected.
		const auto selection = context.SideRead<SideTables, Selected>().Entities();
		const std::vector<ecs::Entity> newestFirst(selection.rbegin(), selection.rend());
		for (UnitVoiceCue cue : cues)
		{
			// issueAttackCommand: m_air, the target using an airborne locomotor (isUsingAirborneLocomotor).
			if (cue.order == VoiceOrder::AttackObject && lookup.IsAlive(cue.target))
				if (const auto *motion = lookup.Get<gp::Locomotion>(cue.target))
					cue.air = gp::IsAirborne(motion->locomotor);
			const auto *targetRef = lookup.IsAlive(cue.target) ? lookup.Get<gp::DefinitionRef>(cue.target) : nullptr;
			VoiceTarget target;
			if (targetRef != nullptr)
			{
				const content::ObjectDefinition &kind = templates.DefinitionAt(targetRef->index);
				target = {true, kind.Is("HEAL_PAD"), kind.Is("STRUCTURE")};
			}
			const auto *targetOwner = targetRef != nullptr ? lookup.Get<gp::Owner>(cue.target) : nullptr;
			const auto *targetTeam = targetRef != nullptr ? lookup.Get<gp::TeamMember>(cue.target) : nullptr;
			std::string powerType;
			for (const content::SpecialPowerTemplate &kind : gameContent.powers.templates)
				if (!cue.specialPower.empty() && kind.name == cue.specialPower)
					powerType = kind.type;
			std::vector<VoiceUnit> units;
			for (const ecs::Entity entity : newestFirst)
			{
				const auto *ref = lookup.IsAlive(entity) ? lookup.Get<gp::DefinitionRef>(entity) : nullptr;
				if (ref == nullptr)
					continue;
				const auto *owner = lookup.Get<gp::Owner>(entity);
				// MSG_CREATE_SELECTED_GROUP: unit responses only for things we own (isLocallyControlled).
				if (cue.order == VoiceOrder::CreateGroup && (!local.valid || owner == nullptr || owner->player != local.player))
					continue;
				const content::ObjectDefinition &definition = templates.DefinitionAt(ref->index);
				VoiceUnit unit;
				unit.entity = entity;
				unit.definition = &definition;
				unit.ignoredInGui = definition.Is("IGNORED_IN_GUI");
				const auto *move = lookup.Get<gp::MoveOrder>(entity);
				unit.hasAi = move != nullptr;
				// isMoving() || isWaitingForPath(): under way somewhere (its route still to come counts).
				unit.effectivelyMoving = move != nullptr &&
					(move->mode == gp::MoveMode::Point || move->mode == gp::MoveMode::Path || move->mode == gp::MoveMode::Direct ||
						move->mode == gp::MoveMode::PathExact || gp::Wandering(move->mode) || move->mode == gp::MoveMode::WanderInPlace);
				unit.shoedWorker = definition.Is("INFANTRY") && definition.Is("DOZER") && definition.Is("HARVESTER") && owner != nullptr && shoes &&
					upgrades.Completed(owner->player).Has(*shoes);
				// getCurrentWeapon, or the info's slot's weapon.
				std::uint32_t weapon = gp::WeaponCatalog::None;
				std::uint8_t slot = 0;
				if (const auto *slots = lookup.Get<gp::WeaponSlots>(entity))
				{
					slot = cue.weaponSlot != UnitVoiceCue::NoSlot ? cue.weaponSlot : slots->current;
					if (slot < slots->slots.size())
						weapon = slots->slots[slot].weapon;
				}
				else if (const auto *armament = lookup.Get<gp::Armament>(entity); armament != nullptr && cue.weaponSlot == UnitVoiceCue::NoSlot)
					weapon = armament->weapon;
				if (weapon != gp::WeaponCatalog::None && weapon < weapons.Size())
				{
					unit.hasWeapon = true;
					unit.damageType = weapons.At(weapon).damageType;
					unit.weaponSlot = slot;
					unit.rocketPods = rocketPods != nullptr && templates.WeaponContentAt(weapon) == rocketPods;
				}
				if (targetRef != nullptr && owner != nullptr && targetOwner != nullptr)
				{
					const auto *team = lookup.Get<gp::TeamMember>(entity);
					unit.toTarget = relationships.Between(team != nullptr ? team->team : gp::Relationships::NoTeam, owner->player,
						targetTeam != nullptr ? targetTeam->team : gp::Relationships::NoTeam, targetOwner->player);
				}
				if (!powerType.empty())
					if (const auto initiate = PowerModuleSound(gameContent, definition, powerType))
					{
						unit.hasPowerModule = true;
						unit.powerInitiateSound = *initiate;
					}
				if (const auto *flags = lookup.Get<gp::StatusFlags>(entity))
					unit.carBomb = (flags->bits & carBombBit) != 0;
				units.push_back(unit);
			}
			if (units.empty())
				continue;
			const auto pick = PickUnitVoice(units, cue, target, valid);
			if (!pick)
				continue;
			const ecs::Entity speaker = units[pick->unit].entity;
			const auto *at = lookup.Get<gp::Transform>(speaker);
			const auto *owner = lookup.Get<gp::Owner>(speaker);
			if (at == nullptr)
				continue;
			const std::array<float, 3> where{Engine::Math::ToFloat(at->position.x), Engine::Math::ToFloat(at->position.y), Engine::Math::ToFloat(at->position.z)};
			const std::uint32_t player = owner != nullptr ? owner->player : SoundRequest::NoOwner;
			if (!pick->sound.empty())
				sounds.push_back({std::string(pick->sound), where, player});
			const std::string &extra = pick->carBomb == CarBombVoice::Attack ? misc.carBombAttack
				: pick->carBomb == CarBombVoice::Move                       ? misc.carBombMove
				: pick->carBomb == CarBombVoice::Select                     ? misc.carBombSelect
																			 : std::string{};
			if (pick->carBomb != CarBombVoice::None && !extra.empty())
				sounds.push_back({extra, where, player});
		}
		cues.clear();
	}
};
}

export namespace generalszh::presentation
{
// CommandTranslator's MSG_META_ALL_CHEER (in a network game; the caller decides): MiscAudio's AllCheerSound for the one at
// the keys (addAudioEvent: no place, no player), then MSG_DO_CHEER for the selection (the logic's selected group).
inline commands::Cheer AllCheer(ecs::World &world)
{
	if (auto *voices = world.FindResource<UnitVoiceSounds>(); voices != nullptr && !voices->allCheer.empty())
		if (auto *sounds = world.FindResource<SoundRequests>())
		{
			SoundRequest request;
			request.sound = voices->allCheer;
			request.positioned = false;
			sounds->pending.push_back(std::move(request));
		}
	const auto selected = world.Side<Selected>().Entities();
	return commands::Cheer{std::vector<ecs::Entity>(selected.begin(), selected.end())};
}
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::UnitVoiceSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.unit_voices";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
