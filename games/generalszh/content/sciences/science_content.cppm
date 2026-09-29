export module games.generalszh.content.sciences.science_content;
import std;

export import engine.config.binding.schema;

// Science.ini (ScienceStore: each science's prerequisites, purchase cost in general's points, whether it may be
// granted, its names) and Rank.ini (RankInfoStore: each rank's skill points needed, the sciences it grants and the
// purchase points it gives), as the original reads them: a later entry of a science changes the fields it names.
export namespace generalszh::content
{
struct ScienceInfo
{
	std::string name;
	std::vector<std::string> prerequisites; // PrerequisiteSciences
	std::int32_t purchaseCost{0};           // SciencePurchasePointCost (0: not purchasable)
	bool grantable{true};                   // IsGrantable
	std::string displayName;                // DisplayName (a string label)
	std::string description;                // Description
};

struct RankInfo
{
	std::string name;                         // RankName (a string label)
	std::int32_t skillPointsNeeded{0};        // SkillPointsNeeded
	std::vector<std::string> sciencesGranted; // SciencesGranted
	std::int32_t purchasePointsGranted{0};    // SciencePurchasePointsGranted
};

namespace science_detail
{
inline std::vector<std::string> Names(const engine::config::Node &field)
{
	std::vector<std::string> out;
	for (const std::string_view value : field.values)
		if (value != "None")
			out.emplace_back(value);
	return out;
}
}

// Each Science block in order of first appearance (its bit is its place); later blocks change what they name.
inline std::vector<ScienceInfo> BindSciences(const engine::config::Document &document)
{
	std::vector<ScienceInfo> out;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Science" || root.values.empty())
			continue;
		const std::string name(root.Value());
		auto found = std::find_if(out.begin(), out.end(), [&](const ScienceInfo &info) { return info.name == name; });
		if (found == out.end())
		{
			out.push_back({name});
			found = out.end() - 1;
		}
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "PrerequisiteSciences")
				found->prerequisites = science_detail::Names(field);
			else if (field.key == "SciencePurchasePointCost" && !field.values.empty())
				found->purchaseCost = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(0));
			else if (field.key == "IsGrantable" && !field.values.empty())
				found->grantable = engine::config::values::ParseBool(field.Value()).value_or(true);
			else if (field.key == "DisplayName" && !field.values.empty())
				found->displayName = std::string(field.Value());
			else if (field.key == "Description" && !field.values.empty())
				found->description = std::string(field.Value());
		}
	}
	return out;
}

// Rank 1..n (RankInfoStore: they must come in order).
inline std::vector<RankInfo> BindRanks(const engine::config::Document &document)
{
	std::vector<RankInfo> out;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Rank" || root.values.empty())
			continue;
		const auto level = engine::config::values::ParseInt(root.Value()).value_or(0);
		if (level != static_cast<std::int64_t>(out.size()) + 1)
			continue;
		RankInfo info;
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "RankName" && !field.values.empty())
				info.name = std::string(field.Value());
			else if (field.key == "SkillPointsNeeded" && !field.values.empty())
				info.skillPointsNeeded = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(0));
			else if (field.key == "SciencesGranted")
				info.sciencesGranted = science_detail::Names(field);
			else if (field.key == "SciencePurchasePointsGranted" && !field.values.empty())
				info.purchasePointsGranted = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(0));
		}
		out.push_back(std::move(info));
	}
	return out;
}
}
