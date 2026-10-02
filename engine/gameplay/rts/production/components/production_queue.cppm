export module engine.gameplay.rts.production.components.production_queue;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// What a factory is building (the original's ProductionUpdate): a queue of
// units, the front one under construction for its build time; each entry
// makes `quantity` units (the original's quantity modifiers) that join
// `team` when done. Paid for when queued. As the original's update, the
// front entry counts its updates and is done once they reach its build time
// as its player's power stands now (calcTimeToBuild each update), so power
// coming back finishes what was slowed at once. An upgrade under research is an
// entry too (its `definition` the upgrade's bit): it takes its build time
// whatever the power, and needs no door.
export namespace engine::gameplay
{
enum class ProductionKind : std::uint32_t
{
	Unit,
	Upgrade,
};

struct ProductionEntry
{
	std::uint32_t definition{0};
	std::uint32_t team{0};
	std::uint32_t quantity{1};
	std::uint32_t productionId{0}; // its factory's id for it (ProductionUpdate::requestUniqueUnitID: what cancels it)
	std::uint64_t frames{0};      // m_framesUnderConstruction: the production updates it has had
	std::uint64_t ticksTotal{1};  // calcTimeToBuild at full power (a unit's BuildTime, an upgrade's)
	ProductionKind kind{ProductionKind::Unit};
	std::uint32_t produced{0}; // of its quantity, those already out (one at a time through a gated exit)
	std::int64_t paid{0}; // what it cost when queued (given back if it is cancelled)
	// calcTimeToBuild as its last update had it (its player's power stretches a unit's): m_percentComplete is frames
	// over it (0: not updated yet, ticksTotal).
	std::uint64_t ticksNow{0};
};

struct ProductionQueue
{
	static constexpr std::uint32_t MaxEntries = 9;
	std::array<ProductionEntry, MaxEntries> entries{};
	std::uint32_t count{0};
	std::uint32_t capacity{MaxEntries};
	std::uint32_t nextId{1}; // the next production id it hands out
	// The disabled types it still builds under (ProductionUpdate DisabledTypesToProcess; default DISABLED_HELD, 1 << 3).
	std::uint32_t runsWhileDisabled{1u << 3};
	// The tick whose production update a cancellation already spent (the front cancelled as no longer allowed: the
	// update returns there), so the production system leaves the queue alone that tick.
	std::uint64_t spentTick{0};

	bool Full() const noexcept { return count >= capacity || count >= MaxEntries; }

	bool Push(const ProductionEntry &entry) noexcept
	{
		if (Full())
			return false;
		entries[count++] = entry;
		return true;
	}

	// Takes out the entry at `index` (a cancelled one), the rest moving up.
	void RemoveAt(std::uint32_t index) noexcept
	{
		if (index >= count)
			return;
		for (std::uint32_t next = index + 1; next < count; ++next)
			entries[next - 1] = entries[next];
		entries[--count] = {};
	}

	void PopFront() noexcept
	{
		for (std::uint32_t index = 1; index < count; ++index)
			entries[index - 1] = entries[index];
		if (count > 0)
			entries[--count] = {};
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ProductionQueue>
{
	static constexpr std::string_view StableName = "engine.gameplay.production_queue";
	static constexpr std::uint32_t Version = 8;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
