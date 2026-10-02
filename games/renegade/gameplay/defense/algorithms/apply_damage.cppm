export module games.renegade.gameplay.defense.algorithms.apply_damage;
import std;
export import games.renegade.gameplay.defense.components.defense;
export import games.renegade.gameplay.defense.resources.damage_rules;

export namespace renegade
{
// Combat/damage.cpp DefenseObjectClass::Do_Damage, numeric reservoir slice.
// Scoring, callbacks, client trust, punishment and special damage are separate
// ledger items. Q48.16 replaces the original float arithmetic.
inline DefenseHit ApplyDamage(engine::gameplay::Health &health, engine::gameplay::Shield &shield,
	const Defense &defense, const DamageRequest &hit, const DamageRules &rules)
{
	Fixed damage = hit.amount * hit.difficultyScale;
	const auto skin = rules.At(hit.alternateSkin.value_or(defense.skin), hit.warhead);
	const auto shieldRule = hit.alternateSkin ? ArmorResponse{Fixed{}, Fixed{}} : rules.At(shield.armor, hit.warhead);
	const auto beforeHealth = health.current, beforeShield = shield.current;
	const bool repair = damage * skin.multiplier < Fixed{} || damage * shieldRule.multiplier < Fixed{};
	if (repair)
	{
		// One reservoir per hit. Excess health repair does NOT spill to armor.
		if (health.current < health.maximum && skin.multiplier != Fixed{})
			health.current -= std::clamp(damage * skin.multiplier, health.current - health.maximum, Fixed{});
		else
			shield.current -= std::clamp(damage * shieldRule.multiplier, shield.current - shield.maximum, Fixed{});
	}
	else
	{
		if (hit.teammate && hit.source != hit.target && !hit.friendlyFire)
			return {hit.target, hit.source, {}, {}, false, false};
		if (shield.current > Fixed{} && !hit.alternateSkin)
		{
			const Fixed diverted = damage * shieldRule.absorption;
			damage -= diverted;
			const Fixed attempted = diverted * shieldRule.multiplier;
			const Fixed applied = std::min(attempted, shield.current);
			shield.current = std::clamp(shield.current - applied, Fixed{}, shield.maximum);
			if (shieldRule.multiplier != Fixed{})
				damage += attempted / shieldRule.multiplier - applied / shieldRule.multiplier;
		}
		damage = std::min(damage * skin.multiplier, health.current);
		health.current -= damage;
		if (!defense.canDie) health.current = std::max(health.current, Fixed::One());
	}
	health.current = std::clamp(health.current, Fixed{}, health.maximum);
	return {hit.target, hit.source, beforeHealth - health.current, beforeShield - shield.current,
		beforeHealth > Fixed{} && health.current == Fixed{}, repair};
}
}
