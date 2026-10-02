export module games.generalszh.presentation.objects.systems.object_icon_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.rts.loadout.components.loadout;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.construction.components.sale;
export import games.generalszh.presentation.objects.components.object_icons;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.content.combat.weapon_bonus_content;
import games.generalszh.content.combat.loadout_content;

// Drawable::drawIconUI's animated icons, each drawn frame while icon UI is on and no script fade runs (the original
// asks only then, so nothing is made or freed otherwise), for everything alive and not IGNORED_IN_GUI (the dead and
// those keep what they had):
//   drawDisabled: hacked, paralyzed, EMP'd, subdued or underpowered, ICON_DISABLED; else it goes;
//   drawEnthusiastic: ENTHUSIASTIC, ICON_ENTHUSIASTIC_SUBLIMINAL when SUBLIMINAL too, else ICON_ENTHUSIASTIC; neither
//   goes only once it is no longer ENTHUSIASTIC;
//   drawBombed: its CARBOMB weapon set flag on and its player the viewer's, ICON_CARBOMB; else it goes;
//   drawHealing (not NO_HEAL_ICON, not sold): not whole, past the game's first 90 logic frames and healed within the
//   last 90 (HEALING_ICON_DISPLAY_TIME), the icon of its kind (STRUCTURE, VEHICLE, else the default); else that goes.
// An icon's animation starts when it is made (the side table keeps the clock then, and its client random draw for a
// RandomizeStartFrame animation's first image). Where they are drawn is
// object_icon_layout's; the host draws them with the health region.
export namespace generalszh::presentation
{
namespace object_icon_detail
{
inline constexpr std::uint32_t DisabledIconTypes = engine::gameplay::disabled_type::Hacked | engine::gameplay::disabled_type::Paralyzed |
	engine::gameplay::disabled_type::Emp | engine::gameplay::disabled_type::Subdued | engine::gameplay::disabled_type::Underpowered;
}

struct ObjectIconSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::Health>,
		ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::WeaponBonusConditions>, ecs::Optional<engine::gameplay::Loadout>,
		ecs::Optional<engine::gameplay::Dying>, ecs::Optional<engine::gameplay::Sale>>;
	using SideTables = ecs::SideTables<ecs::Write<ObjectIcons>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace object_icon_detail;
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		if (!frame.drawIconUi || frame.scriptFade)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &table = context.Side<SideTables, ObjectIcons>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto owners = chunk.Get<engine::gameplay::Owner>();
		const auto healths = chunk.Get<engine::gameplay::Health>();
		const auto disabled = chunk.Get<engine::gameplay::Disabled>();
		const auto bonuses = chunk.Get<engine::gameplay::WeaponBonusConditions>();
		const auto loadouts = chunk.Get<engine::gameplay::Loadout>();
		const auto dying = chunk.Get<engine::gameplay::Dying>();
		const auto sales = chunk.Get<engine::gameplay::Sale>();
		const auto entities = chunk.Entities();
		static const std::uint32_t carBomb = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
		for (std::size_t row = 0; row < entities.size(); ++row)
		{
			const DefinitionLooks *looks = catalog.Of(definitions[row].index);
			if (!dying.empty() || (!healths.empty() && engine::gameplay::IsDead(healths[row])) || (looks != nullptr && looks->ignoredInGui))
			{
				if (ObjectIcons *icons = table.Get(entities[row]))
					icons->drawn = 0;
				continue;
			}
			const std::uint32_t bonus = bonuses.empty() ? 0u : bonuses[row].flags;
			const bool off = !disabled.empty() && (disabled[row].mask & DisabledIconTypes) != 0;
			const bool enthusiastic = (bonus & content::weapon_bonus::Enthusiastic) != 0;
			const bool subliminal = enthusiastic && (bonus & content::weapon_bonus::Subliminal) != 0;
			const bool bomb = !loadouts.empty() && (loadouts[row].weaponFlags & carBomb) != 0 && !owners.empty() && owners[row].player == frame.viewer;
			// (A NO_HEAL_ICON or sold thing leaves its healing icons as they were.)
			const bool healable = looks == nullptr || !looks->noHealIcon;
			const bool sold = !sales.empty();
			const ObjectIcon healIcon = looks != nullptr && looks->structure ? ObjectIcon::StructureHeal
				: looks != nullptr && looks->vehicle ? ObjectIcon::VehicleHeal : ObjectIcon::DefaultHeal;
			constexpr std::uint64_t HealingIconTicks = 90; // LOGICFRAMES_PER_SECOND * 3
			const bool healing = healable && !sold && !healths.empty() && healths[row].current != healths[row].maximum && frame.tick > HealingIconTicks &&
				frame.tick - healths[row].lastHealingTick <= HealingIconTicks;
			ObjectIcons *icons = table.Get(entities[row]);
			if (icons == nullptr && !off && !enthusiastic && !bomb && !healing)
				continue;
			ObjectIcons next = icons != nullptr ? *icons : ObjectIcons{};
			next.drawn = 0;
			const auto keep = [&](ObjectIcon icon, bool shown) {
				double &since = next.since[static_cast<std::size_t>(icon)];
				if (!shown)
					since = -1.0;
				else
				{
					if (since < 0.0)
					{
						since = frame.clock;
						next.roll[static_cast<std::size_t>(icon)] = IconRoll(entities[row], icon, frame.clock);
					}
					next.drawn = static_cast<std::uint8_t>(next.drawn | 1u << static_cast<std::size_t>(icon));
				}
			};
			if (healable && !sold)
				keep(healIcon, healing);
			keep(ObjectIcon::Disabled, off);
			if (enthusiastic)
				keep(subliminal ? ObjectIcon::Subliminal : ObjectIcon::Enthusiastic, true);
			else
			{
				keep(ObjectIcon::Enthusiastic, false);
				keep(ObjectIcon::Subliminal, false);
			}
			keep(ObjectIcon::CarBomb, bomb);
			if (icons != nullptr)
				*icons = next;
			else
				context.Commands().Add<ObjectIcons>(entities[row], next);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ObjectIconSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_icons";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
