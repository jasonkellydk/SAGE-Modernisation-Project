export module games.generalszh.gameplay.scripts.resources.script_records;
import std;

export import engine.ecs.core.entity;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// What the original's ScriptEngine keeps for its conditions besides flags, counters and timers: the object type
// lists scripts make (OBJECTLIST_ADDOBJECTTYPE: a named list of object types), each player's count of an object type
// as last seen (PLAYER_LOST_OBJECT_TYPE), the upgrades each player has finished (by the building that researched
// them) and the sciences each has gained, until a condition takes them (notifyOfCompletedUpgrade /
// notifyOfAcquiredScience), and the special powers each has triggered and completed, by the object that fired them
// (notifyOfTriggeredSpecialPower / notifyOfCompletedSpecialPower); and when each speech and sound a condition asked
// about is done (isSpeechComplete / isAudioComplete: m_testingSpeech, m_testingAudio). Simulation state: checkpointed
// and hashed.
export namespace generalszh::gameplay
{
struct ScriptRecords
{
	std::vector<std::pair<std::string, std::vector<std::string>>> objectTypeLists; // in the order made
	std::vector<std::map<std::string, std::int64_t>> objectCounts;               // by player
	std::vector<std::vector<std::pair<std::string, ecs::Entity>>> completedUpgrades; // by player
	std::vector<std::vector<std::string>> acquiredSciences;                          // by player
	std::vector<std::vector<std::pair<std::string, ecs::Entity>>> triggeredPowers;   // by player
	std::vector<std::vector<std::pair<std::string, ecs::Entity>>> completedPowers;   // by player
	std::vector<std::pair<std::string, std::uint64_t>> testingSpeech, testingAudio;   // newest first: the tick each is done
	bool timeFrozen{false}; // FREEZE_TIME / UNFREEZE_TIME (m_freezeByScript)

	// ScriptEngine::isSpeechComplete / isAudioComplete: the first time a sound is asked about it is done its length
	// in ticks from now (`lengthTicks`); once the tick comes, it is done and taken off the list.
	static bool SoundComplete(std::vector<std::pair<std::string, std::uint64_t>> &list, const std::string &name, std::uint64_t tick,
		const std::function<std::uint64_t()> &lengthTicks)
	{
		auto found = std::find_if(list.begin(), list.end(), [&](const auto &entry) { return entry.first == name; });
		if (found == list.end())
		{
			list.insert(list.begin(), {name, (lengthTicks ? lengthTicks() : 0) + tick});
			found = list.begin();
		}
		if (tick < found->second)
			return false;
		list.erase(found);
		return true;
	}

	const std::vector<std::string> *List(std::string_view name) const noexcept
	{
		for (const auto &[listName, types] : objectTypeLists)
			if (listName == name)
				return &types;
		return nullptr;
	}
	// ScriptEngine::addObjectToList: into the named list (made when first named), once.
	void AddToList(const std::string &name, const std::string &type)
	{
		for (auto &[listName, types] : objectTypeLists)
			if (listName == name)
			{
				if (std::find(types.begin(), types.end(), type) == types.end())
					types.push_back(type);
				return;
			}
		objectTypeLists.push_back({name, {type}});
	}
	// doObjectTypeListMaintenance removing: out of the list; an empty list goes.
	void RemoveFromList(const std::string &name, const std::string &type)
	{
		for (auto it = objectTypeLists.begin(); it != objectTypeLists.end(); ++it)
			if (it->first == name)
			{
				std::erase(it->second, type);
				if (it->second.empty())
					objectTypeLists.erase(it);
				return;
			}
	}
	template<typename T>
	static T &ForPlayer(std::vector<T> &list, std::uint32_t player)
	{
		if (list.size() <= player)
			list.resize(player + 1);
		return list[player];
	}
	void CompletedUpgrade(std::uint32_t player, const std::string &upgrade, ecs::Entity source)
	{
		ForPlayer(completedUpgrades, player).push_back({upgrade, source});
	}
	void AcquiredScience(std::uint32_t player, const std::string &science) { ForPlayer(acquiredSciences, player).push_back(science); }
	void TriggeredPower(std::uint32_t player, const std::string &power, ecs::Entity source) { ForPlayer(triggeredPowers, player).push_back({power, source}); }
	void CompletedPower(std::uint32_t player, const std::string &power, ecs::Entity source) { ForPlayer(completedPowers, player).push_back({power, source}); }
	// isSpecialPowerTriggered / isSpecialPowerComplete: found (from anything, or the object given), then taken off.
	static bool Take(std::vector<std::vector<std::pair<std::string, ecs::Entity>>> &lists, std::uint32_t player, const std::string &name,
		std::optional<ecs::Entity> source)
	{
		auto &list = ForPlayer(lists, player);
		for (auto it = list.begin(); it != list.end(); ++it)
			if (it->first == name && (!source || *source == it->second))
			{
				list.erase(it);
				return true;
			}
		return false;
	}
	bool TakeTriggeredPower(std::uint32_t player, const std::string &power, std::optional<ecs::Entity> source = std::nullopt)
	{
		return Take(triggeredPowers, player, power, source);
	}
	bool TakeCompletedPower(std::uint32_t player, const std::string &power, std::optional<ecs::Entity> source = std::nullopt)
	{
		return Take(completedPowers, player, power, source);
	}
	// isUpgradeComplete / isScienceAcquired: found (from any building, or the one given), then taken off the list.
	bool TakeUpgrade(std::uint32_t player, const std::string &upgrade, std::optional<ecs::Entity> source = std::nullopt)
	{
		auto &list = ForPlayer(completedUpgrades, player);
		for (auto it = list.begin(); it != list.end(); ++it)
			if (it->first == upgrade && (!source || *source == it->second))
			{
				list.erase(it);
				return true;
			}
		return false;
	}
	bool TakeScience(std::uint32_t player, const std::string &science)
	{
		auto &list = ForPlayer(acquiredSciences, player);
		if (const auto it = std::find(list.begin(), list.end(), science); it != list.end())
		{
			list.erase(it);
			return true;
		}
		return false;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(objectTypeLists.size()));
		for (const auto &[name, types] : objectTypeLists)
		{
			writer.Text(name);
			writer.U32(static_cast<std::uint32_t>(types.size()));
			for (const std::string &type : types)
				writer.Text(type);
		}
		writer.U32(static_cast<std::uint32_t>(objectCounts.size()));
		for (const auto &counts : objectCounts)
		{
			writer.U32(static_cast<std::uint32_t>(counts.size()));
			for (const auto &[type, count] : counts)
			{
				writer.Text(type);
				writer.I64(count);
			}
		}
		writer.U32(static_cast<std::uint32_t>(completedUpgrades.size()));
		for (const auto &list : completedUpgrades)
		{
			writer.U32(static_cast<std::uint32_t>(list.size()));
			for (const auto &[upgrade, source] : list)
			{
				writer.Text(upgrade);
				writer.U32(source.index);
				writer.U32(source.generation);
			}
		}
		writer.U32(static_cast<std::uint32_t>(acquiredSciences.size()));
		for (const auto &list : acquiredSciences)
		{
			writer.U32(static_cast<std::uint32_t>(list.size()));
			for (const std::string &science : list)
				writer.Text(science);
		}
		for (const auto *powers : {&triggeredPowers, &completedPowers})
		{
			writer.U32(static_cast<std::uint32_t>(powers->size()));
			for (const auto &list : *powers)
			{
				writer.U32(static_cast<std::uint32_t>(list.size()));
				for (const auto &[power, source] : list)
				{
					writer.Text(power);
					writer.U32(source.index);
					writer.U32(source.generation);
				}
			}
		}
		for (const auto *sounds : {&testingSpeech, &testingAudio})
		{
			writer.U32(static_cast<std::uint32_t>(sounds->size()));
			for (const auto &[name, done] : *sounds)
			{
				writer.Text(name);
				writer.U64(done);
			}
		}
		writer.Flag(timeFrozen);
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		ScriptRecords loaded;
		const auto lists = reader.U32();
		for (std::uint32_t index = 0; lists && index < *lists; ++index)
		{
			auto name = reader.Text();
			const auto count = reader.U32();
			if (!name || !count)
				return false;
			std::vector<std::string> types;
			for (std::uint32_t type = 0; type < *count; ++type)
				if (auto text = reader.Text())
					types.push_back(std::move(*text));
				else
					return false;
			loaded.objectTypeLists.push_back({std::move(*name), std::move(types)});
		}
		const auto players = reader.U32();
		for (std::uint32_t player = 0; players && player < *players; ++player)
		{
			const auto count = reader.U32();
			if (!count)
				return false;
			auto &counts = loaded.objectCounts.emplace_back();
			for (std::uint32_t entry = 0; entry < *count; ++entry)
			{
				auto type = reader.Text();
				const auto value = reader.I64();
				if (!type || !value)
					return false;
				counts[std::move(*type)] = *value;
			}
		}
		const auto upgradePlayers = reader.U32();
		for (std::uint32_t player = 0; upgradePlayers && player < *upgradePlayers; ++player)
		{
			const auto count = reader.U32();
			if (!count)
				return false;
			auto &list = loaded.completedUpgrades.emplace_back();
			for (std::uint32_t entry = 0; entry < *count; ++entry)
			{
				auto upgrade = reader.Text();
				const auto index = reader.U32(), generation = reader.U32();
				if (!upgrade || !index || !generation)
					return false;
				ecs::Entity source;
				source.index = *index;
				source.generation = *generation;
				list.push_back({std::move(*upgrade), source});
			}
		}
		const auto sciencePlayers = reader.U32();
		for (std::uint32_t player = 0; sciencePlayers && player < *sciencePlayers; ++player)
		{
			const auto count = reader.U32();
			if (!count)
				return false;
			auto &list = loaded.acquiredSciences.emplace_back();
			for (std::uint32_t entry = 0; entry < *count; ++entry)
				if (auto science = reader.Text())
					list.push_back(std::move(*science));
				else
					return false;
		}
		for (auto *powers : {&loaded.triggeredPowers, &loaded.completedPowers})
		{
			const auto count = reader.U32();
			if (!count)
				return false;
			for (std::uint32_t player = 0; player < *count; ++player)
			{
				const auto entries = reader.U32();
				if (!entries)
					return false;
				auto &list = powers->emplace_back();
				for (std::uint32_t entry = 0; entry < *entries; ++entry)
				{
					auto power = reader.Text();
					const auto index = reader.U32(), generation = reader.U32();
					if (!power || !index || !generation)
						return false;
					ecs::Entity source;
					source.index = *index;
					source.generation = *generation;
					list.push_back({std::move(*power), source});
				}
			}
		}
		for (auto *sounds : {&loaded.testingSpeech, &loaded.testingAudio})
		{
			const auto count = reader.U32();
			if (!count)
				return false;
			for (std::uint32_t entry = 0; entry < *count; ++entry)
			{
				auto name = reader.Text();
				const auto done = reader.U64();
				if (!name || !done)
					return false;
				sounds->push_back({std::move(*name), *done});
			}
		}
		const auto frozen = reader.Flag();
		if (!frozen)
			return false;
		loaded.timeFrozen = *frozen;
		if (!lists || !players || !upgradePlayers || !sciencePlayers)
			return false;
		*this = std::move(loaded);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::ScriptRecords>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.script_records";
};
}
