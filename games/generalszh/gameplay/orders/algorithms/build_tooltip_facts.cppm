export module games.generalszh.gameplay.orders.algorithms.build_tooltip_facts;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.content.control_bar.command_catalog;
import engine.ecs.query.query;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.production.algorithms.build_cost;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import games.generalszh.gameplay.orders.algorithms.command_button_readiness;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.components.overcharge;

// What the control bar's build tooltip asks the game about a command button (ControlBar::populateBuildTooltipLayout,
// Core/GameEngine/Source/GameClient/GUI/GUICallbacks/ControlBarPopupDescription.cpp): for the local player and the
// first selected object, read now and never kept. The text is the hud's (hud::PopulateBuildTooltip).
export namespace generalszh::gameplay
{
// BuildAssistant::canMakeUnit's answer as the tooltip tells it (CANMAKE_*): the reasons it names, else none.
enum class BuildRefusal : std::uint8_t
{
	None,        // CANMAKE_OK, or a reason the tooltip does not name (no prerequisites, factory disabled)
	NoMoney,     // CANMAKE_NO_MONEY
	QueueFull,   // CANMAKE_QUEUE_FULL
	ParkingFull, // CANMAKE_PARKING_PLACES_FULL
	MaxedOut,    // CANMAKE_MAXED_OUT_FOR_PLAYER
};

// One ProductionPrerequisite, in the INI's order: an Object line's units with how many of each the player has
// (countObjectsByThingTemplate: dead ones too, not those under construction), or a Science line's science, had or not.
struct PrerequisiteState
{
	bool science{false};
	std::vector<std::string> units;
	std::vector<int> owned;
	bool hasScience{true};
};

struct BuildTooltipFacts
{
	std::vector<bool> hasButtonScience; // Player::hasScience for each of the button's sciences
	bool selected{false};               // a first selected object
	std::optional<bool> overcharge;     // its OverchargeBehavior on (none: it has none)
	BuildRefusal refusal{BuildRefusal::None}; // canMakeUnit(selected, the button's Object)
	std::int64_t thingCost{0};          // ThingTemplate::calcCostToBuild(player)
	std::vector<PrerequisiteState> prerequisites;
	bool upgradeInProduction{false};    // Player::hasUpgradeInProduction
	bool queueAtMax{false};             // the selected's production count == MAX_BUILD_QUEUE_BUTTONS
	bool canAffordUpgrade{true};        // UpgradeCenter::canAffordUpgrade
	bool upgradeComplete{false};        // Player::hasUpgradeComplete
	bool objectHasUpgrade{false};       // the selected's Object::hasUpgrade
	bool objectAffected{true};          // the selected's Object::affectedByUpgrade
	std::int64_t upgradeCost{0};        // UpgradeTemplate::calcCostToBuild
};

namespace build_tooltip_detail
{
// canMakeUnit in its order: script-disabled, maxed out, not possible (no reason told); then the factory's queue and
// parking (ProductionUpdate::canQueueCreateUnit), then the money.
inline BuildRefusal Refusal(GameWorld &game, ecs::Entity builder, const content::ObjectDefinition &what)
{
	namespace gp = engine::gameplay;
	switch (CanMakeUnit(game, builder, what))
	{
	case CanMake::MaxedOut: return BuildRefusal::MaxedOut;
	case CanMake::NoPrerequisites:
	case CanMake::BuilderDisabled: return BuildRefusal::None;
	case CanMake::Ok:
	case CanMake::NoMoney: break;
	}
	if (game.world.Get<gp::ProductionQueue>(builder) != nullptr)
	{
		if (game.world.Get<gp::Airfield>(builder) != nullptr && !what.Is("PRODUCED_AT_HELIPAD") && !FreeSpace(game, builder))
			return BuildRefusal::ParkingFull;
		if (game.world.Get<gp::ProductionQueue>(builder)->Full())
			return BuildRefusal::QueueFull;
	}
	return CanMakeUnit(game, builder, what) == CanMake::NoMoney ? BuildRefusal::NoMoney : BuildRefusal::None;
}
}

inline BuildTooltipFacts ReadBuildTooltipFacts(GameWorld &game, std::uint32_t player, ecs::Entity selected, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	using namespace build_tooltip_detail;
	auto &world = game.world;
	const content::GameContent &content = game.templates.Content();
	BuildTooltipFacts facts;
	const auto &sciences = world.Resource<gp::PlayerSciences>();
	const auto hasScience = [&](std::string_view name) {
		const auto science = content.Science(name);
		return science && sciences.Has(player, *science);
	};
	for (const std::string &science : button.sciences)
		facts.hasButtonScience.push_back(hasScience(science));
	const bool alive = world.IsAlive(selected) && world.Get<gp::DefinitionRef>(selected) != nullptr;
	facts.selected = alive;
	if (alive)
		if (const auto *overcharge = world.Get<gp::Overcharge>(selected))
			facts.overcharge = overcharge->active != 0;
	if (const content::ObjectDefinition *what = button.object.empty() ? nullptr : content.objects.Find(button.object))
	{
		if (alive)
			facts.refusal = Refusal(game, selected, *what);
		facts.thingCost = CostToBuild(game, player, *what);
		std::size_t objects = 0, sciencesSeen = 0;
		for (const char kind : what->prerequisiteOrder)
		{
			PrerequisiteState state;
			if (kind == 'O' && objects < what->prerequisiteObjects.size())
			{
				state.units = what->prerequisiteObjects[objects++];
				state.owned.assign(state.units.size(), 0);
				ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>, ecs::Exclude<gp::UnderConstruction>> things(world);
				things.ForEachChunk([&](auto chunk) {
					const auto owners = chunk.template Get<gp::Owner>();
					const auto definitions = chunk.template Get<gp::DefinitionRef>();
					for (std::size_t row = 0; row < owners.size(); ++row)
					{
						if (owners[row].player != player)
							continue;
						const std::string &name = game.templates.DefinitionAt(definitions[row].index).name;
						for (std::size_t unit = 0; unit < state.units.size(); ++unit)
							if (state.units[unit] == name)
								++state.owned[unit];
					}
				});
			}
			else if (kind == 'S' && sciencesSeen < what->prerequisiteSciences.size())
			{
				state.science = true;
				state.hasScience = hasScience(what->prerequisiteSciences[sciencesSeen++]);
			}
			else
				continue;
			facts.prerequisites.push_back(std::move(state));
		}
	}
	if (const auto upgrade = button.upgrade.empty() ? std::nullopt : content.upgrades.Find(button.upgrade))
	{
		const auto &players = world.Resource<gp::PlayerUpgrades>();
		facts.upgradeInProduction = players.InProduction(player, *upgrade);
		facts.upgradeComplete = players.Completed(player).Has(*upgrade);
		facts.upgradeCost = content.upgrades.upgrades[*upgrade].cost;
		facts.canAffordUpgrade = world.Resource<gp::PlayerMoney>().Balance(player) >= facts.upgradeCost;
		if (alive)
		{
			if (const auto *queue = world.Get<gp::ProductionQueue>(selected))
				facts.queueAtMax = queue->count == gp::ProductionQueue::MaxEntries;
			const auto *own = world.Get<gp::Upgradable>(selected);
			facts.objectHasUpgrade = own != nullptr && own->completed.Has(*upgrade);
			facts.objectAffected = AffectedByUpgrade(game, selected, *upgrade);
		}
	}
	return facts;
}
}
