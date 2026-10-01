export module engine.gameplay.common.weapons.resources.weapon_catalog;
import std;

export import engine.gameplay.common.weapons.definitions.weapon;
export import engine.gameplay.common.weapons.definitions.weapon_bonus;
import engine.ecs.system.system;

// Every weapon in play, indexed by Armament::weapon.
export namespace engine::gameplay
{
class WeaponCatalog
{
public:
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	// The damage type that no armor resists (its weapons may do no damage and still be chosen); set by the game.
	std::uint32_t unresistable{None};
	// The plain death (DEATH_NORMAL: Object::kill's); set by the game.
	std::uint32_t normalDeath{0};
	// DAMAGE_KILL_GARRISONED (kills those inside a garrison instead of hurting it); set by the game.
	std::uint32_t killGarrisoned{None};
	// The damage types and death estimateWeaponDamage treats apart (SNIPER, SURRENDER, DISARM, DEPLOY, HACK, KILL_PILOT;
	// DEATH_BURNED); set by the game, None: the game has none.
	std::uint32_t sniper{None};
	std::uint32_t surrender{None};
	std::uint32_t disarm{None};
	std::uint32_t deploy{None};
	std::uint32_t hack{None};
	std::uint32_t killPilot{None};
	std::uint32_t burnedDeath{None};
	// DEATH_DETONATED (a missile that calls its die modules on going off); set by the game.
	std::uint32_t detonatedDeath{0};
	// The weapon bonus conditions of continuous fire (CONTINUOUS_FIRE_MEAN, CONTINUOUS_FIRE_FAST); set by the game.
	std::uint32_t continuousFireMean{0};
	std::uint32_t continuousFireFast{0};
	// FiringTracker's TARGET_FAERIE_FIRE: the weapon bonus conditions of shooting at a thing with the FAERIE_FIRE object
	// status (its bit; None: no such status); set by the game.
	std::uint32_t targetFaerieFire{0};
	std::uint32_t faerieFireStatus{None};

	// The global weapon bonus table (GameData's WeaponBonus lines) and the weapons' own tables.
	WeaponBonusSet globalBonus;
	std::vector<WeaponBonusSet> extraBonuses;
	// The weapons' ScatterTarget patterns, one after another (WeaponDefinition::scatterFirst/scatterCount), unscaled.
	std::vector<Engine::Math::FixedVector2> scatterTargets;

	// Weapon::computeBonus: the global table's and then the weapon's own rows for these conditions.
	WeaponBonus Bonus(const WeaponDefinition &weapon, std::uint32_t conditions) const noexcept
	{
		WeaponBonus bonus;
		if (conditions == 0)
			return bonus;
		AppendBonuses(globalBonus, conditions, bonus);
		if (weapon.extraBonus < extraBonuses.size())
			AppendBonuses(extraBonuses[weapon.extraBonus], conditions, bonus);
		return bonus;
	}

	std::uint32_t Add(WeaponDefinition weapon)
	{
		m_weapons.push_back(weapon);
		return static_cast<std::uint32_t>(m_weapons.size() - 1);
	}

	const WeaponDefinition &At(std::uint32_t index) const { return m_weapons.at(index); }
	std::size_t Size() const noexcept { return m_weapons.size(); }

private:
	std::vector<WeaponDefinition> m_weapons;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::WeaponCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.weapon_catalog";
};
}
