export module games.renegade.content.armor.defense_preset;
import std;
export import games.renegade.content.armor.armor_catalog;
export import games.renegade.content.levels.definition_catalog;
export import games.renegade.gameplay.defense.components.defense;

export namespace renegade::content
{
struct DefensePreset
{
	engine::gameplay::Health health;
	engine::gameplay::Shield shield;
	Defense defense;
	Fixed damage_points{}, death_points{};
};

// DamageableGameObjDef's own defense wrapper (207011207) contains the
// DefenseObjectDef variables (7311607). Definition subclass IDs overlap:
// interpret this bounded schema rather than unrelated leaf microchunks.
inline std::expected<DefensePreset, std::string> ReadDefensePreset(
	const LegacyDefinition &definition, const ArmorCatalog &armor)
{
	using namespace persist;
	if (armor.armors.empty() || armor.armors.size() != armor.armorSaveIds.size())
		return std::unexpected("defense preset requires an initialized armor catalog");
	const auto wrappers = Descendants(definition.data, 207011207);
	if (!wrappers || wrappers->size() != 1) return std::unexpected("missing or duplicate damageable defense definition");
	const auto vars = One(wrappers->front().payload, 7311607);
	if (!vars) return std::unexpected(vars.error());
	const auto fields = Micros(vars->payload);
	if (!fields) return std::unexpected(fields.error());
	// DefenseObjectDefClass constructor defaults, with Load's -2 save-ID
	// sentinels. Missing known fields retain those defaults; unknown fields
	// remain in the immutable raw definition for later schema slices.
	DefensePreset result;
	result.health.current = result.health.maximum = Fixed::FromInt(100);
	std::int32_t skin_id = -2, shield_id = -2;
	std::array<bool, 8> seen{};
	for (const auto &field : *fields) {
		if (field.id >= seen.size()) continue;
		if (std::exchange(seen[field.id], true)) return std::unexpected("duplicate defense preset field");
		if (field.id == 2 || field.id == 5) {
			const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
			(field.id == 2 ? skin_id : shield_id) = std::bit_cast<std::int32_t>(*value);
		} else {
			const auto value = Scalar(field.payload); if (!value) return std::unexpected(value.error());
			switch (field.id) {
			case 0: result.health.current = *value; break;
			case 1: result.health.maximum = *value; break;
			case 3: result.shield.current = *value; break;
			case 4: result.shield.maximum = *value; break;
			case 6: result.damage_points = *value; break;
			case 7: result.death_points = *value; break;
			}
		}
	}
	result.defense.skin = result.health.armor = armor.ArmorBySaveId(skin_id);
	result.shield.armor = armor.ArmorBySaveId(shield_id);
	return result;
}

// Commando/god.cpp cGod::Create_Commando's IS_SOLOPLAY branch. This is
// game policy: the shared health/shield components impose no difficulty rule.
inline void ApplySoloDifficulty(DefensePreset &preset, int difficulty) noexcept
{
	const auto maximum = Fixed::FromInt(difficulty == 0 ? 200 : difficulty == 2 ? 75 : 100);
	preset.health.current = preset.health.maximum = maximum;
	preset.shield.current = preset.shield.maximum = maximum;
}
}
