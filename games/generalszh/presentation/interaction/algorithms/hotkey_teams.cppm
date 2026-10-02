export module games.generalszh.presentation.interaction.algorithms.hotkey_teams;
import std;

export import engine.ecs.core.world;
export import games.generalszh.commands.game_commands;
export import games.generalszh.gameplay.orders.resources.hotkey_squads;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.resources.unit_voice_cues;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.slaves.components.slaved;
import engine.gameplay.rts.construction.components.sale;
import Engine.Core.Math.FixedPresentation;

// The control groups as the local player works them (SelectionTranslator::translateGameMessage's MSG_META_CREATE_TEAMn,
// SELECT_TEAMn, ADD_TEAMn and VIEW_TEAMn; the CommandMap.ini keys Ctrl / none / Shift / Alt + 0..9). Creating one is the
// simulation's (a CreateTeam command: MSG_CREATE_TEAMn); selecting and adding work the local selection from the
// simulation's squads at once; a selection made sends a SelectTeam command (MSG_SELECT_TEAMn: the academy's control-group
// count; the logic's copy of the selection is not kept, the port's orders carry their units), and MSG_ADD_TEAMn, which
// only sets that copy, is not sent; pressing a group again within 20 logic frames, or viewing it, centres the view on its last member.
export namespace generalszh::presentation
{
enum class TeamMeta : std::uint8_t
{
	Create,
	Select,
	Add,
	View,
};

// SelectionTranslator's m_lastGroupSelTime / m_lastGroupSelGroup (0 and -1 at first): presentation state in the world.
struct HotkeyTaps
{
	std::uint64_t lastTime{0};
	std::int32_t lastGroup{-1};
};
}

// (Declared before ApplyTeamMeta, which reaches the resource.)
export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::HotkeyTaps>
{
	static constexpr std::string_view StableName = "generalszh.presentation.hotkey_taps";
};
}

export namespace generalszh::presentation
{

struct TeamMetaOutcome
{
	std::optional<commands::CreateTeam> command;    // to submit
	std::optional<commands::SelectTeam> select;     // to submit (MSG_SELECT_TEAMn)
	std::optional<std::array<float, 3>> lookAt;    // TheTacticalView->lookAt
};

namespace hotkey_detail
{
// Object::isSelectable: ALWAYS_SELECTABLE; else SELECTABLE, not OBJECT_STATUS_UNSELECTABLE (an enslaved drone, a structure
// being sold), alive; and not carried off the map.
inline bool Selectable(const ecs::World &world, const SelectionCatalog &catalog, ecs::Entity unit)
{
	if (!world.IsAlive(unit))
		return false;
	const auto *reference = world.Get<engine::gameplay::DefinitionRef>(unit);
	const SelectionLook *look = reference != nullptr ? catalog.Of(reference->index) : nullptr;
	if (look == nullptr)
		return false;
	if ((look->kinds & select_kind::AlwaysSelectable) != 0)
		return true;
	if ((look->kinds & select_kind::Selectable) == 0)
		return false;
	if (const auto *slave = world.Get<engine::gameplay::Slaved>(unit); slave != nullptr && slave->enslaved != 0)
		return false;
	if (world.Get<engine::gameplay::Sale>(unit) != nullptr || world.Get<engine::gameplay::OffMap>(unit) != nullptr)
		return false;
	const auto *health = world.Get<engine::gameplay::Health>(unit);
	return health == nullptr || !engine::gameplay::IsDead(*health);
}

// Squad::getLiveObjects: the squad's members that still exist and are selectable, in order.
inline std::vector<ecs::Entity> LiveMembers(const ecs::World &world, std::uint32_t player, std::int32_t group)
{
	std::vector<ecs::Entity> live;
	const auto *squads = world.FindResource<gameplay::HotkeySquads>();
	const auto *catalog = world.FindResource<SelectionCatalog>();
	if (squads == nullptr || catalog == nullptr)
		return live;
	for (const ecs::Entity member : squads->Members(player, group))
		if (Selectable(world, *catalog, member))
			live.push_back(member);
	return live;
}

inline std::optional<std::array<float, 3>> PositionOf(const ecs::World &world, ecs::Entity unit)
{
	const auto *transform = world.Get<engine::gameplay::Transform>(unit);
	if (transform == nullptr)
		return std::nullopt;
	return std::array<float, 3>{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
		Engine::Math::ToFloat(transform->position.z)};
}

inline std::optional<std::array<float, 3>> LastMemberPosition(const ecs::World &world, std::uint32_t player, std::int32_t group)
{
	const auto live = LiveMembers(world, player, group);
	return live.empty() ? std::nullopt : PositionOf(world, live.back());
}
}

// One team meta-event on logic frame `frame` (TheGameLogic->getFrame).
inline TeamMetaOutcome ApplyTeamMeta(ecs::World &world, TeamMeta meta, std::int32_t group, std::uint64_t frame)
{
	using namespace hotkey_detail;
	TeamMetaOutcome outcome;
	const auto *local = world.FindResource<LocalPlayer>();
	if (local == nullptr || !local->valid || group < 0 || group >= gameplay::HotkeySquadCount)
		return outcome;
	const std::uint32_t player = local->player;
	auto &selected = world.Side<Selected>();
	switch (meta)
	{
	case TeamMeta::Create:
	{
		// The selected objects the local player controls, in the drawable list's order (newest first).
		std::vector<std::pair<std::uint32_t, ecs::Entity>> mine;
		for (const ecs::Entity entity : selected.Entities())
			if (const auto *owner = world.IsAlive(entity) ? world.Get<engine::gameplay::Owner>(entity) : nullptr; owner != nullptr && owner->player == player)
			{
				const auto *id = world.Get<engine::gameplay::ObjectId>(entity);
				mine.emplace_back(id != nullptr ? id->value : 0u, entity);
			}
		std::stable_sort(mine.begin(), mine.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
		commands::CreateTeam create{group, {}};
		for (const auto &[id, entity] : mine)
			create.units.push_back(entity);
		outcome.command = std::move(create);
		return outcome;
	}
	case TeamMeta::Select:
	case TeamMeta::Add:
	{
		HotkeyTaps &taps = world.Resource<HotkeyTaps>();
		if (taps.lastTime == 0)
			taps.lastTime = frame;
		if (frame - taps.lastTime < 20 && group == taps.lastGroup)
			outcome.lookAt = LastMemberPosition(world, player, group); // a double press: the view jumps to the group
		else if (meta == TeamMeta::Select)
		{
			// deselectAllDrawables, MSG_SELECT_TEAMn sent (the logic's selection and the academy's control groups), then
			// each live member the local player controls.
			selected.Clear();
			outcome.select = commands::SelectTeam{group};
			for (const ecs::Entity member : LiveMembers(world, player, group))
				if (const auto *owner = world.Get<engine::gameplay::Owner>(member); owner != nullptr && owner->player == player)
					selected.Emplace(member);
			// MSG_SELECT_TEAMn: CommandTranslator has the selection answer (pickAndPlayUnitVoiceResponse).
			if (auto *cues = world.FindResource<UnitVoiceCues>())
				cues->pending.push_back(UnitVoiceCue{VoiceOrder::CreateGroup});
		}
		else
		{
			// A structure selected last (getFirstSelectedDrawable) goes with the rest first (the group force-attack
			// exploit); then every live member joins, whoever controls it.
			const auto entities = selected.Entities();
			if (!entities.empty())
				if (const auto *catalog = world.FindResource<SelectionCatalog>())
					if (const auto *reference = world.Get<engine::gameplay::DefinitionRef>(entities.back()))
						if (const SelectionLook *look = catalog->Of(reference->index); look != nullptr && (look->kinds & select_kind::Structure) != 0)
							selected.Clear();
			for (const ecs::Entity member : LiveMembers(world, player, group))
				if (selected.Get(member) == nullptr)
					selected.Emplace(member);
		}
		taps.lastTime = frame;
		taps.lastGroup = group;
		return outcome;
	}
	case TeamMeta::View:
		// The original takes groups 1..10 here (VIEW_TEAM0 does nothing); fixed to every group, 0..9.
		outcome.lookAt = LastMemberPosition(world, player, group);
		return outcome;
	}
	return outcome;
}

// Drawable::drawsAnyUIText / drawUIText's group: the number of the hotkey squad of the object's controlling player
// holding it, for a selected object the local player controls (NO_HOTKEY_SQUAD otherwise).
inline std::int32_t ShownGroupNumber(const ecs::World &world, ecs::Entity unit)
{
	const auto *local = world.FindResource<LocalPlayer>();
	const auto *squads = world.FindResource<gameplay::HotkeySquads>();
	const auto *owner = world.Get<engine::gameplay::Owner>(unit);
	if (local == nullptr || !local->valid || squads == nullptr || owner == nullptr || owner->player != local->player)
		return gameplay::NoHotkeySquad;
	return gameplay::SquadNumberOf(*squads, owner->player, unit);
}
}
