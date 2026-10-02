export module engine.gameplay.common.weapons.systems.temp_weapon_bonus_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.weapons.components.temp_weapon_bonus;

// TempWeaponBonusHelper::update, chunk-parallel: a temporary weapon bonus whose tick has come is cleared
// (clearTempWeaponBonus).
export namespace engine::gameplay
{
struct TempWeaponBonusSystem
{
	using Query = ecs::Query<ecs::Write<TempWeaponBonus>, ecs::Write<WeaponBonusConditions>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t now = context.Tick();
		auto temps = chunk.Get<TempWeaponBonus>();
		auto conditions = chunk.Get<WeaponBonusConditions>();
		for (std::size_t row = 0; row < temps.size(); ++row)
		{
			TempWeaponBonus &temp = temps[row];
			if (temp.bit == 0 || now < temp.removeTick)
				continue;
			SetWeaponBonus(conditions[row], temp.bit, false, now);
			temp = {};
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::TempWeaponBonusSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.temp_weapon_bonus";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
