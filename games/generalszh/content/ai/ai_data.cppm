export module games.generalszh.content.ai.ai_data;
import std;

export import engine.config.binding.schema;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// AIData.ini (the original's TheAI->getAiData(): AI.cpp's AIData fields, AISideInfo, AISideBuildList), what the
// computer players read: how often they try to build a structure or a team and how their wealth changes that, the
// recruiting and base-defence distances, whether skirmish bases turn to face the map's middle, how long before a lost
// building is rebuilt; per side, how many resource gatherers they keep at each level of difficulty, their base
// defence, and the sciences of each skill set; and per side the skirmish base plan (each structure: where, how often
// it is rebuilt, which way it faces, whether it stands from the start and whether it is built without being told).
export namespace generalszh::content
{
struct AiSideInfo
{
	std::string side;
	std::array<std::int32_t, 3> resourceGatherers{0, 0, 0}; // easy, normal, hard
	std::string baseDefenseStructure;
	std::vector<std::vector<std::string>> skillSets; // SkillSet1..5
};

struct AiBuildListEntry
{
	std::string structure;
	Engine::Math::FixedVector2 location;
	std::int32_t rebuilds{0};
	Engine::Math::Fixed angleDegrees;
	bool initiallyBuilt{false};
	bool automaticallyBuild{true};
	std::string name;                            // Name: what the built structure is called (its script name)
	Engine::Math::FixedVector2 rallyPointOffset; // RallyPointOffset: off its factory's natural rally point
};

struct AiBuildList
{
	std::string side;
	std::vector<AiBuildListEntry> structures;
};

struct AiData
{
	Engine::Math::Fixed structureSeconds;
	Engine::Math::Fixed teamSeconds{Engine::Math::Fixed::FromInt(10)};
	std::int64_t wealthy{7000};
	std::int64_t poor{2000};
	Engine::Math::Fixed structuresWealthyRate{Engine::Math::Fixed::FromInt(2)};
	Engine::Math::Fixed structuresPoorRate{Engine::Math::Fixed::FromRatio(6, 10)};
	Engine::Math::Fixed teamsWealthyRate{Engine::Math::Fixed::FromInt(2)};
	Engine::Math::Fixed teamsPoorRate{Engine::Math::Fixed::FromRatio(6, 10)};
	Engine::Math::Fixed teamResourcesToStart{Engine::Math::Fixed::FromRatio(1, 10)};
	Engine::Math::Fixed maxRecruitRadius{Engine::Math::Fixed::FromInt(500)};
	Engine::Math::Fixed skirmishBaseDefenseExtraDistance{Engine::Math::Fixed::FromInt(150)};
	// SkirmishGroupFudgeDistance (TAiData m_skirmishGroupFudgeValue, default 0): a skirmish AI's team following a path as a
	// team is there once its centre is within this times its member count of a member's goal (AIFollowWaypointPathState).
	Engine::Math::Fixed skirmishGroupFudgeDistance;
	Engine::Math::Fixed supplyCenterSafeRadius{Engine::Math::Fixed::FromInt(300)};
	Engine::Math::Fixed attackPriorityDistanceModifier; // AttackPriorityDistanceModifier (0 as the original's default)
	Engine::Math::Fixed rebuildDelaySeconds{Engine::Math::Fixed::FromInt(30)};
	// The idle look's range factors (AI::getAdjustedVisionRangeForObject): GuardOuterModifierAI / Human, AlertRangeModifier,
	// AggressiveRangeModifier (TAiData's defaults otherwise).
	Engine::Math::Fixed guardOuterModifierAi{Engine::Math::Fixed::One()};
	Engine::Math::Fixed guardOuterModifierHuman{Engine::Math::Fixed::One()};
	Engine::Math::Fixed alertRangeModifier{Engine::Math::Fixed::One()};
	Engine::Math::Fixed aggressiveRangeModifier{Engine::Math::Fixed::One()};
	Engine::Math::Fixed guardInnerModifierAi{Engine::Math::Fixed::One()};
	Engine::Math::Fixed guardInnerModifierHuman{Engine::Math::Fixed::One()};
	// GuardEnemyScanRate / GuardChaseUnitsDuration (ms; TAiData's defaults half a second and none).
	Engine::Math::Fixed guardEnemyScanRateMs{Engine::Math::Fixed::FromInt(500)};
	Engine::Math::Fixed guardChaseUnitsMs;
	// GuardEnemyReturnScanRate (ms; TAiData's default a second): how often a guard on its way back looks for enemies.
	Engine::Math::Fixed guardEnemyReturnScanRateMs{Engine::Math::Fixed::FromInt(1000)};
	// ForceIdleMSEC (ms; TAiData's default a single frame): how long a unit that went idle waits before its first look.
	std::optional<Engine::Math::Fixed> forceIdleMs;
	bool rotateSkirmishBases{false};
	// ForceSkirmishAI (Player::setPlayerType: a map's computer player gets the skirmish AI too), AIDozerBoredRadiusModifier
	// (DozerAIUpdate::getBoredRange: a computer player's dozer looks this many times as far), AttackIgnoreInsignificantBuildings
	// (getNextMoodTarget: AI::IGNORE_INSIGNIFICANT_BUILDINGS); TAiData's defaults No, 2 and No.
	bool forceSkirmishAi{false};
	Engine::Math::Fixed aiDozerBoredRadiusModifier{Engine::Math::Fixed::FromInt(2)};
	bool attackIgnoreInsignificantBuildings{false};
	// AICrushesInfantry (TAiData's default Yes): a computer player's vehicles set out to crush infantry (wantToSquishTarget,
	// canPursue).
	bool aiCrushesInfantry{true};
	// EnableRepulsors (KINDOF_CAN_BE_REPULSED run from enemies and repulsors), RepulsedDistance (how much further than
	// their vision they run).
	bool enableRepulsors{false};
	Engine::Math::Fixed repulsedDistance;
	// AttackUsesLineOfSight (TAiData's default Yes): whether obstacles block the view of KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT
	// lookers (Pathfinder::isAttackViewBlockedByObstacle).
	bool attackUsesLineOfSight{true};
	// Group ground movement (AIGroup::friend_computeGroundPath / friend_moveInfantryToPos / friend_moveVehicleToPos):
	// MinInfantryForGroup, MinVehiclesForGroup (TAiData's defaults 3 and 4), MinDistanceForGroup (100) and
	// DistanceRequiresGroup (0).
	std::int32_t minInfantryForGroup{3};
	std::int32_t minVehiclesForGroup{4};
	Engine::Math::Fixed minDistanceForGroup{Engine::Math::Fixed::FromInt(100)};
	// WallHeight (TAiData m_wallHeight, default 0): the height of the wall made of WALK_ON_TOP_OF_WALL pieces (Pathfinder's
	// m_wallHeight: the wall layer's height everywhere).
	Engine::Math::Fixed wallHeight;
	Engine::Math::Fixed distanceRequiresGroup;
	// MaxRetaliationDistance / RetaliationFriendsRadius (TAiData's defaults 210 and 120): how far an aggressor may be for
	// a human player's things to strike back, and how far round the victim they are called from.
	Engine::Math::Fixed maxRetaliateDistance{Engine::Math::Fixed::FromInt(210)};
	Engine::Math::Fixed retaliateFriendsRadius{Engine::Math::Fixed::FromInt(120)};
	std::vector<AiSideInfo> sides;
	std::vector<AiBuildList> buildLists;

	const AiSideInfo *Side(std::string_view side) const noexcept
	{
		for (const AiSideInfo &info : sides)
			if (info.side == side)
				return &info;
		return nullptr;
	}
	// AISkirmishPlayer::newMap: the first build list of the side.
	const AiBuildList *BuildList(std::string_view side) const noexcept
	{
		for (const AiBuildList &list : buildLists)
			if (list.side == side)
				return &list;
		return nullptr;
	}
};

namespace ai_data_detail
{
// "X:501.22 Y:546.25"
inline Engine::Math::FixedVector2 Location(const engine::config::Node &field)
{
	Engine::Math::FixedVector2 at;
	for (const std::string_view value : field.values)
	{
		if (value.starts_with("X:"))
			at.x = engine::config::values::ParseFixed(value.substr(2)).value_or(at.x);
		else if (value.starts_with("Y:"))
			at.y = engine::config::values::ParseFixed(value.substr(2)).value_or(at.y);
	}
	return at;
}
}

inline AiData BindAiData(const engine::config::Document &document, engine::config::BindContext &context)
{
	AiData data;
	const auto fixed = [&](const engine::config::Node &field, Engine::Math::Fixed &out) { out = engine::config::ReadFixed(field, context).value_or(out); };
	const auto integer = [&](const engine::config::Node &field, auto &out) {
		out = static_cast<std::remove_reference_t<decltype(out)>>(engine::config::ReadInt(field, context).value_or(out));
	};
	const auto yes = [](const engine::config::Node &field, bool fallback) {
		return field.values.empty() ? fallback : engine::config::values::ParseBool(field.Value()).value_or(fallback);
	};
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "AIData")
			continue;
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "WallHeight")
				fixed(field, data.wallHeight);
			else if (key == "StructureSeconds")
				fixed(field, data.structureSeconds);
			else if (key == "TeamSeconds")
				fixed(field, data.teamSeconds);
			else if (key == "Wealthy")
				integer(field, data.wealthy);
			else if (key == "Poor")
				integer(field, data.poor);
			else if (key == "StructuresWealthyRate")
				fixed(field, data.structuresWealthyRate);
			else if (key == "StructuresPoorRate")
				fixed(field, data.structuresPoorRate);
			else if (key == "TeamsWealthyRate")
				fixed(field, data.teamsWealthyRate);
			else if (key == "TeamsPoorRate")
				fixed(field, data.teamsPoorRate);
			else if (key == "TeamResourcesToStart")
				fixed(field, data.teamResourcesToStart);
			else if (key == "MaxRecruitRadius")
				fixed(field, data.maxRecruitRadius);
			else if (key == "SkirmishBaseDefenseExtraDistance")
				fixed(field, data.skirmishBaseDefenseExtraDistance);
			else if (key == "SkirmishGroupFudgeDistance")
				fixed(field, data.skirmishGroupFudgeDistance);
			else if (key == "SupplyCenterSafeRadius")
				fixed(field, data.supplyCenterSafeRadius);
			else if (key == "AttackPriorityDistanceModifier")
				fixed(field, data.attackPriorityDistanceModifier);
			else if (key == "RebuildDelayTimeSeconds")
				fixed(field, data.rebuildDelaySeconds);
			else if (key == "GuardOuterModifierAI")
				fixed(field, data.guardOuterModifierAi);
			else if (key == "GuardOuterModifierHuman")
				fixed(field, data.guardOuterModifierHuman);
			else if (key == "AlertRangeModifier")
				fixed(field, data.alertRangeModifier);
			else if (key == "AggressiveRangeModifier")
				fixed(field, data.aggressiveRangeModifier);
			else if (key == "GuardInnerModifierAI")
				fixed(field, data.guardInnerModifierAi);
			else if (key == "GuardInnerModifierHuman")
				fixed(field, data.guardInnerModifierHuman);
			else if (key == "ForceIdleMSEC")
			{
				Engine::Math::Fixed ms;
				fixed(field, ms);
				data.forceIdleMs = ms;
			}
			else if (key == "GuardEnemyScanRate")
				fixed(field, data.guardEnemyScanRateMs);
			else if (key == "GuardChaseUnitsDuration")
				fixed(field, data.guardChaseUnitsMs);
			else if (key == "GuardEnemyReturnScanRate")
				fixed(field, data.guardEnemyReturnScanRateMs);
			else if (key == "EnableRepulsors")
				data.enableRepulsors = yes(field, data.enableRepulsors);
			else if (key == "MinInfantryForGroup" || key == "MinVehiclesForGroup")
			{
				// INI::parseInt.
				Engine::Math::Fixed count = Engine::Math::Fixed::FromInt(key == "MinInfantryForGroup" ? data.minInfantryForGroup : data.minVehiclesForGroup);
				fixed(field, count);
				(key == "MinInfantryForGroup" ? data.minInfantryForGroup : data.minVehiclesForGroup) = static_cast<std::int32_t>(count.Floor());
			}
			else if (key == "MinDistanceForGroup")
				fixed(field, data.minDistanceForGroup);
			else if (key == "DistanceRequiresGroup")
				fixed(field, data.distanceRequiresGroup);
			else if (key == "AttackUsesLineOfSight")
				data.attackUsesLineOfSight = yes(field, data.attackUsesLineOfSight);
			else if (key == "RepulsedDistance")
				fixed(field, data.repulsedDistance);
			else if (key == "MaxRetaliationDistance")
				fixed(field, data.maxRetaliateDistance);
			else if (key == "RetaliationFriendsRadius")
				fixed(field, data.retaliateFriendsRadius);
			else if (key == "AICrushesInfantry")
				data.aiCrushesInfantry = yes(field, data.aiCrushesInfantry);
			else if (key == "ForceSkirmishAI")
				data.forceSkirmishAi = yes(field, data.forceSkirmishAi);
			else if (key == "AIDozerBoredRadiusModifier")
				fixed(field, data.aiDozerBoredRadiusModifier);
			else if (key == "AttackIgnoreInsignificantBuildings")
				data.attackIgnoreInsignificantBuildings = yes(field, data.attackIgnoreInsignificantBuildings);
			else if (key == "RotateSkirmishBases")
				data.rotateSkirmishBases = yes(field, data.rotateSkirmishBases);
			else if (key == "SideInfo" && !field.values.empty())
			{
				// AI::parseSideInfo: onto the side's info if it has one (a later AIData block, a map.ini override), each
				// SkillSetN replacing set N alone.
				AiSideInfo info;
				info.side = std::string(field.Value());
				if (const AiSideInfo *existing = data.Side(info.side))
					info = *existing;
				for (const engine::config::Node &entry : field.children)
				{
					if (entry.key == "ResourceGatherersEasy")
						integer(entry, info.resourceGatherers[0]);
					else if (entry.key == "ResourceGatherersNormal")
						integer(entry, info.resourceGatherers[1]);
					else if (entry.key == "ResourceGatherersHard")
						integer(entry, info.resourceGatherers[2]);
					else if (entry.key == "BaseDefenseStructure1" && !entry.values.empty())
						info.baseDefenseStructure = std::string(entry.Value());
					else if (entry.key.starts_with("SkillSet"))
					{
						const std::size_t slot = entry.key.size() == 9 && entry.key[8] >= '1' && entry.key[8] <= '5'
							? static_cast<std::size_t>(entry.key[8] - '1') : info.skillSets.size();
						if (info.skillSets.size() <= slot)
							info.skillSets.resize(slot + 1);
						auto &set = info.skillSets[slot];
						set.clear();
						for (const engine::config::Node &science : entry.children)
							if (science.key == "Science" && !science.values.empty())
								set.push_back(std::string(science.Value()));
					}
				}
				if (AiSideInfo *existing = const_cast<AiSideInfo *>(data.Side(info.side)))
					*existing = std::move(info);
				else
					data.sides.push_back(std::move(info));
			}
			else if (key == "SkirmishBuildList" && !field.values.empty())
			{
				AiBuildList list;
				list.side = std::string(field.Value());
				for (const engine::config::Node &structure : field.children)
				{
					if (structure.key != "Structure" || structure.values.empty())
						continue;
					AiBuildListEntry entry;
					entry.structure = std::string(structure.Value());
					for (const engine::config::Node &value : structure.children)
					{
						if (value.key == "Location")
							entry.location = ai_data_detail::Location(value);
						else if (value.key == "Name" && !value.values.empty())
							entry.name = std::string(value.Value());
						else if (value.key == "RallyPointOffset")
							entry.rallyPointOffset = ai_data_detail::Location(value);
						else if (value.key == "Rebuilds")
							integer(value, entry.rebuilds);
						else if (value.key == "Angle")
							fixed(value, entry.angleDegrees);
						else if (value.key == "InitiallyBuilt")
							entry.initiallyBuilt = yes(value, entry.initiallyBuilt);
						else if (value.key == "AutomaticallyBuild")
							entry.automaticallyBuild = yes(value, entry.automaticallyBuild);
					}
					list.structures.push_back(std::move(entry));
				}
				// TAiData::addFactionBuildList: a side's later list replaces its earlier one.
				if (AiBuildList *existing = const_cast<AiBuildList *>(data.BuildList(list.side)))
					*existing = std::move(list);
				else
					data.buildLists.push_back(std::move(list));
			}
		}
	}
	return data;
}
}
