export module games.generalszh.content.containment.garrison_points;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.models.model_rigs;
import games.generalszh.content.objects.model_states;
import games.generalszh.content.objects.model_conditions;

// Where a garrison's occupants stand to shoot (GarrisonContain::loadGarrisonPoints): the FIREPOINT01, FIREPOINT02 ...
// bones (at most MAX_GARRISON_POINTS: 40), in the model's own frame, of its GARRISONED model while pristine, DAMAGED
// and REALLYDAMAGED (the original switches its conditions through those three). None: they shoot from its middle.
export namespace generalszh::content
{
inline constexpr std::size_t MaxGarrisonPoints = 40;

using GarrisonPointSets = std::array<std::vector<Engine::Math::FixedVector3>, 3>; // pristine, damaged, really damaged

inline GarrisonPointSets ReadGarrisonPoints(const ObjectDefinition &object, ModelRigs &rigs)
{
	GarrisonPointSets points;
	bool garrison = false;
	for (const ModuleEntry &module : object.modules)
		garrison = garrison || (module.type == "GarrisonContain" && module.block != nullptr);
	if (!garrison)
		return points;
	const ModelStates states = ReadModelStates(object);
	if (states.Empty())
		return points;
	const auto conditions = [](std::initializer_list<std::string_view> names) {
		ConditionBits bits{};
		for (const std::string_view name : names)
		{
			const std::uint32_t bit = ModelConditionBit(name);
			bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
		}
		return bits;
	};
	const std::array<ConditionBits, 3> sets{conditions({"GARRISONED"}), conditions({"GARRISONED", "DAMAGED"}),
		conditions({"GARRISONED", "REALLYDAMAGED"})};
	for (std::size_t set = 0; set < sets.size(); ++set)
	{
		const std::string &model = states.states[SelectModelState(states, sets[set])].model;
		if (model.empty())
			continue;
		for (std::size_t index = 1; index <= MaxGarrisonPoints; ++index)
		{
			char name[16];
			std::snprintf(name, sizeof(name), "FIREPOINT%02d", static_cast<int>(index));
			const auto bone = rigs.Bone(model, name);
			if (!bone)
				break;
			points[set].push_back(bone->position);
		}
	}
	return points;
}

// Where a garrison that does not enclose its occupants (IsEnclosingContainer No: a fire base) keeps them
// (GarrisonContain::loadStationGarrisonPoints: getMultiLogicalBonePosition("STATION", ContainMax) with the model switched
// to pristine GARRISONED): its STATION01, STATION02 ... bones, at most ContainMax (and MAX_GARRISON_POINTS), in its own
// frame. An enclosing garrison, or none: no stations.
inline std::vector<Engine::Math::FixedVector3> ReadGarrisonStations(const ObjectDefinition &object, ModelRigs &rigs)
{
	std::vector<Engine::Math::FixedVector3> stations;
	const ModuleEntry *garrison = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.type == "GarrisonContain" && module.block != nullptr)
			garrison = &module;
	if (garrison == nullptr)
		return stations;
	const auto *encloses = garrison->block->Find("IsEnclosingContainer");
	if (encloses == nullptr || encloses->Value().empty() || encloses->Value()[0] == 'Y' || encloses->Value()[0] == 'y')
		return stations;
	std::size_t most = 0;
	if (const auto *room = garrison->block->Find("ContainMax"))
		most = static_cast<std::size_t>(std::clamp<long long>(std::atoll(std::string(room->Value()).c_str()), 0, static_cast<long long>(MaxGarrisonPoints)));
	const ModelStates states = ReadModelStates(object);
	if (states.Empty())
		return stations;
	ConditionBits garrisoned{};
	const std::uint32_t bit = ModelConditionBit("GARRISONED");
	garrisoned[bit / 64] |= std::uint64_t{1} << (bit % 64);
	const std::string &model = states.states[SelectModelState(states, garrisoned)].model;
	if (model.empty())
		return stations;
	for (std::size_t index = 1; index <= most; ++index)
	{
		char name[16];
		std::snprintf(name, sizeof(name), "STATION%02d", static_cast<int>(index));
		const auto bone = rigs.Bone(model, name);
		if (!bone)
			break;
		stations.push_back(bone->position);
	}
	return stations;
}

// Where a transport's riders stand (OpenContain::putObjAtNextFirePoint): the FIREPOINT01 .. bones (MAX_FIRE_POINTS: 32)
// of its model at rest, in its own frame; with PassengersInTurret, turning with its turret about its turret bone.
struct TransportFirePointSet
{
	std::vector<Engine::Math::FixedVector3> points;
	Engine::Math::FixedVector3 turretPivot;
	bool inTurret{false};
};

inline std::optional<TransportFirePointSet> ReadTransportFirePoints(const ObjectDefinition &object, ModelRigs &rigs)
{
	// OpenContain's own redeploy: not a garrison's or a helix's (theirs are their own), nor a building that is not one.
	const ModuleEntry *contain = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && std::string_view(module.type).ends_with("Contain"))
			contain = &module;
	if (contain == nullptr || contain->type == "GarrisonContain" || contain->type == "HelixContain")
		return std::nullopt;
	const ModelStates states = ReadModelStates(object);
	if (states.Empty())
		return std::nullopt;
	const ModelState &rest = states.states[SelectModelState(states, ConditionBits{})];
	if (rest.model.empty())
		return std::nullopt;
	TransportFirePointSet set;
	for (std::size_t index = 1; index <= 32; ++index)
	{
		char name[16];
		std::snprintf(name, sizeof(name), "FIREPOINT%02d", static_cast<int>(index));
		const auto bone = rigs.Bone(rest.model, name);
		if (!bone)
			break;
		set.points.push_back(bone->position);
	}
	if (set.points.empty())
		return std::nullopt;
	if (const auto *turret = contain->block->Find("PassengersInTurret"); turret != nullptr && !turret->Value().empty() &&
		(turret->Value()[0] == 'Y' || turret->Value()[0] == 'y'))
		if (const auto pivot = rest.turretBone.empty() ? std::nullopt : rigs.Bone(rest.model, rest.turretBone))
		{
			set.inTurret = true;
			set.turretPivot = pivot->position;
		}
	return set;
}
}
