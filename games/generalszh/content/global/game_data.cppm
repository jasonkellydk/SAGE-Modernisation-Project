export module games.generalszh.content.global.game_data;
import std;

export import engine.config.binding.schema;
export import Engine.Core.Math.Fixed;
export import games.generalszh.content.combat.weapon_bonus_content;

// GameData.ini's globals the port uses (the original's GlobalData), in the
// port's units: gravity per tick squared; health ratios below which a unit
// looks damaged and really damaged; the starting cash; how short power slows
// production. Unread fields keep the shipped values.
export namespace generalszh::content
{
struct GameData
{
	Engine::Math::Fixed gravity{Engine::Math::Fixed::FromRatio(-64, 900)};
	// The sky box (W3DWater's "new_skybox" while DrawSkyBox is on): the scale its model is made at (SkyBoxScale) and the
	// height its centre sits at under the camera (SkyBoxPositionZ).
	Engine::Math::Fixed skyBoxScale{Engine::Math::Fixed::FromRatio(9, 2)};
	Engine::Math::Fixed skyBoxPositionZ{};
	// Building occlusion: how long after it is made (or walks out of a building) a unit may show through buildings
	// (DefaultOcclusionDelay, a thing's OcclusionDelay when it has none), and how dark its silhouette's colour is
	// (OccludedColorLuminanceScale).
	std::uint64_t defaultOcclusionDelayTicks{0};
	Engine::Math::Fixed occludedLuminanceScale{Engine::Math::Fixed::FromRatio(1, 2)};
	// GenerateMinefieldBehavior's defaults: StandardMinefieldDistance (feet) and StandardMinefieldDensity (mines per square foot).
	Engine::Math::Fixed standardMinefieldDistance{Engine::Math::Fixed::FromInt(40)};
	Engine::Math::Fixed standardMinefieldDensity{Engine::Math::Fixed::FromRatio(4, 1000)};
	Engine::Math::Fixed unitDamaged{Engine::Math::Fixed::FromRatio(7, 10)};
	Engine::Math::Fixed unitReallyDamaged{Engine::Math::Fixed::FromRatio(35, 100)};
	// MovementPenaltyDamageState: PRISTINE 0, DAMAGED 1, REALLYDAMAGED 2 (the default), RUBBLE 3.
	std::uint32_t movementPenaltyState{2};
	Engine::Math::Fixed structureStiffness{Engine::Math::Fixed::FromRatio(3, 10)}; // StructureStiffness
	Engine::Math::Fixed structureRubbleHeight{Engine::Math::Fixed::FromInt(10)};   // DefaultStructureRubbleHeight
	std::int64_t defaultStartingCash{10000};
	// The look a fired special power leaves (SpecialPowerViewObject).
	std::string specialPowerViewObject;
	std::int64_t valuePerSupplyBox{100}; // ValuePerSupplyBox
	// Human/AISoloPlayerHealthBonus_Easy/_Normal/_Hard: a single-player game's health bonus by player type (human,
	// computer) and difficulty.
	std::array<std::array<Engine::Math::Fixed, 3>, 2> soloHealthBonus{{{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(), Engine::Math::Fixed::One()},
		{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(), Engine::Math::Fixed::One()}}};
	Engine::Math::Fixed sellPercentage{Engine::Math::Fixed::FromRatio(1, 2)}; // SellPercentage: selling gives back this share of the cost
	std::uint32_t maxTunnelCapacity{10};
	// The road buffer's room (W3DRoadBuffer::allocateRoadBuffers): MaxRoadSegments segments, MaxRoadTypes road types.
	std::uint32_t maxRoadSegments{4000};
	std::uint32_t maxRoadTypes{100};
	// Terrain tracks: how many vehicles may leave them at once (MaxTerrainTracks), and whether any do (MakeTrackMarks).
	std::uint32_t maxTerrainTracks{0};
	bool makeTrackMarks{false};
	// Drawable::flashAsSelected without a colour: its player's colour (SelectionFlashHouseColor) else white, saturated by
	// SelectionFlashSaturationFactor (GlobalData's defaults: no, 0.5).
	bool selectionFlashHouseColor{false};
	Engine::Math::Fixed selectionFlashSaturationFactor{Engine::Math::Fixed::FromRatio(1, 2)};
	// Where structures may go (BuildAssistant::isLocationLegalToBuild): how uneven the ground under one may be, how near
	// the map's edge, and how far from supplies one that may not be built near them must keep.
	Engine::Math::Fixed allowedHeightVariationForBuilding{Engine::Math::Fixed::FromInt(10)};
	Engine::Math::Fixed minDistFromEdgeOfMapForBuild{Engine::Math::Fixed::FromInt(30)};
	Engine::Math::Fixed supplyBuildBorder{Engine::Math::Fixed::FromInt(20)}; // MaxTunnelCapacity: how many a player's tunnel network holds
	// Base regeneration (BaseRegenerateUpdate): a share of max health a second, once damage-free this many ticks.
	Engine::Math::Fixed baseRegenPerSecond;
	// A promotion's world animation (LevelGainAnimationName, LevelGainAnimationTime s, LevelGainAnimationZRise a second).
	std::string levelGainAnimation;
	// ShellMapName: the map the shell plays behind its menus (GlobalData's default: ShellMap1).
	std::string shellMapName{"Maps\\ShellMap1\\ShellMap1.map"};
	bool playIntro{true}; // PlayIntro: the EA logo movie before the shell
	Engine::Math::Fixed levelGainSeconds;
	Engine::Math::Fixed levelGainRise;
	std::uint64_t baseRegenDelayTicks{0};
	// HistoricDamageLimit: how long a weapon's hits count towards its historic bonus at most (parseDurationUnsignedInt).
	std::uint64_t historicDamageLimitTicks{0};
	// AmmoPipWorldOffset (added to the object's top before it is projected), AmmoPipScreenOffset (x, y: shares of its
	// bounding sphere radius) and AmmoPipScaleFactor (only with icons scaled by the zoom, which retail does not do).
	std::array<Engine::Math::Fixed, 3> ammoPipWorldOffset{};
	std::array<Engine::Math::Fixed, 2> ammoPipScreenOffset{};
	Engine::Math::Fixed ammoPipScaleFactor{Engine::Math::Fixed::One()};
	// ContainerPipWorldOffset and ContainerPipScreenOffset, as the ammo pips' (Drawable::drawContained).
	std::array<Engine::Math::Fixed, 3> containerPipWorldOffset{};
	std::array<Engine::Math::Fixed, 2> containerPipScreenOffset{};
	// The shroud's cells (PartitionCellSize) and how long a look lingers once its looker moves on (UnlookPersistDuration).
	Engine::Math::Fixed partitionCellSize; // (none given: 1, PartitionManager::init)
	// GroupMoveClickToGatherAreaFactor: a player's group order inside its members' area, grown by this, tightens the
	// group instead (AIGroup::groupMoveToPosition; none given: 1).
	Engine::Math::Fixed groupMoveClickToGatherFactor{Engine::Math::Fixed::One()};
	std::int64_t framesPerSecondLimit{0};  // FramesPerSecondLimit: the logic rate SET_FPS_LIMIT 0 goes back to
	std::uint64_t unlookPersistTicks{30};
	Engine::Math::Fixed lowEnergyPenaltyModifier{Engine::Math::Fixed::One()};
	Engine::Math::Fixed minLowEnergyProductionSpeed{Engine::Math::Fixed::FromRatio(1, 2)};
	Engine::Math::Fixed maxLowEnergyProductionSpeed{Engine::Math::Fixed::FromRatio(4, 5)};
	// Models take the NIGHT condition (headlights on, night models) when the time of day is night.
	bool forceModelsToFollowTimeOfDay{true};
	// MaxFieldParticleCount (GlobalData default 30): on-screen ground-aligned AREA_EFFECT particles beyond which such
	// particle systems make no more.
	std::int32_t maxFieldParticleCount{30};
	// How see-through a structure being placed is drawn (ObjectPlacementOpacity).
	Engine::Math::Fixed objectPlacementOpacity{Engine::Math::Fixed::FromRatio(45, 100)};
	// MoveHintName: the model (and its animation, "<name>.<name>") shown where a move is ordered.
	std::string moveHintName{"SCMoveHint"};
	// KeyboardCameraRotateSpeed: radians the view turns each client frame a keypad rotate key is held (default 0.1).
	Engine::Math::Fixed keyboardCameraRotateSpeed{Engine::Math::Fixed::FromRatio(1, 10)};
	// DownwindAngle (radians, default -0.785: north-east): which way the wind blows (the rally point flag faces it).
	Engine::Math::Fixed downwindAngle{Engine::Math::Fixed::FromRatio(-785, 1000)};
	// The control bar's power bar (PowerBarBase, PowerBarIntervals, PowerBarYellowRange): its length is the log to this
	// base of the power made, this many powers of the base filling it; yellow while consumption is within the range
	// under production.
	std::int32_t powerBarBase{7};
	Engine::Math::Fixed powerBarIntervals{Engine::Math::Fixed::FromInt(3)};
	std::int32_t powerBarYellowRange{5};
	// Camera shakes (presentation): intensity per shake type (subtle, normal,
	// strong, severe, cinematic extreme, cinematic insane).
	std::array<Engine::Math::Fixed, 6> shakeIntensity{Engine::Math::Fixed::FromRatio(1, 2), Engine::Math::Fixed::One(), Engine::Math::Fixed::FromRatio(5, 2),
		Engine::Math::Fixed::FromInt(5), Engine::Math::Fixed::FromRatio(15, 2), Engine::Math::Fixed::FromInt(10)};
	// How much brighter infantry are lit than the scene (InfantryLightMorningScale, ...AfternoonScale, ...EveningScale,
	// ...NightScale: GlobalData m_infantryLightScale by time of day, 1.5 each by default).
	std::array<Engine::Math::Fixed, 4> infantryLightScale{Engine::Math::Fixed::FromRatio(3, 2), Engine::Math::Fixed::FromRatio(3, 2),
		Engine::Math::Fixed::FromRatio(3, 2), Engine::Math::Fixed::FromRatio(3, 2)};
	// The global weapon bonus table (WeaponBonus lines: GlobalData::m_weaponBonusSet).
	engine::gameplay::WeaponBonusSet weaponBonus;
	// Max health per veterancy level (HealthBonus_Veteran / _Elite / _Heroic; regular is always 100%).
	std::array<Engine::Math::Fixed, 4> healthBonus{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(), Engine::Math::Fixed::One(),
		Engine::Math::Fixed::One()};
	// The shroud as drawn (W3DShroud): ShroudColor (R: G: B:) scaled by a cell's level, the levels a clear, fogged and
	// shrouded cell take (ClearAlpha, FogAlpha, ShroudAlpha: 0 is opaque, 255 clear; nothing darker than ShroudAlpha).
	std::array<std::uint8_t, 3> shroudColor{255, 255, 255};
	std::uint8_t clearAlpha{255};
	std::uint8_t fogAlpha{127};
	std::uint8_t shroudAlpha{0};
};

GameData BindGameData(const engine::config::Document &document, engine::config::BindContext &context)
{
	GameData data;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "GameData")
			continue;
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			const auto fixed = [&](Engine::Math::Fixed &out) { out = engine::config::ReadFixed(field, context).value_or(out); };
			const auto byte = [&](std::uint8_t &out) {
				if (const auto value = engine::config::ReadFixed(field, context))
					out = static_cast<std::uint8_t>(std::clamp<std::int64_t>(value->Floor(), 0, 255));
			};
			if (key == "ShroudColor")
			{
				// "R:255 G:255 B:255" (the tokens may come split at the colon).
				std::string joined;
				for (const std::string_view value : field.values)
					(joined += value) += ' ';
				for (std::size_t channel = 0; channel < 3; ++channel)
				{
					const std::string tag = std::string(1, "RGB"[channel]) + ":";
					if (const auto at = joined.find(tag); at != std::string::npos)
					{
						std::size_t from = at + 2;
						while (from < joined.size() && joined[from] == ' ')
							++from;
						int parsed = 0;
						std::from_chars(joined.data() + from, joined.data() + joined.size(), parsed);
						data.shroudColor[channel] = static_cast<std::uint8_t>(std::clamp(parsed, 0, 255));
					}
				}
				continue;
			}
			// INI::parseCoord3D / parseCoord2D: "X:0.0 Y:0.0 Z:10.0" (a missing one stays as it was).
			const auto labeled = [&](std::string_view label, Engine::Math::Fixed &out) {
				for (std::size_t index = 0; index < field.values.size(); ++index)
				{
					std::string_view token = field.values[index];
					if (token.size() < label.size() + 1 || token.substr(0, label.size()) != label || token[label.size()] != ':')
						continue;
					token.remove_prefix(label.size() + 1);
					if (token.empty() && index + 1 < field.values.size())
						token = field.values[++index];
					out = engine::config::values::ParseFixed(token).value_or(out);
				}
			};
			if (key == "AmmoPipWorldOffset")
			{
				labeled("X", data.ammoPipWorldOffset[0]);
				labeled("Y", data.ammoPipWorldOffset[1]);
				labeled("Z", data.ammoPipWorldOffset[2]);
				continue;
			}
			if (key == "ContainerPipWorldOffset")
			{
				labeled("X", data.containerPipWorldOffset[0]);
				labeled("Y", data.containerPipWorldOffset[1]);
				labeled("Z", data.containerPipWorldOffset[2]);
				continue;
			}
			if (key == "ContainerPipScreenOffset")
			{
				labeled("X", data.containerPipScreenOffset[0]);
				labeled("Y", data.containerPipScreenOffset[1]);
				continue;
			}
			if (key == "AmmoPipScreenOffset")
			{
				labeled("X", data.ammoPipScreenOffset[0]);
				labeled("Y", data.ammoPipScreenOffset[1]);
				continue;
			}
			if (key == "ClearAlpha")
				byte(data.clearAlpha);
			else if (key == "AmmoPipScaleFactor")
				fixed(data.ammoPipScaleFactor);
			else if (key == "FogAlpha")
				byte(data.fogAlpha);
			else if (key == "ShroudAlpha")
				byte(data.shroudAlpha);
			else if (key == "Gravity")
				data.gravity = engine::config::ReadPerSecondSquared(field, context).value_or(data.gravity);
			else if (key == "StandardMinefieldDistance")
				fixed(data.standardMinefieldDistance);
			else if (key == "StandardMinefieldDensity")
				fixed(data.standardMinefieldDensity);
			else if (key == "UnitDamagedThreshold")
				fixed(data.unitDamaged);
			else if (key == "UnitReallyDamagedThreshold")
				fixed(data.unitReallyDamaged);
			else if (key == "StructureStiffness")
				fixed(data.structureStiffness);
			else if (key == "ShellMapName")
				data.shellMapName = std::string(field.Value());
			else if (key == "PlayIntro")
				data.playIntro = engine::config::ReadBool(field, context).value_or(data.playIntro);
			else if (key == "DefaultStructureRubbleHeight")
				fixed(data.structureRubbleHeight);
			else if (key == "MovementPenaltyDamageState")
			{
				static constexpr std::array<std::string_view, 4> states{"PRISTINE", "DAMAGED", "REALLYDAMAGED", "RUBBLE"};
				for (std::uint32_t index = 0; index < states.size(); ++index)
					if (field.Value() == states[index])
						data.movementPenaltyState = index;
			}
			else if (key == "LevelGainAnimationName")
				data.levelGainAnimation = std::string(field.Value());
			else if (key == "LevelGainAnimationTime")
				fixed(data.levelGainSeconds);
			else if (key == "LevelGainAnimationZRise")
				fixed(data.levelGainRise);
			else if (key == "BaseRegenHealthPercentPerSecond")
				data.baseRegenPerSecond = engine::config::ReadPercent(field, context).value_or(data.baseRegenPerSecond);
			else if (key == "BaseRegenDelay")
				data.baseRegenDelayTicks = engine::config::ReadDurationTicks(field, context).value_or(data.baseRegenDelayTicks);
			else if (key == "HistoricDamageLimit")
				data.historicDamageLimitTicks = engine::config::ReadDurationTicks(field, context).value_or(data.historicDamageLimitTicks);
			else if (key == "PartitionCellSize")
				fixed(data.partitionCellSize);
			else if (key == "SkyBoxScale")
				fixed(data.skyBoxScale);
			else if (key == "SkyBoxPositionZ")
				fixed(data.skyBoxPositionZ);
			else if (key == "DefaultOcclusionDelay")
				data.defaultOcclusionDelayTicks = engine::config::ReadDurationTicks(field, context).value_or(data.defaultOcclusionDelayTicks);
			else if (key == "OccludedColorLuminanceScale")
				fixed(data.occludedLuminanceScale);
			else if (key == "GroupMoveClickToGatherAreaFactor")
				fixed(data.groupMoveClickToGatherFactor);
			else if (key == "FramesPerSecondLimit" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), data.framesPerSecondLimit);
			else if (key == "UnlookPersistDuration")
				data.unlookPersistTicks = engine::config::ReadDurationTicks(field, context).value_or(data.unlookPersistTicks);
			else if (key == "AllowedHeightVariationForBuilding")
				fixed(data.allowedHeightVariationForBuilding);
			else if (key == "MinDistFromEdgeOfMapForBuild")
				fixed(data.minDistFromEdgeOfMapForBuild);
			else if (key == "SupplyBuildBorder")
				fixed(data.supplyBuildBorder);
			else if (key == "MaxTerrainTracks")
				data.maxTerrainTracks = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::ReadInt(field, context).value_or(0), 0));
			else if (key == "MakeTrackMarks")
				data.makeTrackMarks = engine::config::values::ParseBool(field.Value()).value_or(data.makeTrackMarks);
			else if (key == "SelectionFlashHouseColor")
				data.selectionFlashHouseColor = engine::config::values::ParseBool(field.Value()).value_or(data.selectionFlashHouseColor);
			else if (key == "SelectionFlashSaturationFactor")
				fixed(data.selectionFlashSaturationFactor);
			else if (key == "SellPercentage")
				data.sellPercentage = engine::config::ReadPercent(field, context).value_or(data.sellPercentage);
			else if (key == "MaxRoadSegments")
				data.maxRoadSegments = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::ReadInt(field, context).value_or(data.maxRoadSegments), 0));
			else if (key == "MaxRoadTypes")
				data.maxRoadTypes = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::ReadInt(field, context).value_or(data.maxRoadTypes), 0));
			else if (key == "MaxTunnelCapacity")
				data.maxTunnelCapacity = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::ReadInt(field, context).value_or(data.maxTunnelCapacity), 0));
			else if (key.starts_with("HumanSoloPlayerHealthBonus_") || key.starts_with("AISoloPlayerHealthBonus_"))
			{
				const std::size_t type = key.starts_with("AI") ? 1 : 0;
				const std::string_view level = key.substr(key.find('_') + 1);
				const std::size_t difficulty = level == "Easy" ? 0 : level == "Normal" ? 1 : level == "Hard" ? 2 : 3;
				if (difficulty < 3)
					data.soloHealthBonus[type][difficulty] = engine::config::ReadPercent(field, context).value_or(data.soloHealthBonus[type][difficulty]);
			}
			else if (key == "ValuePerSupplyBox")
				data.valuePerSupplyBox = engine::config::ReadInt(field, context).value_or(data.valuePerSupplyBox);
			else if (key == "SpecialPowerViewObject")
				data.specialPowerViewObject = std::string(field.Value());
			else if (key == "DefaultStartingCash")
				data.defaultStartingCash = engine::config::ReadInt(field, context).value_or(data.defaultStartingCash);
			else if (key == "LowEnergyPenaltyModifier")
				fixed(data.lowEnergyPenaltyModifier);
			else if (key == "MinLowEnergyProductionSpeed")
				fixed(data.minLowEnergyProductionSpeed);
			else if (key == "MaxLowEnergyProductionSpeed")
				fixed(data.maxLowEnergyProductionSpeed);
			else if (key == "HealthBonus_Veteran" || key == "HealthBonus_Elite" || key == "HealthBonus_Heroic")
			{
				// INI::parsePercentToReal.
				std::string_view text = field.Value();
				if (!text.empty() && text.back() == '%')
					text.remove_suffix(1);
				if (const auto percent = engine::config::values::ParseFixed(text))
					data.healthBonus[key == "HealthBonus_Veteran" ? 1 : key == "HealthBonus_Elite" ? 2 : 3] = *percent / Engine::Math::Fixed::FromInt(100);
			}
			else if (key == "WeaponBonus")
				ReadWeaponBonus(field, data.weaponBonus, context);
			else if (key == "PowerBarBase")
				data.powerBarBase = static_cast<std::int32_t>(engine::config::ReadInt(field, context).value_or(data.powerBarBase));
			else if (key == "PowerBarIntervals")
				fixed(data.powerBarIntervals);
			else if (key == "PowerBarYellowRange")
				data.powerBarYellowRange = static_cast<std::int32_t>(engine::config::ReadInt(field, context).value_or(data.powerBarYellowRange));
			else if (key == "ObjectPlacementOpacity")
				fixed(data.objectPlacementOpacity);
			else if (key == "DownwindAngle")
				fixed(data.downwindAngle);
			else if (key == "KeyboardCameraRotateSpeed")
				fixed(data.keyboardCameraRotateSpeed);
			else if (key == "MoveHintName")
				data.moveHintName = std::string(field.Value());
			else if (key == "ForceModelsToFollowTimeOfDay")
				data.forceModelsToFollowTimeOfDay = engine::config::ReadBool(field, context).value_or(data.forceModelsToFollowTimeOfDay);
			else if (key == "MaxFieldParticleCount")
				data.maxFieldParticleCount = static_cast<std::int32_t>(engine::config::ReadInt(field, context).value_or(data.maxFieldParticleCount));
			else
			{
				constexpr std::array<std::string_view, 6> shakes{"ShakeSubtleIntensity", "ShakeNormalIntensity", "ShakeStrongIntensity",
					"ShakeSevereIntensity", "ShakeCineExtremeIntensity", "ShakeCineInsaneIntensity"};
				for (std::size_t type = 0; type < shakes.size(); ++type)
					if (key == shakes[type])
						fixed(data.shakeIntensity[type]);
				constexpr std::array<std::string_view, 4> infantry{"InfantryLightMorningScale", "InfantryLightAfternoonScale",
					"InfantryLightEveningScale", "InfantryLightNightScale"};
				for (std::size_t time = 0; time < infantry.size(); ++time)
					if (key == infantry[time])
						fixed(data.infantryLightScale[time]);
			}
		}
	}
	return data;
}
}
