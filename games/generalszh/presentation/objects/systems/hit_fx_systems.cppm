export module games.generalszh.presentation.objects.systems.hit_fx_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.combat.resources.garrison_kills;
export import engine.gameplay.rts.combat.components.missile;
export import engine.gameplay.common.lifetime.components.lifetime;
export import games.generalszh.presentation.objects.components.hit_fx;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// What hits look and sound like (ActiveBody::doDamageFX), once a tick after
// the simulation published the tick's hits, in the order dealt: the hit
// object's DamageFX (its armor set's, by its armor then) for the damage type
// and the attacker's veterancy, major at or above AmountForMajorFX, else
// minor, played on the hit object; the same damage type again on the same
// object waits out its ThrottleTime (a different type shows at once).
export namespace generalszh::presentation
{
struct HitFxSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Targetable>,
		ecs::Read<engine::gameplay::Experience>>;
	using SideTables = ecs::SideTables<ecs::Write<HitFxThrottle>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Hits>, ecs::Read<LookCatalog>, ecs::Write<FxRequests>, ecs::Read<engine::gameplay::GarrisonClears>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &fx = context.Write<FxRequests>().pending;
		auto &throttles = context.Side<SideTables, HitFxThrottle>();
		const std::uint64_t tick = context.Tick();
		context.Read<engine::gameplay::Hits>().ForEach([&](const engine::gameplay::Hit &hit) {
			const auto *definition = lookup.IsAlive(hit.target) ? lookup.Get<engine::gameplay::DefinitionRef>(hit.target) : nullptr;
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			const content::DamageFxTable *table = looks != nullptr ? looks->HitFxFor(catalog.ArmorName(hit.armor)) : nullptr;
			if (table == nullptr)
				return;
			HitFxThrottle *throttle = throttles.Get(hit.target);
			if (throttle != nullptr && hit.fxType == throttle->lastType && throttle->nextTick > tick)
				return;
			const auto *veteran = lookup.IsAlive(hit.source) ? lookup.Get<engine::gameplay::Experience>(hit.source) : nullptr;
			const std::uint32_t level = veteran != nullptr ? veteran->level : 0u;
			const content::DamageFxEntry *entry = table->At(hit.fxType, level);
			if (throttle == nullptr)
				throttle = throttles.Emplace(hit.target);
			throttle->lastType = hit.fxType;
			throttle->nextTick = tick + (entry != nullptr ? entry->throttleTicks : 0);
			const std::string *list = table->FxFor(hit.fxType, level, hit.amount);
			const auto *transform = lookup.Get<engine::gameplay::Transform>(hit.target);
			if (list == nullptr || transform == nullptr)
				return;
			const auto *body = lookup.Get<engine::gameplay::Targetable>(hit.target);
			FxRequest request{*list,
				{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y), Engine::Math::ToFloat(transform->position.z)},
				static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f, body != nullptr ? Engine::Math::ToFloat(body->radius) : 0.0f};
			request.object = hit.target;
			// DamageFX::doDamageFX: doFXObj(victim, source), the dealer (when it is still there) the second object (a
			// Ricochet particle nugget turns away from it).
			if (const auto *from = lookup.IsAlive(hit.source) ? lookup.Get<engine::gameplay::Transform>(hit.source) : nullptr)
			{
				request.hasSecondary = true;
				request.secondary = {Engine::Math::ToFloat(from->position.x), Engine::Math::ToFloat(from->position.y), Engine::Math::ToFloat(from->position.z)};
			}
			fx.push_back(std::move(request));
		});
		// A projectile that cleared a garrison: its GarrisonHitKillFX at the building (doFXObj on it).
		for (const engine::gameplay::GarrisonKill &kill : context.Read<engine::gameplay::GarrisonClears>().kills)
		{
			const DefinitionLooks *looks = kill.projectileDefinition != 0xFFFFFFFFu ? catalog.Of(kill.projectileDefinition) : nullptr;
			const auto *transform = lookup.IsAlive(kill.building) ? lookup.Get<engine::gameplay::Transform>(kill.building) : nullptr;
			if (looks == nullptr || looks->garrisonHitFx.empty() || transform == nullptr)
				continue;
			const auto *body = lookup.Get<engine::gameplay::Targetable>(kill.building);
			FxRequest request{looks->garrisonHitFx,
				{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y), Engine::Math::ToFloat(transform->position.z)},
				static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f, body != nullptr ? Engine::Math::ToFloat(body->radius) : 0.0f};
			request.object = kill.building;
			fx.push_back(std::move(request));
		}
	}
};

// BunkerBusterBehavior::update, once a tick: a bunker buster gone off and holding in its KILL_SELF state
// (MISSILE_KILLING_SELF: its MissileAIUpdate held KillSelfDelay before it is killed: here, a missile no longer flying
// whose lifetime runs out) plays its CrashThroughBunkerFX on itself every logic frame that leaves 1 over a multiple of
// CrashThroughBunkerFXFrequency ("not too much").
inline bool CrashFxFrame(std::uint64_t tick, std::uint32_t frequency) noexcept
{
	return frequency != 0 && tick % frequency == 1;
}

struct BunkerCrashFxSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Lifetime>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Optional<engine::gameplay::MissileFlight>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &fx = context.Write<FxRequests>().pending;
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			if (!chunk.template Get<engine::gameplay::MissileFlight>().empty())
				return; // still flying
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < definitions.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->crashThroughFx.empty() || !CrashFxFrame(tick, looks->crashThroughTicks))
					continue;
				const auto &at = transforms[row].position;
				FxRequest request{looks->crashThroughFx, {Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)},
					static_cast<float>(transforms[row].facing.units) * 6.283185307179586f / 4294967296.0f, 0.0f};
				request.object = entities[row];
				fx.push_back(std::move(request));
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::BunkerCrashFxSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bunker_crash_fx";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::HitFxSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.hit_fx";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
