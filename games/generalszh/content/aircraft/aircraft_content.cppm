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
		// A flight deck's: its landing strip, taxi points and creation points (the first: the hangar).
		bool landing{false};
		Engine::Math::FixedVector3 landStart;
		Engine::Math::FixedVector3 landEnd;
		std::vector<Engine::Math::FixedVector3> taxi;
		std::vector<RestBone> creation;
	};
	std::vector<Space> spaces;
	std::vector<Runway> runways;
	// A flight deck: its deck's height over the terrain, and only its front row takes off.
	Engine::Math::Fixed deckHeight;
	bool frontRow{false};
};

// FlightDeckBehavior's timing and payload (FlightDeckBehaviorModuleData; durations in frames, rounded up as
// parseDurationUnsignedInt): its jets (PayloadTemplate), healed HealAmountPerSecond while parked, the queue moved up every
// ParkingCleanupPeriod (HumanFollowPeriod after a move), a replacement after ReplacementDelay + DockAnimationDelay, a
// wave every LaunchWaveDelay, the ramp up LaunchRampDelay before a launch and down LowerRampDelay after it, the catapult's
// steam CatapultFireDelay after it (each runway's Runway{N}CatapultSystem); ApproachHeight.
struct FlightDeckContent
{
	std::string payload;
	Engine::Math::Fixed healPerSecond;
	Engine::Math::Fixed approachHeight;
	std::uint64_t cleanupTicks{0};
	std::uint64_t followTicks{0};
	std::uint64_t replacementTicks{0};
	std::uint64_t dockTicks{0};
	std::uint64_t launchWaveTicks{0};
	std::uint64_t launchRampTicks{0};
	std::uint64_t lowerRampTicks{0};
	std::uint64_t catapultTicks{0};
	std::vector<std::string> catapultSystems; // by runway
};

inline std::optional<FlightDeckContent> ReadFlightDeck(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "FlightDeckBehavior")
			continue;
		FlightDeckContent out;
		const auto frames = [&](std::string_view key) -> std::uint64_t {
			const auto *node = module.block->Find(key);
			if (node == nullptr || node->values.empty())
				return 0;
			const std::int64_t ms = std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0));
			return static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		const auto real = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
		};
		if (const auto *node = module.block->Find("PayloadTemplate"))
			out.payload = std::string(node->Value());
		out.healPerSecond = real("HealAmountPerSecond");
		out.approachHeight = real("ApproachHeight");
		out.cleanupTicks = frames("ParkingCleanupPeriod");
		out.followTicks = frames("HumanFollowPeriod");
		out.replacementTicks = frames("ReplacementDelay");
		out.dockTicks = frames("DockAnimationDelay");
		out.launchWaveTicks = frames("LaunchWaveDelay");
		out.launchRampTicks = frames("LaunchRampDelay");
		out.lowerRampTicks = frames("LowerRampDelay");
		out.catapultTicks = frames("CatapultFireDelay");
		const auto *runways = module.block->Find("NumRunways");
		const int count = runways != nullptr ? static_cast<int>(engine::config::values::ParseInt(runways->Value()).value_or(0)) : 0;
		for (int runway = 1; runway <= count; ++runway)
		{
			const auto *node = module.block->Find("Runway" + std::to_string(runway) + "CatapultSystem");
			out.catapultSystems.push_back(node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{});
		}
		return out;
	}
	return std::nullopt;
}

// FlightDeckBehavior::buildInfo's parking: NumRunways x NumSpacesPerRunway spaces, space `row * runways + runway` the
// row'th of Runway{N}Spaces (the first at the runway's start), each parked and prepped at its bone (facing its turn); a
// new jet comes out of the runway's first creation bone (the hangar). Runway{N}Takeoff and Runway{N}Landing name the
// strips' start and end bones, Runway{N}Taxi and Runway{N}Creation the points between; LandingDeckHeightOffset is the
// deck's height. Bones of its default model at rest.
inline std::optional<ParkingLayout> ReadFlightDeckLayout(const ObjectDefinition &object, ModelRigs &rigs)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "FlightDeckBehavior")
			continue;
		const std::string model = DefaultModel(object).model;
		const auto bone = [&](std::string_view name) { return rigs.Bone(model, name).value_or(RestBone{}); };
		const auto names = [&](const std::string &key) {
			std::vector<std::string> out;
			if (const auto *node = module.block->Find(key))
				for (const std::string_view value : node->values)
					out.emplace_back(value);
			return out;
		};
		const auto count = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? static_cast<int>(engine::config::values::ParseInt(node->Value()).value_or(0)) : 0;
		};
		ParkingLayout layout;
		layout.frontRow = true;
		if (const auto *deck = module.block->Find("LandingDeckHeightOffset"))
			layout.deckHeight = engine::config::values::ParseFixed(deck->Value()).value_or(Engine::Math::Fixed{});
		const int runways = count("NumRunways"), rows = count("NumSpacesPerRunway");
		std::vector<std::vector<std::string>> spaces;
		for (int runway = 1; runway <= runways; ++runway)
		{
			const std::string prefix = "Runway" + std::to_string(runway);
			spaces.push_back(names(prefix + "Spaces"));
			ParkingLayout::Runway strip;
			const auto takeoff = names(prefix + "Takeoff");
			const auto landing = names(prefix + "Landing");
			if (takeoff.size() >= 2)
			{
				strip.start = bone(takeoff[0]).position;
				strip.end = bone(takeoff[1]).position;
			}
			if (landing.size() >= 2)
			{
				strip.landing = true;
				strip.landStart = bone(landing[0]).position;
				strip.landEnd = bone(landing[1]).position;
			}
			for (const std::string &name : names(prefix + "Taxi"))
				strip.taxi.push_back(bone(name).position);
			for (const std::string &name : names(prefix + "Creation"))
				strip.creation.push_back(bone(name));
			layout.runways.push_back(std::move(strip));
		}
		for (int row = 0; row < rows; ++row)
			for (int runway = 0; runway < runways; ++runway)
			{
				const RestBone at = static_cast<std::size_t>(row) < spaces[runway].size() ? bone(spaces[runway][row]) : RestBone{};
				const RestBone hangar = layout.runways[runway].creation.empty() ? at : layout.runways[runway].creation.front();
				layout.spaces.push_back({hangar, at, at, static_cast<std::uint32_t>(runway)});
			}
		return layout;
	}
	return std::nullopt;
}

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
