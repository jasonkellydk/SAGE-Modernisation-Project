export module engine.gameplay.rts.stealth.systems.stealth_detector_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.stealth.components.stealth_detector;
export import engine.gameplay.rts.stealth.components.grant_stealth;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.rts.stealth.resources.detections;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.containment.resources.cargo_manifest;

// Stealth detectors, in parallel per chunk: on its scan tick a live, enabled
// detector (inside a garrison only if CanDetectWhileGarrisoned, inside any
// other container only if CanDetectWhileContained) reveals every stealthed
// thing of an enemy or neutral within its range whose classes it may detect,
// found in this tick's spatial index, until just past its next scan; and a
// garrisoned building so found (PartitionFilterStealthedOrStealthGarrisoned:
// the building's classes and side count) gives away those inside it with
// stealth that are not its player's or allies (markAsDetected(rate + 2)). The reveals are gathered by target, and the reveal pass
// marks them detected: shown to their enemies, targetable again. Stealth
// grantors (the GPS scrambler) give allies stealth the same way.
export namespace engine::gameplay
{
struct StealthDetectorSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Write<StealthDetector>, ecs::Optional<Health>, ecs::Optional<OffMap>, ecs::Optional<Disabled>>;
	using Lookup = ecs::Lookup<ecs::Read<Garrison>, ecs::Read<Stealth>, ecs::Read<Owner>, ecs::Read<TeamMember>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Write<DetectionOffers>, ecs::Write<Detections>,
		ecs::Write<DetectorPings>, ecs::Read<CargoManifest>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<DetectionOffers>().Reset(query.PreparedChunkCount());
		context.Write<DetectorPings>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		auto &offers = context.Write<DetectionOffers>().Slot(context);
		auto &pings = context.Write<DetectorPings>().Slot(context);
		const std::uint64_t tick = context.Tick();
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto teamRows = chunk.Get<TeamMember>();
		auto detectors = chunk.Get<StealthDetector>();
		const auto healths = chunk.Get<Health>();
		const auto offMap = chunk.Get<OffMap>();
		const auto lookup = context.Lookup<Lookup>();
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < detectors.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			StealthDetector &detector = detectors[row];
			if (!detector.Has(stealth_detector_flag::Enabled) || tick < detector.nextScan || (!healths.empty() && IsDead(healths[row])))
				continue;
			detector.nextScan = tick + detector.rate;
			if (!offMap.empty())
			{
				// Inside something: a garrisonable building (GarrisonContain) or any other container.
				const bool garrisoned = lookup.IsAlive(offMap[row].holder) && lookup.Get<Garrison>(offMap[row].holder) != nullptr;
				if (!detector.Has(garrisoned ? stealth_detector_flag::WhileGarrisoned : stealth_detector_flag::WhileContained))
					continue;
			}
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			const std::size_t before = offers.size();
			bool foundInside = false; // a stealthed occupant found, revealed or not (foundSomeone)
			// Revealed until just past the next scan.
			const std::uint64_t until = tick + detector.rate + 1;
			spatial.ForEachWithin(transforms[row].position.XY(), detector.range, [&](const SpatialEntry &entry) {
				if ((entry.classes & detector.forbiddenClasses) != 0)
					return;
				if ((entry.classes & target_class::Stealthed) == 0)
				{
					// Perhaps garrisoning something stealthy.
					if ((entry.classes & target_class::Structure) == 0 || lookup.Get<Garrison>(entry.entity) == nullptr)
						return;
					if (detector.requiredClasses != 0 && (entry.classes & detector.requiredClasses) == 0)
						return;
					if (relationships.Between(team, player, entry.team, entry.player) == Relationship::Allies)
						return;
					if (const Health *health = lookup.Get<Health>(entry.entity); health != nullptr && IsDead(*health))
						return;
					for (const ecs::Entity rider : manifest.Aboard(entry.entity))
					{
						if (lookup.Get<Stealth>(rider) == nullptr)
							continue;
						foundInside = true;
						const Owner *theirs = lookup.Get<Owner>(rider);
						const TeamMember *theirTeam = lookup.Get<TeamMember>(rider);
						if (theirs == nullptr || theirs->player == player ||
							relationships.Between(team, player, theirTeam != nullptr ? theirTeam->team : Relationships::NoTeam, theirs->player) == Relationship::Allies)
							continue;
						offers.push_back({rider, tick + detector.rate + 2, entities[row]});
					}
					return;
				}
				if (detector.requiredClasses != 0 && (entry.classes & detector.requiredClasses) == 0)
					return;
				if (relationships.Between(team, player, entry.team, entry.player) == Relationship::Allies)
					return;
				offers.push_back({entry.entity, until, entities[row]});
			});
			pings.push_back({entities[row], offers.size() > before || foundInside ? 1u : 0u, 0});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const { context.Write<Detections>().Gather(context.Write<DetectionOffers>()); }
};

// Stealth grantors, in parallel per chunk: each grows its radius and grants
// stealth to its allies of its classes within it (found in this tick's
// spatial index); after its final scan it is removed.
struct GrantStealthSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Write<GrantStealth>, ecs::OptionalWrite<Lifetime>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Write<GrantOffers>, ecs::Write<StealthGrants>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<GrantOffers>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		auto &offers = context.Write<GrantOffers>().Slot(context);
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto teamRows = chunk.Get<TeamMember>();
		auto grants = chunk.Get<GrantStealth>();
		auto lifetimes = chunk.Get<Lifetime>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < grants.size(); ++row)
		{
			GrantStealth &grant = grants[row];
			if (grant.done != 0)
				continue;
			grant.radius = grant.radius + grant.growRate;
			const bool last = grant.radius >= grant.finalRadius;
			if (last)
				grant.radius = grant.finalRadius;
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			spatial.ForEachWithin(transforms[row].position.XY(), grant.radius, [&](const SpatialEntry &entry) {
				if (entry.entity == entities[row] || (grant.classes != 0 && (entry.classes & grant.classes) == 0))
					return;
				if (relationships.Between(team, player, entry.team, entry.player) == Relationship::Allies)
					offers.push_back(entry.entity);
			});
			if (last)
			{
				grant.done = 1;
				if (!lifetimes.empty())
					lifetimes[row] = {context.Tick() + 1, 1, lifetimes[row].deathType};
			}
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const { context.Write<StealthGrants>().Gather(context.Write<GrantOffers>()); }
};

// The tick's reveals and grants, in parallel per chunk: detected stealthed
// things show (and are no longer hidden in their target classes); granted
// ones may stealth from now on.
struct StealthRevealSystem
{
	using Query = ecs::Query<ecs::Write<Stealth>, ecs::OptionalWrite<Targetable>>;
	using Resources = ecs::Resources<ecs::Read<Detections>, ecs::Read<StealthGrants>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const Detections &detections = context.Read<Detections>();
		const StealthGrants &grants = context.Read<StealthGrants>();
		if (detections.Count() == 0 && grants.Count() == 0)
			return;
		const std::uint64_t tick = context.Tick();
		auto stealths = chunk.Get<Stealth>();
		auto targetables = chunk.Get<Targetable>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < stealths.size(); ++row)
		{
			Stealth &stealth = stealths[row];
			if (grants.Count() != 0 && grants.Contains(entities[row]) && !stealth.Has(stealth_flag::CanStealth))
			{
				stealth.Set(stealth_flag::CanStealth, true);
				stealth.allowedAt = tick; // at once, as the original
			}
			const std::uint64_t until = detections.Count() != 0 ? detections.For(entities[row]) : 0;
			if (until == 0)
				continue;
			if (until > stealth.detectedUntil)
				stealth.detectedUntil = until;
			stealth.Set(stealth_flag::Detected, stealth.detectedUntil > tick);
			if (!targetables.empty() && !stealth.Hidden())
				targetables[row].classes &= ~target_class::Hidden;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::StealthDetectorSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_detectors";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After movement: it scans from where the detector ended up this tick.
	using Before = SystemTypeList<engine::gameplay::StealthRevealSystem>;
	using After = SystemTypeList<engine::gameplay::MovementSystem>;
};
template<>
struct SystemTraits<engine::gameplay::StealthRevealSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_reveal";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::StealthDetectorSystem, engine::gameplay::GrantStealthSystem>;
};
template<>
struct SystemTraits<engine::gameplay::GrantStealthSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.grant_stealth";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::StealthRevealSystem>;
	using After = SystemTypeList<engine::gameplay::MovementSystem>;
};
}
