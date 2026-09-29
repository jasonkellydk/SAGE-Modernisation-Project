export module engine.gameplay.rts.vision.systems.vision_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.vision.components.vision;
export import engine.gameplay.rts.vision.resources.shroud_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.carried;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.death.components.dying;

// Objects looking at the shroud, after the tick's moves, deaths and removals (the original's PartitionManager::update:
// cells touched, then processPendingUndoShroudRevealQueue). An object whose look was taken from anything that has since
// changed (Object::handlePartitionCellMaintenance: its partition cell, shroud clearing range, owner, container,
// construction, death, and with a reveal-to-all range its stealth) unlooks, the unlook coming back UnlookPersistDuration
// later, and looks again (Object::look) unless dead: for its player and allies (all players, KINDOF_REVEAL_TO_ALL; and
// whoever spies through it) out to its clearing range (its footprint while under construction), and, not under
// construction nor stealthed unseen, to its enemies and neutrals out to its reveal-to-all range. Contained, it looks only
// from a garrison (a tunnel or transport hides it). An object gone unlooks (Object::onDelete). Unlooks now due are undone.
export namespace engine::gameplay
{
namespace vision_key
{
inline constexpr std::uint32_t UnderConstruction = 1u << 0;
inline constexpr std::uint32_t Contained = 1u << 1;
inline constexpr std::uint32_t Dead = 1u << 2;
inline constexpr std::uint32_t Stealthed = 1u << 3;
inline constexpr std::uint32_t Detected = 1u << 4;
}

struct VisionSystem
{
	using Query = ecs::Query<ecs::Write<Vision>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Optional<UnderConstruction>,
		ecs::Optional<Carried>, ecs::Optional<Stealth>, ecs::Optional<Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<Garrison>>;
	using Resources = ecs::Resources<ecs::Read<Relationships>, ecs::Write<ShroudMap>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ShroudMap &map = context.Write<ShroudMap>();
		const Relationships &relationships = context.Read<Relationships>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t now = context.Tick();
		const std::uint64_t persist = map.UnlookPersist();
		if (map.Players() == 0)
			return;

		// Gone: its looks undone as it goes (onDelete), its slot free.
		for (std::uint32_t slot = 0; slot < map.SlotCount(); ++slot)
			if (Looker *looker = map.LookerAt(slot); looker != nullptr && !lookup.IsAlive(looker->entity))
			{
				map.Unlook(*looker, now, persist);
				map.Release(slot);
			}

		const std::uint32_t players = map.Players();
		const std::uint64_t everyone = players >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << players) - 1;
		query.ForEachChunk([&](auto chunk) {
			const auto entities = chunk.Entities();
			const auto visions = chunk.template Get<Vision>();
			const auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto building = chunk.template Get<UnderConstruction>();
			const auto carried = chunk.template Get<Carried>();
			const auto stealth = chunk.template Get<Stealth>();
			const auto dying = chunk.template Get<Dying>();
			for (std::size_t row = 0; row < visions.size(); ++row)
			{
				Vision &vision = visions[row];
				const ecs::Entity entity = entities[row];
				const auto position = transforms[row].position;
				const std::uint32_t player = owners[row].player;

				// Its partition cell (none off the map).
				auto cell = map.CellOf(position.x, position.y);
				const bool onMap = cell[0] >= 0 && cell[1] >= 0 && cell[0] < map.CellsX() && cell[1] < map.CellsY();
				std::uint32_t flags = 0;
				if (!building.empty())
					flags |= vision_key::UnderConstruction;
				if (!carried.empty())
					flags |= vision_key::Contained;
				if (!dying.empty())
					flags |= vision_key::Dead;
				// The stealth bits relook only an object with a reveal-to-all range (Object::setStatus).
				if (!stealth.empty() && vision.revealToAllRange > Engine::Math::Fixed{})
				{
					if ((stealth[row].flags & stealth_flag::Stealthed) != 0)
						flags |= vision_key::Stealthed;
					if ((stealth[row].flags & stealth_flag::Detected) != 0)
						flags |= vision_key::Detected;
				}

				Looker *looker = map.LookerAt(vision.slot);
				if (looker == nullptr || looker->entity != entity)
				{
					// Newly registered: it first looks on its first partition cell (m_lastCell starts null).
					vision.slot = map.Acquire(entity);
					looker = map.LookerAt(vision.slot);
					looker->keyPlayer = player;
					looker->keyFlags = flags;
					looker->keyRange = vision.clearingRange.Raw();
				}
				const std::int32_t keyX = onMap ? cell[0] : Looker::OffMap, keyY = onMap ? cell[1] : Looker::OffMap;
				if (looker->keyCellX == keyX && looker->keyCellY == keyY && looker->keyPlayer == player && looker->keyFlags == flags &&
					looker->keyRange == vision.clearingRange.Raw())
					continue;
				looker->keyCellX = keyX;
				looker->keyCellY = keyY;
				looker->keyPlayer = player;
				looker->keyFlags = flags;
				looker->keyRange = vision.clearingRange.Raw();

				// handleShroud: unlook, then look.
				map.Unlook(*looker, now, persist);
				if ((flags & vision_key::Dead) != 0)
					continue;
				// In a tunnel or transport: no look.
				if (!carried.empty())
				{
					const ecs::Entity carrier = carried[row].carrier;
					if (!lookup.IsAlive(carrier) || lookup.Get<Garrison>(carrier) == nullptr)
						continue;
				}
				const bool underConstruction = (flags & vision_key::UnderConstruction) != 0;
				const Engine::Math::Fixed range = underConstruction ? vision.footprintRange : vision.clearingRange;
				if (range > Engine::Math::Fixed{})
				{
					std::uint64_t mask = 0;
					if (vision.revealToAll != 0)
						mask = everyone;
					else
					{
						for (std::uint32_t other = 0; other < players; ++other)
							if (relationships.Allies(player, other))
								mask |= std::uint64_t{1} << other;
						mask |= vision.spiedMask;
					}
					map.Look(looker->look, cell[0], cell[1], map.CellsFor(range), mask);
				}
				// To everyone else, while seen.
				if (vision.revealToAllRange > Engine::Math::Fixed{} && !underConstruction)
				{
					const bool hidden = (flags & vision_key::Stealthed) != 0 && (flags & vision_key::Detected) == 0;
					if (!hidden)
					{
						std::uint64_t mask = 0;
						for (std::uint32_t other = 0; other < players; ++other)
							if (other != player && !relationships.Allies(player, other))
								mask |= std::uint64_t{1} << other;
						map.Look(looker->revealAll, cell[0], cell[1], map.CellsFor(vision.revealToAllRange), mask);
					}
				}
			}
		});

		map.ProcessPending(now);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::VisionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.vision";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The game orders it after the tick's deaths and removals (the partition update closes the frame).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
