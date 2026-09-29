export module games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.abilities.components.sticky_bomb;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.death.components.dying;

// StickyBombUpdate::update for every stuck-on bomb, chunk-parallel: a bomb whose target is effectively dead goes
// (destroyObject), one on something immobile stays where it was put, down on the ground (for mine clearers to reach),
// one on something else rides it OffsetZ up; once a second it pings. Each bomb writes only itself; the target is only
// read. What reaches beyond it (its removal, the ping's sound) goes out as its chunk's StickyBombEvents, for the
// session to carry out once the systems have run (ApplyStickyBombEvents).
export namespace generalszh::gameplay
{
struct StickyBombEvent
{
	enum class Kind : std::uint8_t
	{
		Destroy, // its target died: TheGameLogic->destroyObject(self)
		Ping,    // the "UnitBombPing" sound
	};
	ecs::Entity bomb;
	Kind kind{Kind::Ping};
	std::uint8_t reserved[7]{};
};

struct StickyBombEvents : ecs::ChunkOutputs<StickyBombEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::StickyBombEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.sticky_bomb_events";
};
}

export namespace generalszh::gameplay
{
struct StickyBombSystem
{
	using Query = ecs::Query<ecs::Write<StickyBomb>, ecs::Write<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Health>,
		ecs::Read<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::GroundHeight>, ecs::Write<StickyBombEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<StickyBombEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto lookup = context.Lookup<Lookup>();
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		auto &events = context.Write<StickyBombEvents>().Slot(context);
		const std::uint64_t tick = context.Tick();
		const std::uint64_t second = context.Time().Step().TicksPerSecond();
		auto bombs = chunk.Get<StickyBomb>();
		auto transforms = chunk.Get<gp::Transform>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < bombs.size(); ++row)
		{
			StickyBomb &bomb = bombs[row];
			gp::Transform &self = transforms[row];
			// getTargetObject: findObjectByID (a dying target is still found).
			if (bomb.target != ecs::Entity{} && lookup.IsAlive(bomb.target))
			{
				const auto *health = lookup.Get<gp::Health>(bomb.target);
				if (lookup.Get<gp::Dying>(bomb.target) != nullptr || (health != nullptr && gp::IsDead(*health)))
				{
					events.push_back({entities[row], StickyBombEvent::Kind::Destroy});
					continue;
				}
				const auto *ref = lookup.Get<gp::DefinitionRef>(bomb.target);
				if (ref != nullptr && templates.DefinitionAt(ref->index).Is("IMMOBILE"))
					self.position.z = ground.At(self.position.XY());
				else if (const auto *on = lookup.Get<gp::Transform>(bomb.target))
				{
					const StickyBombConfig *config = templates.StickyBombOf(refs[row].index);
					self.position = on->position;
					self.position.z += config != nullptr ? config->offsetZ : Engine::Math::Fixed::FromInt(10);
				}
			}
			if (tick >= bomb.nextPingTick)
			{
				bomb.nextPingTick += second;
				events.push_back({entities[row], StickyBombEvent::Kind::Ping});
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::StickyBombSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.sticky_bombs";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
