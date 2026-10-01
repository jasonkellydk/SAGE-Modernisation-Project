export module engine.gameplay.rts.containment.systems.container_classes_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;

// Before the step, chunk-parallel: what a container holds, as its target classes (the spatial index carries them to
// whatever estimates a weapon's damage against it, WeaponTemplate::estimateWeaponDamage's getContain() tests): Emptied
// while it holds nobody (getContainCount() == 0), Clearable while a garrison (GarrisonContain, not
// ImmuneToClearBuildingAttacks) holds someone; ArmedContainer while it is able to attack (Object::isAbleToAttack: built,
// not being sold, and armed itself or letting its riders fire with someone aboard).
export namespace engine::gameplay
{
struct ContainerClassesSystem
{
	using Query = ecs::Query<ecs::Read<Transport>, ecs::Write<Targetable>, ecs::Optional<Garrison>, ecs::Optional<Armament>, ecs::Optional<UnderConstruction>,
		ecs::Optional<Sale>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		const auto transports = chunk.Get<Transport>();
		auto targetables = chunk.Get<Targetable>();
		const auto garrisons = chunk.Get<Garrison>();
		const auto armaments = chunk.Get<Armament>();
		const bool unbuilt = !chunk.Get<UnderConstruction>().empty() || !chunk.Get<Sale>().empty();
		for (std::size_t row = 0; row < transports.size(); ++row)
		{
			std::uint32_t &classes = targetables[row].classes;
			classes &= ~(target_class::Emptied | target_class::Clearable | target_class::ArmedContainer);
			const bool armed = !armaments.empty() && armaments[row].weapon != WeaponCatalog::None;
			if (!unbuilt && (armed || (transports[row].definition.passengersFire && transports[row].occupied > 0)))
				classes |= target_class::ArmedContainer;
			if (transports[row].occupied == 0)
				classes |= target_class::Emptied;
			else if (!garrisons.empty() && garrisons[row].immuneToClear == 0)
				classes |= target_class::Clearable;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ContainerClassesSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.container_classes";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
