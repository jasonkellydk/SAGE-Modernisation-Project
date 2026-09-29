export module games.generalszh.gameplay.ai.resources.ai_players;
import std;

export import engine.ecs.core.entity;
export import engine.core.serialization.byte_stream;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The computer players (the original's AIPlayer / AISkirmishPlayer, one per computer player): its level of difficulty,
// its side, its base plan (the side's skirmish build list moved to its start: each structure, where, facing, how
// often it may still be rebuilt, whether it stood from the start and is built unasked, and what stands there now),
// where its base is (the plan's middle and reach), and the enemy it has picked (looked at again every 5 s).
// Its team building: the teams in its build and ready queues with their work orders, and per level team its production
// priority and condition timing. Simulation state: checkpointed and hashed.
export namespace generalszh::gameplay
{
struct AiBuildSlot
{
	std::string structure;
	Engine::Math::FixedVector2 location;
	Engine::Math::Fixed angleDegrees;
	std::int32_t rebuilds{0};
	bool initiallyBuilt{false};
	bool automaticallyBuild{true};
	ecs::Entity built;          // what stands for it (none: nothing yet, or it was lost)
	std::uint64_t builtTick{0}; // the tick after it was put up, or after it was lost (0: never, or ready to rebuild)
	bool priorityBuild{false};  // marked by SKIRMISH_BUILD_BUILDING (never cleared)
	bool underConstruction{false};
	// A supply centre's (checkForSupplyCenter): the supply gatherers it wants and has (-1: its free one is still due).
	bool supplyBuilding{false};
	std::int32_t desiredGatherers{0};
	std::int32_t currentGatherers{0};
};

// A unit a team being built still needs (WorkOrder): its type, the factory making one now (none: none is), how many are
// done of how many, whether the team needs them to count as built (the minimum), and whether it is a supply gatherer
// (queueSupplyTruck's).
struct AiWorkOrder
{
	std::uint32_t definition{0};
	ecs::Entity factory;
	std::int32_t completed{0};
	std::int32_t required{0};
	bool mandatory{false};
	bool resourceGatherer{false};

	// isWaitingToBuild: nothing makes one now and more are needed.
	bool WaitingToBuild() const noexcept { return factory == ecs::Entity{} && completed < required; }
};

// A team being built or waiting to start (TeamInQueue): the team instance its units join, its orders (in the original's
// list order), the tick it was started, whether a script asked for it (it may take a busy factory), whether it is one
// unit for a team already out (and that unit), and whether it was sent to its start.
struct AiTeamInQueue
{
	std::uint32_t team{0};
	std::vector<AiWorkOrder> orders;
	std::uint64_t started{0};
	bool priorityBuild{false};
	bool reinforcement{false};
	bool sentToStartLocation{false};
	ecs::Entity reinforcementUnit;
};

// What a computer player keeps about each of its teams (TeamPrototype): its production priority now (scripts and
// success/failure change it) and when its production condition (the prototype's own copy of the script) may next be
// looked at.
struct AiTeamProduction
{
	bool priorityChanged{false};
	std::int32_t priority{0};
	std::uint64_t evaluateAt{0};
};

struct AiPlayer
{
	std::uint32_t player{0};
	// An AISkirmishPlayer (its side's skirmish build list; faster team looks), else a plain AIPlayer (the map's plan).
	bool skirmish{true};
	std::uint8_t difficulty{1}; // easy 0, normal 1, hard 2
	std::string side;
	std::vector<AiBuildSlot> buildList;
	Engine::Math::FixedVector2 baseCenter;
	bool baseCenterSet{false};
	Engine::Math::Fixed baseRadius;
	std::optional<std::uint32_t> enemy;
	std::uint64_t checkEnemyAt{0};
	// Base building (doBaseBuilding): ready for the next structure, the countdown to that, the ticks to the next look,
	// the tick of the last one started.
	bool readyToBuildStructure{false};
	std::int64_t structureTimer{2};
	std::int64_t buildDelay{0};
	std::uint64_t lastBuildingTick{0};
	ecs::Entity currentWarehouse; // the supply warehouse it last planned by (m_curWarehouseID)
	// isSupplySourceAttacked: the tick before which it does not look again (m_supplySourceAttackCheckFrame) and what it
	// last found under attack (m_attackedSupplyCenter).
	std::uint64_t supplyAttackCheckTick{0};
	ecs::Entity attackedSupplyCenter;
	// Team building (doTeamBuilding): the teams being built and those built and waiting to start, ready for the next
	// team, the countdown to that, the ticks to the next look, and the seconds between teams (AIData TeamSeconds).
	std::vector<AiTeamInQueue> buildQueue;
	std::vector<AiTeamInQueue> readyQueue;
	bool readyToBuildTeam{false};
	std::int64_t teamTimer{2};
	std::int64_t teamDelay{0};
	std::int64_t teamSeconds{10};
	// Base defences (AISkirmishPlayer): how many front and flank ones were tried, and the angles (radians off the
	// approach) the next ones go at: front left/right, left flank (backdoor) left/right, right flank left/right.
	std::int32_t frontDefenses{0};
	std::int32_t flankDefenses{0};
	std::array<Engine::Math::Fixed, 6> defenseAngles{};
	// The skill set it buys its general's sciences from (m_skillsetSelector; -1: not chosen yet).
	std::int32_t skillset{-1};
	// Structure repair (repairStructure / updateBridgeRepair): what waits (at most 2, first first), the once-a-second
	// countdown, its repair dozer and where that started, and whether one is being made for it or is at work.
	std::vector<ecs::Entity> toRepair;
	std::int64_t repairTimer{0};
	ecs::Entity repairDozer;
	Engine::Math::FixedVector2 repairDozerOrigin;
	bool dozerQueuedForRepair{false};
	bool dozerIsRepairing{false};
};

struct AiPlayers
{
	std::vector<AiPlayer> players;
	// By level team (TeamRoster index of the team a team is an instance of).
	std::vector<AiTeamProduction> teams;

	AiTeamProduction &Production(std::uint32_t team)
	{
		if (team >= teams.size())
			teams.resize(team + 1);
		return teams[team];
	}

	AiPlayer *Of(std::uint32_t player) noexcept
	{
		for (AiPlayer &ai : players)
			if (ai.player == player)
				return &ai;
		return nullptr;
	}
	const AiPlayer *Of(std::uint32_t player) const noexcept { return const_cast<AiPlayers *>(this)->Of(player); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		const auto fixed = [&](Engine::Math::Fixed value) { writer.I64(value.Raw()); };
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const AiPlayer &ai : players)
		{
			writer.U32(ai.player);
			writer.U8(ai.difficulty);
			writer.Text(ai.side);
			writer.U32(static_cast<std::uint32_t>(ai.buildList.size()));
			for (const AiBuildSlot &slot : ai.buildList)
			{
				writer.Text(slot.structure);
				fixed(slot.location.x);
				fixed(slot.location.y);
				fixed(slot.angleDegrees);
				writer.I64(slot.rebuilds);
				writer.U8(static_cast<std::uint8_t>((slot.initiallyBuilt ? 1 : 0) | (slot.automaticallyBuild ? 2 : 0)));
				writer.U32(slot.built.index);
				writer.U32(slot.built.generation);
				writer.U64(slot.builtTick);
				writer.U8(static_cast<std::uint8_t>((slot.priorityBuild ? 1 : 0) | (slot.underConstruction ? 2 : 0) | (slot.supplyBuilding ? 4 : 0)));
				writer.I64(slot.desiredGatherers);
				writer.I64(slot.currentGatherers);
			}
			fixed(ai.baseCenter.x);
			fixed(ai.baseCenter.y);
			writer.U8(ai.baseCenterSet ? 1 : 0);
			fixed(ai.baseRadius);
			writer.U8(ai.enemy ? 1 : 0);
			writer.U32(ai.enemy.value_or(0));
			writer.U64(ai.checkEnemyAt);
			writer.U8(ai.readyToBuildStructure ? 1 : 0);
			writer.I64(ai.structureTimer);
			writer.I64(ai.buildDelay);
			writer.U64(ai.lastBuildingTick);
			writer.U32(ai.currentWarehouse.index);
			writer.U32(ai.currentWarehouse.generation);
			writer.U64(ai.supplyAttackCheckTick);
			writer.U32(ai.attackedSupplyCenter.index);
			writer.U32(ai.attackedSupplyCenter.generation);
			writer.U8(ai.skirmish ? 1 : 0);
			for (const auto *queue : {&ai.buildQueue, &ai.readyQueue})
			{
				writer.U32(static_cast<std::uint32_t>(queue->size()));
				for (const AiTeamInQueue &entry : *queue)
				{
					writer.U32(entry.team);
					writer.U64(entry.started);
					writer.U8(static_cast<std::uint8_t>((entry.priorityBuild ? 1 : 0) | (entry.reinforcement ? 2 : 0) | (entry.sentToStartLocation ? 4 : 0)));
					writer.U32(entry.reinforcementUnit.index);
					writer.U32(entry.reinforcementUnit.generation);
					writer.U32(static_cast<std::uint32_t>(entry.orders.size()));
					for (const AiWorkOrder &order : entry.orders)
					{
						writer.U32(order.definition);
						writer.U32(order.factory.index);
						writer.U32(order.factory.generation);
						writer.I64(order.completed);
						writer.I64(order.required);
						writer.U8(static_cast<std::uint8_t>((order.mandatory ? 1 : 0) | (order.resourceGatherer ? 2 : 0)));
					}
				}
			}
			writer.U8(ai.readyToBuildTeam ? 1 : 0);
			writer.I64(ai.teamTimer);
			writer.I64(ai.teamDelay);
			writer.I64(ai.teamSeconds);
			writer.I64(ai.frontDefenses);
			writer.I64(ai.flankDefenses);
			for (const Engine::Math::Fixed angle : ai.defenseAngles)
				fixed(angle);
			writer.I64(ai.skillset);
			writer.U32(static_cast<std::uint32_t>(ai.toRepair.size()));
			for (const ecs::Entity entity : ai.toRepair)
			{
				writer.U32(entity.index);
				writer.U32(entity.generation);
			}
			writer.I64(ai.repairTimer);
			writer.U32(ai.repairDozer.index);
			writer.U32(ai.repairDozer.generation);
			fixed(ai.repairDozerOrigin.x);
			fixed(ai.repairDozerOrigin.y);
			writer.U8(static_cast<std::uint8_t>((ai.dozerQueuedForRepair ? 1 : 0) | (ai.dozerIsRepairing ? 2 : 0)));
		}
		writer.U32(static_cast<std::uint32_t>(teams.size()));
		for (const AiTeamProduction &team : teams)
		{
			writer.U8(team.priorityChanged ? 1 : 0);
			writer.I64(team.priority);
			writer.U64(team.evaluateAt);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto fixed = [&]() -> std::optional<Engine::Math::Fixed> {
			const auto raw = reader.I64();
			return raw ? std::optional(Engine::Math::Fixed::FromRaw(*raw)) : std::nullopt;
		};
		const auto count = reader.U32();
		if (!count)
			return false;
		std::vector<AiPlayer> loaded;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			AiPlayer ai;
			const auto player = reader.U32();
			const auto difficulty = reader.U8();
			auto side = reader.Text();
			const auto slots = reader.U32();
			if (!player || !difficulty || !side || !slots)
				return false;
			ai.player = *player;
			ai.difficulty = *difficulty;
			ai.side = std::move(*side);
			for (std::uint32_t slotIndex = 0; slotIndex < *slots; ++slotIndex)
			{
				AiBuildSlot slot;
				auto structure = reader.Text();
				const auto x = fixed(), y = fixed(), angle = fixed();
				const auto rebuilds = reader.I64();
				const auto flags = reader.U8();
				const auto builtIndex = reader.U32(), builtGeneration = reader.U32();
				const auto builtTick = reader.U64();
				const auto marks = reader.U8();
				const auto desired = reader.I64(), current = reader.I64();
				if (!structure || !x || !y || !angle || !rebuilds || !flags || !builtIndex || !builtGeneration || !builtTick || !marks || !desired || !current)
					return false;
				slot.supplyBuilding = (*marks & 4) != 0;
				slot.desiredGatherers = static_cast<std::int32_t>(*desired);
				slot.currentGatherers = static_cast<std::int32_t>(*current);
				slot.structure = std::move(*structure);
				slot.location = {*x, *y};
				slot.angleDegrees = *angle;
				slot.rebuilds = static_cast<std::int32_t>(*rebuilds);
				slot.initiallyBuilt = (*flags & 1) != 0;
				slot.automaticallyBuild = (*flags & 2) != 0;
				slot.built.index = *builtIndex;
				slot.built.generation = *builtGeneration;
				slot.builtTick = *builtTick;
				slot.priorityBuild = (*marks & 1) != 0;
				slot.underConstruction = (*marks & 2) != 0;
				ai.buildList.push_back(std::move(slot));
			}
			const auto cx = fixed(), cy = fixed();
			const auto centerSet = reader.U8();
			const auto radius = fixed();
			const auto hasEnemy = reader.U8();
			const auto enemy = reader.U32();
			const auto checkAt = reader.U64();
			const auto ready = reader.U8();
			const auto timer = reader.I64();
			const auto delay = reader.I64();
			const auto last = reader.U64();
			const auto warehouseIndex = reader.U32(), warehouseGeneration = reader.U32();
			if (!cx || !cy || !centerSet || !radius || !hasEnemy || !enemy || !checkAt || !ready || !timer || !delay || !last || !warehouseIndex || !warehouseGeneration)
				return false;
			ai.baseCenter = {*cx, *cy};
			ai.baseCenterSet = *centerSet != 0;
			ai.baseRadius = *radius;
			if (*hasEnemy != 0)
				ai.enemy = *enemy;
			ai.checkEnemyAt = *checkAt;
			ai.readyToBuildStructure = *ready != 0;
			ai.structureTimer = *timer;
			ai.buildDelay = *delay;
			ai.lastBuildingTick = *last;
			ai.currentWarehouse.index = *warehouseIndex;
			ai.currentWarehouse.generation = *warehouseGeneration;
			const auto checkTick = reader.U64();
			const auto attackedIndex = reader.U32(), attackedGeneration = reader.U32();
			if (!checkTick || !attackedIndex || !attackedGeneration)
				return false;
			ai.supplyAttackCheckTick = *checkTick;
			ai.attackedSupplyCenter.index = *attackedIndex;
			ai.attackedSupplyCenter.generation = *attackedGeneration;
			const auto skirmish = reader.U8();
			if (!skirmish)
				return false;
			ai.skirmish = *skirmish != 0;
			for (auto *queue : {&ai.buildQueue, &ai.readyQueue})
			{
				const auto entries = reader.U32();
				if (!entries)
					return false;
				for (std::uint32_t entryIndex = 0; entryIndex < *entries; ++entryIndex)
				{
					AiTeamInQueue entry;
					const auto team = reader.U32();
					const auto started = reader.U64();
					const auto marks = reader.U8();
					const auto unitIndex = reader.U32(), unitGeneration = reader.U32();
					const auto orders = reader.U32();
					if (!team || !started || !marks || !unitIndex || !unitGeneration || !orders)
						return false;
					entry.team = *team;
					entry.started = *started;
					entry.priorityBuild = (*marks & 1) != 0;
					entry.reinforcement = (*marks & 2) != 0;
					entry.sentToStartLocation = (*marks & 4) != 0;
					entry.reinforcementUnit.index = *unitIndex;
					entry.reinforcementUnit.generation = *unitGeneration;
					for (std::uint32_t orderIndex = 0; orderIndex < *orders; ++orderIndex)
					{
						AiWorkOrder order;
						const auto definition = reader.U32();
						const auto factoryIndex = reader.U32(), factoryGeneration = reader.U32();
						const auto completed = reader.I64(), required = reader.I64();
						const auto flags = reader.U8();
						if (!definition || !factoryIndex || !factoryGeneration || !completed || !required || !flags)
							return false;
						order.definition = *definition;
						order.factory.index = *factoryIndex;
						order.factory.generation = *factoryGeneration;
						order.completed = static_cast<std::int32_t>(*completed);
						order.required = static_cast<std::int32_t>(*required);
						order.mandatory = (*flags & 1) != 0;
						order.resourceGatherer = (*flags & 2) != 0;
						entry.orders.push_back(order);
					}
					queue->push_back(std::move(entry));
				}
			}
			const auto readyTeam = reader.U8();
			const auto teamTimer = reader.I64(), teamDelay = reader.I64(), teamSeconds = reader.I64();
			if (!readyTeam || !teamTimer || !teamDelay || !teamSeconds)
				return false;
			ai.readyToBuildTeam = *readyTeam != 0;
			ai.teamTimer = *teamTimer;
			ai.teamDelay = *teamDelay;
			ai.teamSeconds = *teamSeconds;
			const auto front = reader.I64(), flank = reader.I64();
			if (!front || !flank)
				return false;
			ai.frontDefenses = static_cast<std::int32_t>(*front);
			ai.flankDefenses = static_cast<std::int32_t>(*flank);
			for (Engine::Math::Fixed &angle : ai.defenseAngles)
			{
				const auto value = fixed();
				if (!value)
					return false;
				angle = *value;
			}
			const auto skillset = reader.I64();
			if (!skillset)
				return false;
			ai.skillset = static_cast<std::int32_t>(*skillset);
			const auto repairs = reader.U32();
			if (!repairs || *repairs > 2)
				return false;
			for (std::uint32_t index = 0; index < *repairs; ++index)
			{
				const auto entityIndex = reader.U32(), generation = reader.U32();
				if (!entityIndex || !generation)
					return false;
				ecs::Entity entity;
				entity.index = *entityIndex;
				entity.generation = *generation;
				ai.toRepair.push_back(entity);
			}
			const auto repairTimer = reader.I64();
			const auto dozerIndex = reader.U32(), dozerGeneration = reader.U32();
			const auto ox = fixed(), oy = fixed();
			const auto repairFlags = reader.U8();
			if (!repairTimer || !dozerIndex || !dozerGeneration || !ox || !oy || !repairFlags)
				return false;
			ai.repairTimer = *repairTimer;
			ai.repairDozer.index = *dozerIndex;
			ai.repairDozer.generation = *dozerGeneration;
			ai.repairDozerOrigin = {*ox, *oy};
			ai.dozerQueuedForRepair = (*repairFlags & 1) != 0;
			ai.dozerIsRepairing = (*repairFlags & 2) != 0;
			loaded.push_back(std::move(ai));
		}
		const auto teamCount = reader.U32();
		if (!teamCount)
			return false;
		std::vector<AiTeamProduction> loadedTeams;
		for (std::uint32_t index = 0; index < *teamCount; ++index)
		{
			const auto changed = reader.U8();
			const auto priority = reader.I64();
			const auto evaluateAt = reader.U64();
			if (!changed || !priority || !evaluateAt)
				return false;
			loadedTeams.push_back({*changed != 0, static_cast<std::int32_t>(*priority), *evaluateAt});
		}
		players = std::move(loaded);
		teams = std::move(loadedTeams);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AiPlayers>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ai_players";
};
}
