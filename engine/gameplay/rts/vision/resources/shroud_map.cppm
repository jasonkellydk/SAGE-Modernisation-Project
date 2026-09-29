export module engine.gameplay.rts.vision.resources.shroud_map;
import std;

export import Engine.Core.Math.Fixed;
export import engine.core.serialization.byte_stream;
export import engine.ecs.core.entity_codec;
import engine.ecs.system.system;

// Who sees what (the original's PartitionManager shroud: PartitionCell::m_shroudLevel, doShroudReveal /
// undoShroudReveal / queueUndoShroudReveal / doShroudCover, revealMapForPlayer and the rest). The map is cut into
// cells (PartitionCellSize) from its origin; per player each cell has a current level (1 shrouded, 0 fogged, below 0
// cleared by that many lookers) and an active shroud level (shrouders). A reveal clears a disc of cells (DiscreteCircle,
// its radius the range in cells rounded up, at least 1) for every player of a mask; an object's unlook comes back
// UnlookPersistDuration later (queued, in order). Simulation state: checkpointed.
export namespace engine::gameplay
{
enum class CellShroud : std::uint8_t
{
	Clear,
	Fogged,
	Shrouded,
};

struct PendingUnlook
{
	std::int32_t cellX{0};
	std::int32_t cellY{0};
	std::int32_t cellRadius{1};
	std::uint64_t mask{0};
	std::uint64_t due{0}; // processed once the frame passes it
};

// One reveal an object has made (the original's SightingInfo: where, how far, for whom), in cells.
struct Sighting
{
	std::int32_t cellX{0};
	std::int32_t cellY{0};
	std::int32_t cellRadius{1};
	std::uint64_t mask{0};
	std::uint8_t valid{0};
	std::uint8_t reserved[7]{};
};

// A script's named standing reveal (ScriptEngine::NamedReveal), where and for whom it looks (invalid: its waypoint or
// player not found, so doing and undoing it do nothing).
struct NamedReveal
{
	std::string name;
	Sighting sighting;
};

// An object's standing looks (m_partitionLastLook, m_partitionRevealAllLastLook) and what they were taken from: the
// things whose change makes the original redo them (Object::handlePartitionCellMaintenance: its partition cell, its
// shroud clearing range, its owner, going into or out of a container, construction, death, and, with a reveal-to-all
// range, the stealth status bits). Kept here, not on the object, so a look outlives whatever removes its object.
struct Looker
{
	static constexpr std::int32_t OffMap = std::numeric_limits<std::int32_t>::min();
	static constexpr std::int32_t Relook = std::numeric_limits<std::int32_t>::max(); // forgotten: looks again

	ecs::Entity entity;
	std::int32_t keyCellX{OffMap};
	std::int32_t keyCellY{OffMap};
	std::uint32_t keyPlayer{0};
	std::uint32_t keyFlags{0};
	std::int64_t keyRange{0};
	Sighting look;
	Sighting revealAll;
};

class ShroudMap
{
public:
	// The playable extent from the origin, GameData's PartitionCellSize and UnlookPersistDuration (in frames).
	void Init(Engine::Math::Fixed width, Engine::Math::Fixed height, Engine::Math::Fixed cellSize, std::uint32_t players, std::uint64_t unlookPersist)
	{
		m_unlookPersist = unlookPersist;
		using Engine::Math::Fixed;
		m_cellSize = cellSize < Fixed::One() ? Fixed::One() : cellSize;
		width = std::max(width, Fixed::One());
		height = std::max(height, Fixed::One());
		m_cellsX = static_cast<std::int32_t>((width / m_cellSize).Ceil());
		m_cellsY = static_cast<std::int32_t>((height / m_cellSize).Ceil());
		m_players = players;
		m_current.assign(static_cast<std::size_t>(m_cellsX) * m_cellsY * players, 1);
		m_active.assign(m_current.size(), 0);
		m_pending.clear();
		m_lookers.clear();
		m_free.clear();
		m_named.clear();
	}

	// The looker pool: a slot per looking object, freed once its object is gone (its looks undone first).
	static constexpr std::uint32_t NoSlot = 0xFFFFFFFFu;
	std::uint32_t Acquire(ecs::Entity entity)
	{
		std::uint32_t slot;
		if (!m_free.empty())
		{
			slot = m_free.back();
			m_free.pop_back();
		}
		else
		{
			slot = static_cast<std::uint32_t>(m_lookers.size());
			m_lookers.emplace_back();
		}
		m_lookers[slot] = Looker{};
		m_lookers[slot].entity = entity;
		return slot;
	}
	void Release(std::uint32_t slot)
	{
		m_lookers[slot] = Looker{};
		m_free.push_back(slot);
	}
	Looker *LookerAt(std::uint32_t slot) noexcept { return slot < m_lookers.size() && m_lookers[slot].entity != ecs::Entity{} ? &m_lookers[slot] : nullptr; }
	std::size_t SlotCount() const noexcept { return m_lookers.size(); }
	ecs::Entity SlotEntity(std::uint32_t slot) const noexcept { return m_lookers[slot].entity; }

	// ScriptEngine::createNamedMapReveal (a name already there keeps its reveal), findNamedReveal, removeNamedMapReveal.
	void AddNamedReveal(std::string name, Sighting sighting)
	{
		if (FindNamedReveal(name) == nullptr)
			m_named.push_back({std::move(name), sighting});
	}
	const NamedReveal *FindNamedReveal(std::string_view name) const noexcept
	{
		const auto found = std::ranges::find(m_named, name, &NamedReveal::name);
		return found == m_named.end() ? nullptr : &*found;
	}
	void RemoveNamedReveal(std::string_view name)
	{
		if (const auto found = std::ranges::find(m_named, name, &NamedReveal::name); found != m_named.end())
			m_named.erase(found);
	}

	// Object::unlook: its looks come back `persist` frames on.
	void Unlook(Looker &looker, std::uint64_t now, std::uint64_t persist)
	{
		for (Sighting *sighting : {&looker.look, &looker.revealAll})
			if (sighting->valid != 0)
			{
				QueueUnreveal(sighting->cellX, sighting->cellY, sighting->cellRadius, sighting->mask, now, persist);
				*sighting = Sighting{};
			}
	}
	void Look(Sighting &sighting, std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask)
	{
		Reveal(cx, cy, radius, mask);
		sighting = Sighting{cx, cy, radius, mask, 1, {}};
	}

	std::int32_t CellsX() const noexcept { return m_cellsX; }
	std::int32_t CellsY() const noexcept { return m_cellsY; }
	std::uint32_t Players() const noexcept { return m_players; }
	std::uint64_t UnlookPersist() const noexcept { return m_unlookPersist; }
	Engine::Math::Fixed CellSize() const noexcept { return m_cellSize; }

	// worldToCell / worldToCellDist.
	std::array<std::int32_t, 2> CellOf(Engine::Math::Fixed x, Engine::Math::Fixed y) const noexcept
	{
		return {static_cast<std::int32_t>((x / m_cellSize).Floor()), static_cast<std::int32_t>((y / m_cellSize).Floor())};
	}
	std::int32_t CellsFor(Engine::Math::Fixed range) const noexcept { return std::max<std::int32_t>(static_cast<std::int32_t>((range / m_cellSize).Ceil()), 1); }

	CellShroud Status(std::uint32_t player, std::int32_t x, std::int32_t y) const noexcept
	{
		if (player >= m_players || x < 0 || y < 0 || x >= m_cellsX || y >= m_cellsY)
			return CellShroud::Shrouded;
		const std::int16_t level = m_current[Index(player, x, y)];
		return level == 1 ? CellShroud::Shrouded : level == 0 ? CellShroud::Fogged : CellShroud::Clear;
	}
	CellShroud StatusAt(std::uint32_t player, Engine::Math::Fixed x, Engine::Math::Fixed y) const noexcept
	{
		const auto cell = CellOf(x, y);
		return Status(player, cell[0], cell[1]);
	}

	// TerrainLogic::setActiveBoundary's partition reset to a new playable extent: the pending unlooks done; each player's
	// fogged cells kept (storeFoggedCells to fog), the objects' looks forgotten, not undone (friend_prepareForMapBoundary
	// Adjust: what they saw stays), every cell still clear kept as revealed for good (storeFoggedCells); the new grid all
	// shrouded, the revealed cells given a looker, the fogged ones fogged (restoreFoggedCells, which stops at the first
	// row past the new grid for every player at once, as the original's); the objects look again as they next update.
	void SwitchExtent(Engine::Math::Fixed width, Engine::Math::Fixed height)
	{
		using Engine::Math::Fixed;
		ProcessPending(std::nullopt);
		enum : std::uint8_t { DontTouch, Fog, Revealed };
		const std::int32_t oldX = m_cellsX, oldY = m_cellsY;
		std::vector<std::uint8_t> store(static_cast<std::size_t>(oldX) * oldY * m_players, DontTouch);
		const auto at = [&](std::uint32_t player, std::int32_t x, std::int32_t y) {
			return (static_cast<std::size_t>(player) * oldY + static_cast<std::size_t>(y)) * oldX + static_cast<std::size_t>(x);
		};
		for (std::uint32_t player = 0; player < m_players; ++player)
			ForEachCell([&](std::int32_t x, std::int32_t y) {
				if (Status(player, x, y) == CellShroud::Fogged)
					store[at(player, x, y)] = Fog;
			});
		for (Looker &looker : m_lookers)
		{
			looker.look.valid = 0;
			looker.revealAll.valid = 0;
			looker.keyCellX = looker.keyCellY = Looker::Relook;
		}
		for (std::uint32_t player = 0; player < m_players; ++player)
			ForEachCell([&](std::int32_t x, std::int32_t y) {
				if (Status(player, x, y) == CellShroud::Clear)
					store[at(player, x, y)] = Revealed;
			});
		width = std::max(width, Fixed::One());
		height = std::max(height, Fixed::One());
		m_cellsX = static_cast<std::int32_t>((width / m_cellSize).Ceil());
		m_cellsY = static_cast<std::int32_t>((height / m_cellSize).Ceil());
		m_current.assign(static_cast<std::size_t>(m_cellsX) * m_cellsY * m_players, 1);
		m_active.assign(m_current.size(), 0);
		const auto restore = [&](bool toFog) {
			for (std::uint32_t player = 0; player < m_players; ++player)
				for (std::int32_t y = 0; y < oldY; ++y)
				{
					if (y >= m_cellsY)
						return;
					for (std::int32_t x = 0; x < oldX && x < m_cellsX; ++x)
					{
						const std::uint8_t stored = store[at(player, x, y)];
						if (stored == Fog && toFog)
						{
							AddLooker(player, x, y);
							RemoveLooker(player, x, y);
						}
						if (stored == Revealed && !toFog)
							AddLooker(player, x, y);
					}
				}
		};
		restore(false);
		restore(true);
	}

	// doShroudReveal / undoShroudReveal / doShroudCover / undoShroudCover over a disc of cells, players highest first.
	void Reveal(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask) { Disc(cx, cy, radius, mask, Op::AddLooker); }
	void Unreveal(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask) { Disc(cx, cy, radius, mask, Op::RemoveLooker); }
	void Cover(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask) { Disc(cx, cy, radius, mask, Op::AddShrouder); }
	void Uncover(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask) { Disc(cx, cy, radius, mask, Op::RemoveShrouder); }

	// queueUndoShroudReveal: it comes back `persist` frames on.
	void QueueUnreveal(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask, std::uint64_t now, std::uint64_t persist)
	{
		m_pending.push_back({cx, cy, radius, mask, now + persist});
	}

	// processPendingUndoShroudRevealQueue: every unlook due before `now` (all of them: no time).
	void ProcessPending(std::optional<std::uint64_t> now)
	{
		while (!m_pending.empty() && (!now || m_pending.front().due < *now))
		{
			const PendingUnlook unlook = m_pending.front();
			m_pending.pop_front();
			Unreveal(unlook.cellX, unlook.cellY, unlook.cellRadius, unlook.mask);
		}
	}
	std::size_t PendingCount() const noexcept { return m_pending.size(); }

	// revealMapForPlayer: a look and an unlook everywhere (fog where nobody looks).
	void RevealAll(std::uint32_t player)
	{
		ForEachCell([&](std::int32_t x, std::int32_t y) {
			AddLooker(player, x, y);
			RemoveLooker(player, x, y);
		});
	}
	// revealMapForPlayerPermanently / undoRevealMapForPlayerPermanently.
	void RevealAllPermanently(std::uint32_t player) { ForEachCell([&](std::int32_t x, std::int32_t y) { AddLooker(player, x, y); }); }
	void UndoRevealAllPermanently(std::uint32_t player)
	{
		ProcessPending(std::nullopt);
		ForEachCell([&](std::int32_t x, std::int32_t y) { RemoveLooker(player, x, y); });
	}
	// shroudMapForPlayer: passive shroud over everything not looked at.
	void ShroudAll(std::uint32_t player)
	{
		ProcessPending(std::nullopt);
		ForEachCell([&](std::int32_t x, std::int32_t y) {
			AddShrouder(player, x, y);
			RemoveShrouder(player, x, y);
		});
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U64(m_unlookPersist);
		writer.I64(m_cellSize.Raw());
		writer.I64(m_cellsX);
		writer.I64(m_cellsY);
		writer.U32(m_players);
		for (const std::int16_t level : m_current)
			writer.I64(level);
		for (const std::int16_t level : m_active)
			writer.I64(level);
		writer.U32(static_cast<std::uint32_t>(m_pending.size()));
		for (const PendingUnlook &unlook : m_pending)
		{
			writer.I64(unlook.cellX);
			writer.I64(unlook.cellY);
			writer.I64(unlook.cellRadius);
			writer.U64(unlook.mask);
			writer.U64(unlook.due);
		}
		writer.U32(static_cast<std::uint32_t>(m_lookers.size()));
		for (const Looker &looker : m_lookers)
		{
			ecs::WriteEntity(writer, looker.entity);
			writer.I64(looker.keyCellX);
			writer.I64(looker.keyCellY);
			writer.U32(looker.keyPlayer);
			writer.U32(looker.keyFlags);
			writer.I64(looker.keyRange);
			for (const Sighting *sighting : {&looker.look, &looker.revealAll})
			{
				writer.I64(sighting->cellX);
				writer.I64(sighting->cellY);
				writer.I64(sighting->cellRadius);
				writer.U64(sighting->mask);
				writer.U8(sighting->valid);
			}
		}
		writer.U32(static_cast<std::uint32_t>(m_free.size()));
		for (const std::uint32_t slot : m_free)
			writer.U32(slot);
		writer.U32(static_cast<std::uint32_t>(m_named.size()));
		for (const NamedReveal &reveal : m_named)
		{
			writer.Text(reveal.name);
			writer.I64(reveal.sighting.cellX);
			writer.I64(reveal.sighting.cellY);
			writer.I64(reveal.sighting.cellRadius);
			writer.U64(reveal.sighting.mask);
			writer.U8(reveal.sighting.valid);
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto persist = reader.U64();
		if (!persist)
			return false;
		const auto size = reader.I64(), cellsX = reader.I64(), cellsY = reader.I64();
		const auto players = reader.U32();
		if (!size || !cellsX || !cellsY || !players || *cellsX < 0 || *cellsY < 0 || *cellsX > 4096 || *cellsY > 4096 || *players > 64)
			return false;
		const std::size_t count = static_cast<std::size_t>(*cellsX) * static_cast<std::size_t>(*cellsY) * *players;
		std::vector<std::int16_t> current(count), active(count);
		for (auto *levels : {&current, &active})
			for (std::int16_t &level : *levels)
			{
				const auto value = reader.I64();
				if (!value)
					return false;
				level = static_cast<std::int16_t>(*value);
			}
		const auto pendingCount = reader.U32();
		if (!pendingCount || *pendingCount > 1000000)
			return false;
		std::deque<PendingUnlook> pending;
		for (std::uint32_t index = 0; index < *pendingCount; ++index)
		{
			const auto x = reader.I64(), y = reader.I64(), radius = reader.I64();
			const auto mask = reader.U64(), due = reader.U64();
			if (!x || !y || !radius || !mask || !due)
				return false;
			pending.push_back({static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y), static_cast<std::int32_t>(*radius), *mask, *due});
		}
		const auto lookerCount = reader.U32();
		if (!lookerCount || *lookerCount > 10000000)
			return false;
		std::vector<Looker> lookers(*lookerCount);
		for (Looker &looker : lookers)
		{
			const auto entity = ecs::ReadEntity(reader);
			const auto keyX = reader.I64(), keyY = reader.I64();
			const auto player = reader.U32(), flags = reader.U32();
			const auto range = reader.I64();
			if (!entity || !keyX || !keyY || !player || !flags || !range)
				return false;
			looker.entity = *entity;
			looker.keyCellX = static_cast<std::int32_t>(*keyX);
			looker.keyCellY = static_cast<std::int32_t>(*keyY);
			looker.keyPlayer = *player;
			looker.keyFlags = *flags;
			looker.keyRange = *range;
			for (Sighting *sighting : {&looker.look, &looker.revealAll})
			{
				const auto x = reader.I64(), y = reader.I64(), radius = reader.I64();
				const auto mask = reader.U64();
				const auto valid = reader.U8();
				if (!x || !y || !radius || !mask || !valid)
					return false;
				*sighting = Sighting{static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y), static_cast<std::int32_t>(*radius), *mask, *valid, {}};
			}
		}
		const auto freeCount = reader.U32();
		if (!freeCount || *freeCount > lookers.size())
			return false;
		std::vector<std::uint32_t> free(*freeCount);
		for (std::uint32_t &slot : free)
		{
			const auto value = reader.U32();
			if (!value || *value >= lookers.size())
				return false;
			slot = *value;
		}
		const auto namedCount = reader.U32();
		if (!namedCount || *namedCount > 100000)
			return false;
		std::vector<NamedReveal> named(*namedCount);
		for (NamedReveal &reveal : named)
		{
			const auto name = reader.Text();
			const auto x = reader.I64(), y = reader.I64(), radius = reader.I64();
			const auto mask = reader.U64();
			const auto valid = reader.U8();
			if (!name || !x || !y || !radius || !mask || !valid)
				return false;
			reveal = {*name, Sighting{static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y), static_cast<std::int32_t>(*radius), *mask, *valid, {}}};
		}
		m_unlookPersist = *persist;
		m_cellSize = Engine::Math::Fixed::FromRaw(*size);
		m_cellsX = static_cast<std::int32_t>(*cellsX);
		m_cellsY = static_cast<std::int32_t>(*cellsY);
		m_players = *players;
		m_current = std::move(current);
		m_active = std::move(active);
		m_pending = std::move(pending);
		m_lookers = std::move(lookers);
		m_free = std::move(free);
		m_named = std::move(named);
		return true;
	}

private:
	enum class Op : std::uint8_t
	{
		AddLooker,
		RemoveLooker,
		AddShrouder,
		RemoveShrouder,
	};

	std::size_t Index(std::uint32_t player, std::int32_t x, std::int32_t y) const noexcept
	{
		return (static_cast<std::size_t>(player) * m_cellsY + static_cast<std::size_t>(y)) * m_cellsX + static_cast<std::size_t>(x);
	}

	template<typename F>
	void ForEachCell(F &&visit)
	{
		for (std::int32_t y = 0; y < m_cellsY; ++y)
			for (std::int32_t x = 0; x < m_cellsX; ++x)
				visit(x, y);
	}

	// PartitionCell::addLooker: a 1 or 0 goes straight to -1, a clear cell one looker more.
	void AddLooker(std::uint32_t player, std::int32_t x, std::int32_t y)
	{
		std::int16_t &level = m_current[Index(player, x, y)];
		level = std::min<std::int16_t>(static_cast<std::int16_t>(level - 1), -1);
	}
	// removeLooker: the last looker leaves the cell fogged, or shrouded under active shroud.
	void RemoveLooker(std::uint32_t player, std::int32_t x, std::int32_t y)
	{
		std::int16_t &level = m_current[Index(player, x, y)];
		if (level == -1)
			level = std::min<std::int16_t>(m_active[Index(player, x, y)], 1);
		else
			++level;
	}
	// addShrouder / removeShrouder.
	void AddShrouder(std::uint32_t player, std::int32_t x, std::int32_t y)
	{
		++m_active[Index(player, x, y)];
		if (m_current[Index(player, x, y)] == 0)
			m_current[Index(player, x, y)] = 1;
	}
	void RemoveShrouder(std::uint32_t player, std::int32_t x, std::int32_t y) { --m_active[Index(player, x, y)]; }

	// DiscreteCircle(cx, cy, radius).drawCircle: its scanlines (the top half, mirrored about the centre row).
	void Disc(std::int32_t cx, std::int32_t cy, std::int32_t radius, std::uint64_t mask, Op op)
	{
		struct Line
		{
			std::int32_t xStart, xEnd, y;
		};
		std::vector<Line> edges;
		std::int32_t x = 0, y = radius, d = (1 - radius) << 1;
		while (y >= 0)
		{
			edges.push_back({cx - x, cx + x, cy + y});
			if (d + y > 0)
			{
				--y;
				d -= ((y << 1) - 1);
			}
			if (x > d)
			{
				++x;
				d += ((x << 1) + 1);
			}
		}
		// removeDuplicates: of lines on the same row, the last.
		std::vector<Line> lines;
		for (std::size_t index = 0; index < edges.size(); ++index)
			if (index + 1 == edges.size() || edges[index].y != edges[index + 1].y)
				lines.push_back(edges[index]);
		for (std::int32_t player = static_cast<std::int32_t>(m_players) - 1; player >= 0; --player)
		{
			if ((mask & (std::uint64_t{1} << player)) == 0)
				continue;
			const auto scan = [&](std::int32_t x1, std::int32_t x2, std::int32_t row) {
				if (row < 0 || row >= m_cellsY || x1 >= m_cellsX || x2 < 0)
					return;
				for (std::int32_t column = std::max(x1, 0); column <= std::min(x2, m_cellsX - 1); ++column)
					switch (op)
					{
					case Op::AddLooker: AddLooker(static_cast<std::uint32_t>(player), column, row); break;
					case Op::RemoveLooker: RemoveLooker(static_cast<std::uint32_t>(player), column, row); break;
					case Op::AddShrouder: AddShrouder(static_cast<std::uint32_t>(player), column, row); break;
					case Op::RemoveShrouder: RemoveShrouder(static_cast<std::uint32_t>(player), column, row); break;
					}
			};
			for (const Line &line : lines)
			{
				scan(line.xStart, line.xEnd, line.y);
				if (line.y != cy)
					scan(line.xStart, line.xEnd, (cy << 1) - line.y);
			}
		}
	}

	Engine::Math::Fixed m_cellSize{Engine::Math::Fixed::FromInt(40)};
	std::int32_t m_cellsX{0}, m_cellsY{0};
	std::uint32_t m_players{0};
	std::uint64_t m_unlookPersist{150};
	std::vector<std::int16_t> m_current; // per player, per cell
	std::vector<std::int16_t> m_active;
	std::deque<PendingUnlook> m_pending;
	std::vector<Looker> m_lookers;
	std::vector<std::uint32_t> m_free;
	std::vector<NamedReveal> m_named;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ShroudMap>
{
	static constexpr std::string_view StableName = "engine.gameplay.shroud_map";
};
}
