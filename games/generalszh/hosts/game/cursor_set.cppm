module;
#include <cstdio>

export module games.generalszh.hosts.game.cursor_set;
import std;

import Graphics.Cursors.Ani;
import engine.filesystem.core.virtual_file_system;
export import games.generalszh.content.global.mouse;

// The mouse cursors as the system shows them: WinCursors (GlobalData's default) makes them system cursors, each of
// Mouse.ini's MouseCursor blocks' Data/Cursors/<Texture>.ANI (one per direction), all loaded up front
// (Win32Mouse::initCursorResources). The arrow shows first (W3DMouse::init) and in the shell; a match asks for the
// others (InGameUI::setMouseCursor).
export namespace generalszh::host
{
struct CursorSet
{
	static constexpr std::size_t Kinds = static_cast<std::size_t>(content::MouseCursorKind::Count);
	std::array<std::vector<std::unique_ptr<Graphics::Cursor>>, Kinds> cursors;
	std::array<std::uint8_t, 2> shown{0xFF, 0xFF}; // the kind and direction showing

	void Load(const content::MouseContent &mouse, const engine::filesystem::VirtualFileSystem &files)
	{
		for (std::size_t kind = 0; kind < Kinds; ++kind)
		{
			const auto &definition = mouse.cursors[kind];
			for (int direction = 0; direction < definition.directions && !definition.texture.empty(); ++direction)
			{
				const std::string path = content::MouseCursorFile(definition, direction);
				auto cursor = std::make_unique<Graphics::Cursor>();
				const auto bytes = files.Read(path);
				if (!bytes || !Graphics::Load_Ani_Cursor(*cursor, *bytes, static_cast<int>(definition.fps.Floor()), definition.hotSpot[0], definition.hotSpot[1]))
				{
					std::fprintf(stderr, "mouse: cursor '%s' could not be read\n", path.c_str());
					cursor.reset();
				}
				cursors[kind].push_back(std::move(cursor));
			}
		}
	}

	// Win32Mouse::setCursor: NONE (or no cursor for it) shows none.
	void Show(std::array<std::uint8_t, 2> wanted)
	{
		if (wanted == shown)
			return;
		shown = wanted;
		const auto &directions = cursors[std::min<std::size_t>(wanted[0], Kinds - 1)];
		const Graphics::Cursor *cursor = wanted[1] < directions.size() ? directions[wanted[1]].get() : nullptr;
		if (cursor == nullptr || !cursor->Show())
			std::fprintf(stderr, "mouse: cursor %u (direction %u) could not be shown\n", wanted[0], wanted[1]);
	}
};
}
