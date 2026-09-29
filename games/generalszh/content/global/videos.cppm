export module games.generalszh.content.global.videos;
import std;

export import engine.config.binding.schema;

// Video.ini (VideoPlayer::init over Data/INI/Default/Video and Data/INI/Video): each movie by its internal name (the
// block's name) and its file. A movie named that is no Video block is looked for under its own name
// (Movie_File_Name), ".bik" added unless it ends so; the file is found in Data/<language>/Movies, else Data/Movies
// (Open_Movie_File).
export namespace generalszh::content
{
struct VideoCatalog
{
	std::map<std::string, std::string, std::less<>> files; // internal name -> Filename

	// Movie_File_Name: its file's name, with ".bik".
	std::string FileName(std::string_view movie) const
	{
		const auto found = files.find(movie);
		std::string name = found != files.end() && !found->second.empty() ? found->second : std::string(movie);
		const bool bik = name.size() >= 4 && name[name.size() - 4] == '.' && std::tolower(static_cast<unsigned char>(name[name.size() - 3])) == 'b' &&
			std::tolower(static_cast<unsigned char>(name[name.size() - 2])) == 'i' && std::tolower(static_cast<unsigned char>(name[name.size() - 1])) == 'k';
		if (!bik)
			name += ".bik";
		return name;
	}

	// Open_Movie_File's order (a mod folder aside): the language's Movies, then the game's.
	std::array<std::string, 2> Paths(std::string_view movie, std::string_view language) const
	{
		const std::string file = FileName(movie);
		return {"Data/" + std::string(language) + "/Movies/" + file, "Data/Movies/" + file};
	}
};

inline VideoCatalog BindVideos(const engine::config::Document &document)
{
	VideoCatalog catalog;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Video" || root.values.empty())
			continue;
		std::string file;
		for (const engine::config::Node &field : root.children)
			if (field.key == "Filename" && !field.values.empty())
				file = std::string(field.values.front());
		catalog.files.insert_or_assign(std::string(root.values.front()), std::move(file));
	}
	return catalog;
}
}
