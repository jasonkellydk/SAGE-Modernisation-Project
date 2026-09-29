export module engine.gameplay.rts.collision.systems.crush_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.collision.systems.collision_systems;
export import engine.gameplay.rts.collision.components.squishable;
export import engine.gameplay.rts.collision.resources.crush_damage;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.resources.relationships;
export import Engine.Core.Math.FixedAngle;

// Being run over, in parallel per chunk: a crusher that is no ally of the
// body it touches, drives toward it and has it under its footprint crushes
// it when its crusher level beats the body's crushable level, or (for
// squishable infantry, the original's SquishCollide) is any crusher at all.
// The kill joins this tick's incoming damage once the chunks are done.
export namespace engine::gameplay
{
struct CrushSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Collider>, ecs::Read<Health>, ecs::Optional<Owner>, ecs::Optional<Squishable>>;
	using Resources = ecs::Resources<ecs::Read<Contacts>, ecs::Read<Relationships>, ecs::Read<CrushSettings>, ecs::Write<CrushDamage>,
		ecs::Write<IncomingDamage>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<CrushDamage>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const Contacts &contacts = context.Read<Contacts>();
		if (contacts.All().empty())
			return;
		const Relationships &relationships = context.Read<Relationships>();
		const CrushSettings &settings = context.Read<CrushSettings>();
		auto &out = context.Write<CrushDamage>().Slot(context);
		const auto transforms = chunk.Get<Transform>();
		const auto colliders = chunk.Get<Collider>();
		const auto healths = chunk.Get<Health>();
		const auto owners = chunk.Get<Owner>();
		const bool squishable = !chunk.Get<Squishable>().empty();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < colliders.size(); ++row)
		{
			if (IsDead(healths[row]))
				continue;
			const auto &at = transforms[row].position;
			for (const Contact &contact : contacts.For(entities[row]))
			{
				const bool crushes = contact.crusherLevel > colliders[row].crushableLevel || (squishable && contact.crusherLevel > 0);
				if (!crushes)
					continue;
				// It is the crusher that must not count the body an ally.
				if (!owners.empty() && relationships.Between(contact.moverPlayer, owners[row].player) == Relationship::Allies)
					continue;
				// Under its footprint (the body taken as a point, as the original's 1-unit squish radius)...
				const Engine::Math::Fixed reach = contact.moverRadius + Engine::Math::Fixed::One();
				if (Engine::Math::DistanceSquared(at.XY(), contact.moverPosition.XY()) > reach * reach)
					continue;
				// ... and driving toward it.
				const auto to = at.XY() - contact.moverPosition.XY();
				if (to.x * Engine::Math::Cos(contact.moverFacing) + to.y * Engine::Math::Sin(contact.moverFacing) <= Engine::Math::Fixed{})
					continue;
				out.push_back({entities[row], contact.mover, settings.amount, settings.damageType, settings.deathType});
				break;
			}
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		const CrushDamage &crushes = context.Write<CrushDamage>();
		if (crushes.Size() == 0)
			return;
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		crushes.ForEach([&](const DamageRecord &record) { incoming.Add(record); });
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::CrushSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.crush";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the contacts, before the tick's damage is applied.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<engine::gameplay::ContactSystem>;
};
}
