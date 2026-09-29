export module engine.gameplay.rts.combat.systems.assist_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.assisted_targeting;
export import engine.gameplay.rts.combat.resources.assists;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;

// Assisted targeting after the tick's shots (Weapon::processRequestAssistance, AssistedTargetingUpdate): each helper's
// shots at its victim count down its assist; spent, or its victim gone or no longer its target, the assist ends (its
// attack done, its slot's lock back as it was). Then each shot of a weapon that asks for help (RequestAssistRange), at
// a victim, asks its firer's player's others of the same kind within that range (from centres, 2D) that are free to
// help (able to attack: built, not sold or disabled; the weapon in hand ready to fire, AssistedTargetingUpdate::
// isFreeToAssist): each locks its assisting slot for the attack and attacks the victim for its assisting clip, and
// the tick's assists are told (the data streams the presentation draws).
export namespace engine::gameplay
{
struct AssistSystem
{
	using Query = ecs::Query<ecs::Read<AssistedTargeting>>;
	using Lookup = ecs::Lookup<ecs::Read<AssistedTargeting>, ecs::Read<Assisting>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Read<DefinitionRef>,
		ecs::Read<Armament>, ecs::Read<WeaponSlots>, ecs::Read<AttackTarget>, ecs::Read<Health>, ecs::Read<Disabled>, ecs::Read<UnderConstruction>,
		ecs::Read<Sale>>;
	using Resources = ecs::Resources<ecs::Read<FiredShots>, ecs::Read<WeaponCatalog>, ecs::Write<Assists>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const FiredShots &fired = context.Read<FiredShots>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		auto &assists = context.Write<Assists>().list;
		assists.clear();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		std::vector<Shot> shots;
		fired.ForEach([&](const Shot &shot) { shots.push_back(shot); });
		// Helpers under way this tick (their component changes land after the pass).
		std::vector<std::pair<ecs::Entity, Assisting>> helping;
		query.ForEachChunk([&](auto chunk) {
			const auto entities = chunk.Entities();
			for (const ecs::Entity entity : entities)
				if (const Assisting *assist = lookup.template Get<Assisting>(entity))
					helping.emplace_back(entity, *assist);
		});
		const auto endAssist = [&](ecs::Entity helper, const Assisting &assist) {
			commands.Remove<Assisting>(helper);
			if (const WeaponSlots *set = lookup.template Get<WeaponSlots>(helper); set != nullptr && set->locked == assist.slot)
			{
				WeaponSlots unlocked = *set;
				unlocked.locked = assist.previousLock;
				commands.Set<WeaponSlots>(helper, unlocked);
			}
			if (const AttackTarget *target = lookup.template Get<AttackTarget>(helper); target != nullptr && target->target == assist.victim)
				commands.Set<AttackTarget>(helper, AttackTarget{});
		};
		const auto dead = [&](ecs::Entity entity) {
			const Health *health = lookup.IsAlive(entity) ? lookup.template Get<Health>(entity) : nullptr;
			return !lookup.IsAlive(entity) || (health != nullptr && IsDead(*health));
		};
		for (auto &[helper, assist] : helping)
		{
			for (const Shot &shot : shots)
				if (shot.source == helper && shot.target == assist.victim && assist.shotsLeft > 0)
					--assist.shotsLeft;
			const AttackTarget *target = lookup.template Get<AttackTarget>(helper);
			if (assist.shotsLeft == 0 || dead(assist.victim) || target == nullptr || target->target != assist.victim)
				endAssist(helper, assist);
			else
				commands.Set<Assisting>(helper, assist);
		}
		// Who may help (AssistedTargetingUpdate::isFreeToAssist): able to attack, the weapon in hand ready to fire.
		const auto free = [&](ecs::Entity helper) {
			if (lookup.template Get<UnderConstruction>(helper) != nullptr || lookup.template Get<Sale>(helper) != nullptr || dead(helper))
				return false;
			if (const Disabled *off = lookup.template Get<Disabled>(helper); off != nullptr && off->mask != 0)
				return false;
			const Armament *armament = lookup.template Get<Armament>(helper);
			return armament != nullptr && armament->weapon != WeaponCatalog::None && armament->readyTick != OutOfAmmo && tick >= armament->readyTick;
		};
		std::vector<ecs::Entity> helpers;
		query.ForEachChunk([&](auto chunk) {
			for (const ecs::Entity entity : chunk.Entities())
				helpers.push_back(entity);
		});
		std::sort(helpers.begin(), helpers.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		for (const Shot &shot : shots)
		{
			if (shot.target == ecs::Entity{} || shot.weapon == WeaponCatalog::None || dead(shot.target))
				continue;
			const Engine::Math::Fixed range = weapons.At(shot.weapon).requestAssistRange;
			if (range <= Engine::Math::Fixed{})
				continue;
			const DefinitionRef *kind = lookup.template Get<DefinitionRef>(shot.source);
			const Transform *from = lookup.template Get<Transform>(shot.source);
			if (kind == nullptr || from == nullptr)
				continue;
			for (const ecs::Entity helper : helpers)
			{
				if (helper == shot.source)
					continue;
				const DefinitionRef *helperKind = lookup.template Get<DefinitionRef>(helper);
				const Owner *owner = lookup.template Get<Owner>(helper);
				const Transform *at = lookup.template Get<Transform>(helper);
				if (helperKind == nullptr || helperKind->index != kind->index || owner == nullptr || owner->player != shot.sourcePlayer || at == nullptr)
					continue;
				if (Engine::Math::DistanceSquared(at->position.XY(), from->position.XY()) > range * range || !free(helper))
					continue;
				const AssistedTargeting &assist = *lookup.template Get<AssistedTargeting>(helper);
				Assisting state{shot.target, assist.clip, assist.slot};
				if (const WeaponSlots *set = lookup.template Get<WeaponSlots>(helper))
				{
					// A previous assist's lock stays as the one to put back.
					const Assisting *already = lookup.template Get<Assisting>(helper);
					state.previousLock = already != nullptr ? already->previousLock : set->locked;
					WeaponSlots locked = *set;
					locked.locked = assist.slot;
					commands.Set<WeaponSlots>(helper, locked);
				}
				if (lookup.template Get<Assisting>(helper) != nullptr)
					commands.Set<Assisting>(helper, state);
				else
					commands.Add<Assisting>(helper, state);
				commands.Set<AttackTarget>(helper, AttackTarget{shot.target, true});
				assists.push_back({shot.source, helper, shot.target});
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AssistSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.assist";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the weapons fire.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
