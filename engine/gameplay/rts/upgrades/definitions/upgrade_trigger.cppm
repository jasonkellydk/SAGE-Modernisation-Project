export module engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
import std;

// Upgrades as bits (the original's UpgradeMaskType, one bit per upgrade
// template), and what makes an object's upgrade-driven behaviour go (the
// original's UpgradeMux: TriggeredBy, ConflictsWith, RemovesUpgrades,
// RequiresAllTriggers). What each trigger then does is the game's: its
// `reaction` is a game-defined number the game's systems act on.
export namespace engine::gameplay
{
struct UpgradeMask
{
	static constexpr std::size_t Words = 4;
	static constexpr std::size_t Capacity = Words * 64;
	std::array<std::uint64_t, Words> bits{};

	static UpgradeMask Of(std::uint32_t upgrade) noexcept
	{
		UpgradeMask mask;
		mask.Set(upgrade);
		return mask;
	}
	void Set(std::uint32_t upgrade) noexcept
	{
		if (upgrade < Capacity)
			bits[upgrade / 64] |= std::uint64_t{1} << (upgrade % 64);
	}
	bool Has(std::uint32_t upgrade) const noexcept { return upgrade < Capacity && ((bits[upgrade / 64] >> (upgrade % 64)) & 1u) != 0; }
	void Add(const UpgradeMask &other) noexcept
	{
		for (std::size_t word = 0; word < Words; ++word)
			bits[word] |= other.bits[word];
	}
	void Remove(const UpgradeMask &other) noexcept
	{
		for (std::size_t word = 0; word < Words; ++word)
			bits[word] &= ~other.bits[word];
	}
	bool Any() const noexcept { return (bits[0] | bits[1] | bits[2] | bits[3]) != 0; }
	bool AnyOf(const UpgradeMask &other) const noexcept
	{
		for (std::size_t word = 0; word < Words; ++word)
			if ((bits[word] & other.bits[word]) != 0)
				return true;
		return false;
	}
	bool AllOf(const UpgradeMask &other) const noexcept
	{
		for (std::size_t word = 0; word < Words; ++word)
			if ((bits[word] & other.bits[word]) != other.bits[word])
				return false;
		return true;
	}
	friend bool operator==(const UpgradeMask &left, const UpgradeMask &right) noexcept
	{
		for (std::size_t word = 0; word < Words; ++word)
			if (left.bits[word] != right.bits[word])
				return false;
		return true;
	}
};

struct UpgradeTrigger
{
	UpgradeMask activation;  // TriggeredBy
	UpgradeMask conflicting; // ConflictsWith
	UpgradeMask removal;     // RemovesUpgrades (the object's own)
	bool requiresAll{false}; // RequiresAllTriggers
	std::uint32_t reaction{0};
};

// UpgradeMux::wouldUpgrade: it has triggers, has not gone yet, nothing it conflicts with is there, and any
// (or with RequiresAllTriggers every) trigger is.
inline bool WouldUpgrade(const UpgradeTrigger &trigger, const UpgradeMask &key, bool executed) noexcept
{
	if (!trigger.activation.Any() || !key.Any() || executed || key.AnyOf(trigger.conflicting))
		return false;
	return trigger.requiresAll ? key.AllOf(trigger.activation) : key.AnyOf(trigger.activation);
}
}
