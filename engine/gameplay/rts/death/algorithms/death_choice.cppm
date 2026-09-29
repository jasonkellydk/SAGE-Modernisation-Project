export module engine.gameplay.rts.death.algorithms.death_choice;
import std;

export import engine.gameplay.rts.death.definitions.death_definition;
export import Engine.Core.Math.FixedRandom;

// The choices a death makes, deterministic from the entity's random stream:
// which slow death (weighted by probability plus the overkill bonus, among
// those whose filter applies), how long its delays are, and which candidate
// effect a phase plays.
export namespace engine::gameplay
{
std::optional<std::uint32_t> ChooseSlowDeath(const DeathDefinition &definition, std::uint32_t deathType, std::uint32_t veterancy,
	Engine::Math::Fixed overkill, Engine::Math::RandomStream &random, std::uint64_t status = 0)
{
	std::vector<std::int64_t> weights;
	std::int64_t total = 0;
	for (const SlowDeathDefinition &slow : definition.slow)
	{
		std::int64_t weight = 0;
		if (slow.filter.Applies(deathType, veterancy, status))
		{
			// The original: probability + (overkill share * bonus share), truncated, at least 1.
			const Engine::Math::Fixed bonus = overkill * slow.overkillBonus;
			weight = static_cast<std::int64_t>(slow.probability) + bonus.Floor();
			weight = weight < 1 ? 1 : weight;
		}
		weights.push_back(weight);
		total += weight;
	}
	if (total == 0)
		return std::nullopt;
	std::int64_t roll = Engine::Math::UniformInt(random, 1, total);
	for (std::uint32_t index = 0; index < weights.size(); ++index)
	{
		roll -= weights[index];
		if (roll <= 0 && weights[index] > 0)
			return index;
	}
	return std::nullopt;
}

std::uint64_t VariedDelay(std::uint64_t base, std::uint64_t variance, Engine::Math::RandomStream &random)
{
	return base + (variance == 0 ? 0u : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(variance))));
}

// One of `candidates` (none when empty).
std::optional<std::uint32_t> PickEffect(const std::vector<std::uint32_t> &candidates, Engine::Math::RandomStream &random)
{
	if (candidates.empty())
		return std::nullopt;
	return candidates[static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(candidates.size()) - 1))];
}
}
