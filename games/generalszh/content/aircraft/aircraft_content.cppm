export module games.generalszh.content.aircraft.aircraft_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.models.model_rigs;
export import engine.gameplay.rts.movement.definitions.locomotor;
import games.generalszh.content.objects.model_draw;
import games.generalszh.content.locomotors.locomotor_catalog;

// Airfields and jets as content: an airfield's parking places and runways
// (ParkingPlaceBehavior: NumRows x NumCols spaces, their bones on its model:
// RunwayNParkMHan, RunwayNParkingM, RunwayNPrepM, RunwayStartN, RunwayEndN;
// the column is the runway), in the airfield's own frame; and a jet's cycle
// (JetAIUpdate: its flight and taxiing locomotors, how soon it lifts off,
// how long it may idle in the air before flying home).
export namespace generalszh::content
{
struct ParkingLayout
{
	struct Space
	{
		RestBone hangar;
		RestBone parking;
		RestBone prep;
		std::uint32_t runway{0};
	};
	struct Runway
	{
		Engine::Math::FixedVector3 start;
		Engine::Math::FixedVector3 end;
	};
	std::vector<Space> spaces;
	std::vector<Runway> runways;
};

std::optional<ParkingLayout> ReadParkingLayout(const ObjectDefinition &object, ModelRigs &rigs)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "ParkingPlaceBehavior")
			continue;
		const auto count = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? static_cast<int>(engine::config::values::ParseInt(node->Value()).value_or(0)) : 0;
		};
		const auto *hasRunways = module.block->Find("HasRunways");
		const bool runways = hasRunways != nullptr && engine::config::values::ParseBool(hasRunways->Value()).value_or(false);
		const std::string model = DefaultModel(object).model;
		const auto bone = [&](const std::string &name) { return rigs.Bone(model, name).value_or(RestBone{}); };
		ParkingLayout layout;
		const int rows = count("NumRows"), cols = count("NumCols");
		for (int row = 0; row < rows; ++row)
			for (int col = 0; col < cols; ++col)
			{
				const std::string runway = "Runway" + std::to_string(col + 1);
				layout.spaces.push_back({bone(runway + "Park" + std::to_string(row + 1) + "Han"), bone(runway + "Parking" + std::to_string(row + 1)),
					bone(runway + "Prep" + std::to_string(row + 1)), static_cast<std::uint32_t>(col)});
			}
		if (runways)
			for (int col = 0; col < cols; ++col)
				layout.runways.push_back({bone("RunwayStart" + std::to_string(col + 1)).position, bone("RunwayEnd" + std::to_string(col + 1)).position});
		return layout;
	}
	return std::nullopt;
}

struct ObjectJet
{
	engine::gameplay::LocomotorDefinition flight;
	engine::gameplay::LocomotorDefinition taxi;
	Engine::Math::Fixed lift;
	std::uint64_t idleReturnTicks{0};
	std::uint64_t takeoffPauseTicks{0}; // TakeoffPause
	Engine::Math::Fixed minHeight;     // MinHeight
	Engine::Math::Fixed outOfAmmoDamage; // OutOfAmmoDamagePerSecond, a share of its maximum health a tick
};

std::optional<ObjectJet> ReadObjectJet(const ObjectDefinition &object, const engine::config::DefinitionTable<engine::gameplay::LocomotorDefinition> &locomotors,
	const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "JetAIUpdate")
			continue;
		const auto *flight = ObjectLocomotor(object, locomotors, "SET_NORMAL");
		if (flight == nullptr)
			return std::nullopt;
		const auto *taxi = ObjectLocomotor(object, locomotors, "SET_TAXIING");
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		ObjectJet jet;
		jet.flight = *flight;
		jet.taxi = taxi != nullptr ? *taxi : *flight;
		if (const auto *lift = module.block->Find("TakeoffDistForMaxLift"))
			jet.lift = engine::config::ReadPercent(*lift, bind).value_or(Engine::Math::Fixed{});
		if (const auto *idle = module.block->Find("ReturnToBaseIdleTime"))
			jet.idleReturnTicks = engine::config::ReadDurationTicks(*idle, bind).value_or(0);
		if (const auto *hurt = module.block->Find("OutOfAmmoDamagePerSecond"))
			jet.outOfAmmoDamage = engine::config::ReadPercent(*hurt, bind).value_or(Engine::Math::Fixed{}) /
				Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond()));
		if (const auto *height = module.block->Find("MinHeight"))
			jet.minHeight = engine::config::values::ParseFixed(height->Value()).value_or(Engine::Math::Fixed{});
		if (const auto *pause = module.block->Find("TakeoffPause"))
			jet.takeoffPauseTicks = engine::config::ReadDurationTicks(*pause, bind).value_or(0);
		return jet;
	}
	return std::nullopt;
}
}
