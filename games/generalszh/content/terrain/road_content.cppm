export module games.generalszh.content.terrain.road_content;
import std;

export import engine.config.document.document;

// Roads.ini's Road blocks (TerrainRoadType as a road, TerrainRoadCollection::newRoad / findRoad): each road's name, its
// id (TerrainRoadCollection::m_idCounter: from 1, one for every new Road and every new Bridge block in the order they
// are read, so roads and bridges share the count), its Texture, RoadWidth and RoadWidthInTexture (INI::parseReal: read
// as floats, as the original). A Road block starts from the DefaultRoad block written before it, when there is one
// (newRoad: its texture, width and width in texture). A name read a second time is the original's INI error: the
// first definition stands.
export namespace generalszh::content
{
struct RoadContent
{
	std::string name;
	std::uint32_t id{0};
	std::string texture;        // Texture
	// RoadWidth and RoadWidthInTexture as written (INI::parseReal: the road builder reads them as floats, RoadReal;
	// content holds no floats). Empty: 0.
	std::string widthText;
	std::string widthInTextureText;
};

// In the order the blocks were read.
struct RoadCatalog
{
	std::vector<RoadContent> roads;

	// TerrainRoadCollection::findRoad: by name, exactly (AsciiString ==).
	const RoadContent *Find(std::string_view name) const noexcept
	{
		for (const RoadContent &road : roads)
			if (road.name == name)
				return &road;
		return nullptr;
	}
};

inline RoadCatalog BindRoads(const engine::config::Document &document)
{
	RoadCatalog catalog;
	std::vector<std::string> bridges;
	std::uint32_t nextId = 1; // m_idCounter: MUST start at 1
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.values.empty())
			continue;
		const std::string name(root.Value());
		if (root.key == "Bridge")
		{
			if (std::find(bridges.begin(), bridges.end(), name) == bridges.end())
			{
				bridges.push_back(name);
				++nextId;
			}
			continue;
		}
		if (root.key != "Road" || catalog.Find(name) != nullptr)
			continue;
		RoadContent road;
		road.name = name;
		road.id = nextId++;
		if (const RoadContent *defaults = catalog.Find("DefaultRoad"))
		{
			road.texture = defaults->texture;
			road.widthText = defaults->widthText;
			road.widthInTextureText = defaults->widthInTextureText;
		}
		for (const engine::config::Node &field : root.children)
		{
			if (field.values.empty())
				continue;
			if (field.key == "Texture")
				road.texture = std::string(field.Value());
			else if (field.key == "RoadWidth")
				road.widthText = std::string(field.Value());
			else if (field.key == "RoadWidthInTexture")
				road.widthInTextureText = std::string(field.Value());
		}
		catalog.roads.push_back(std::move(road));
	}
	return catalog;
}
}
