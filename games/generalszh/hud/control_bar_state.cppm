export module games.generalszh.hud.control_bar_state;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.common.identity.components.owner;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// What the control bar shows this frame for the local player and their selection (the original's
// ControlBar::populateCommand / updateContextCommand / populateBuildQueue and InGameUI's money): read from the
// session's view, never kept. Headless: the view model turns it into what the windows show.
export namespace generalszh::hud
{
inline constexpr std::size_t CommandButtons = 14; // ButtonCommand01..14 (slots 15..18 are script-only)
inline constexpr std::size_t QueueButtons = 9;    // ButtonQueue01..09 (MAX_BUILD_QUEUE_BUTTONS)

struct CommandSlot
{
	const content::CommandButtonContent *button{nullptr}; // none: the slot is empty
	gameplay::ButtonState state{gameplay::ButtonState::Hidden};
	std::string image; // the button's art (ButtonImage)
	std::uint32_t clock{1000}; // a charging power's inverse clock, per mille (1000: none)
};

struct QueueSlot
{
	bool unit{true};            // a unit (else an upgrade under research)
	std::string image;          // the unit's or upgrade's ButtonImage
	std::uint32_t progress{0};  // the front one's build clock, per mille (0..1000)
	std::uint32_t productionId{0};
	std::string upgrade;        // an upgrade's name (what cancels it)
};

struct ControlBarState
{
	bool hasPlayer{false};
	std::int64_t money{0};
	std::int32_t powerProduced{0};
	std::int32_t powerConsumed{0};
	ecs::Entity selected{};                    // the one object whose commands show (none: nothing, or several)
	std::array<CommandSlot, CommandButtons> slots{};
	std::vector<QueueSlot> queue;             // its production, front first
};

// The control bar for `player` (none: an observer) and what they have selected: the commands of the one object of
// theirs selected (several: none yet), each button's availability, and its production queue.
inline ControlBarState ReadControlBar(session::SessionView &view, std::optional<std::uint32_t> player, std::span<const ecs::Entity> selection)
{
	namespace gp = engine::gameplay;
	ControlBarState state;
	if (!player)
		return state;
	auto &world = view.World();
	state.hasPlayer = true;
	state.money = world.Resource<gp::PlayerMoney>().Balance(*player);
	const auto &energy = world.Resource<gp::PlayerEnergy>();
	state.powerProduced = static_cast<std::int32_t>(energy.Production(*player));
	state.powerConsumed = static_cast<std::int32_t>(energy.Consumption(*player));
	if (selection.size() != 1 || !world.IsAlive(selection.front()))
		return state;
	const ecs::Entity selected = selection.front();
	const auto *owner = world.Get<gp::Owner>(selected);
	if (owner == nullptr || owner->player != *player)
		return state;
	state.selected = selected;
	const content::GameContent &content = view.Content();
	// CommandSet::getCommandButton: the scripts' overrides first.
	const auto effective = generalszh::gameplay::EffectiveCommandSet(content.commands, world.FindResource<generalszh::gameplay::CommandBarOverrides>(),
		view.CommandSetOf(selected));
	if (const content::CommandSetContent *set = effective ? &*effective : nullptr)
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			const content::CommandButtonContent *button = content.commands.Button(set->buttons[slot]);
			if (button == nullptr)
				continue;
			CommandSlot &shown = state.slots[slot];
			shown.button = button;
			shown.state = view.CommandAvailability(selected, *button);
			if (shown.state == gameplay::ButtonState::NotReady)
				shown.clock = view.CommandClock(selected, *button);
			shown.image = button->buttonImage;
		}
	if (const auto *queue = world.Get<gp::ProductionQueue>(selected))
		for (std::uint32_t index = 0; index < queue->count && index < QueueButtons; ++index)
		{
			const gp::ProductionEntry &entry = queue->entries[index];
			QueueSlot slot;
			slot.unit = entry.kind == gp::ProductionKind::Unit;
			if (slot.unit)
			{
				if (entry.definition < view.DefinitionCount())
					slot.image = view.Definition(entry.definition).buttonImage;
				slot.productionId = entry.productionId;
			}
			else if (entry.definition < content.upgrades.upgrades.size())
			{
				slot.image = content.upgrades.upgrades[entry.definition].buttonImage;
				slot.upgrade = content.upgrades.upgrades[entry.definition].name;
			}
			if (index == 0 && entry.ticksTotal > 0)
				slot.progress = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
					entry.progress.Raw() * 1000 / (static_cast<std::int64_t>(entry.ticksTotal) << 16), 0, 1000));
			state.queue.push_back(std::move(slot));
		}
	return state;
}
}
