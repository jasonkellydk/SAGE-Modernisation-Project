export module games.generalszh.hosts.game.radar_minimap;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import engine.ecs.query.query;
import games.generalszh.hosts.game.game_client;
import games.generalszh.commands.game_commands;
export import engine.level.presentation.radar_terrain;
import games.generalszh.presentation.hud.algorithms.radar_display;
import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.presentation.audio.resources.audio_resources;
import engine.gameplay.rts.radar.resources.player_radar;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.object_shroud;
import engine.gameplay.rts.vision.resources.shroud_map;
import Engine.Core.Math.FixedPresentation;

// The radar in the control bar's left HUD (the original's W3DLeftHUDDraw -> W3DRadar::drawData, LeftHUDInput), for the
// local player while they have radar (scripts may force it on or hide it): the map's terrain picture keeping its aspect
// (black bars, grey edge lines), the objects' blips (refreshed every 6 frames: the other players' and then the
// player's own, each by radar priority, the newest of a priority first; a structure's cell and the three beside it),
// the shroud over them (the local player's partition cells: shrouded black, fogged half black), the radar events'
// spinning triangles, the view's outline at the terrain's middle height, and the heroes' reticles.
// A click looks there (with units selected, the left button sends them there instead).
export namespace generalszh::host
{
class RadarMinimap
{
public:
	void SetTerrain(engine::level::presentation::RadarTerrain terrain)
	{
		m_terrain = std::move(terrain);
		m_overlay.assign(static_cast<std::size_t>(m_terrain.width) * m_terrain.height * 4, 0);
		m_shroud.assign(m_overlay.size(), 0);
		m_shroudScratch.assign(m_overlay.size(), 0);
		++m_terrainRevision;
		m_hasTerrain = !m_terrain.pixels.empty() && m_terrain.extentWidth > 0.0f && m_terrain.extentHeight > 0.0f;
	}

	// The radar shows (localPlayerHasRadar: forced, else not hidden and the player has radar).
	bool Shown(GameClient &game) const
	{
		const ClientSettings &settings = game.Settings();
		if (settings.radarForced)
			return true;
		session::SessionView *view = game.View();
		const auto player = game.LocalPlayer();
		const auto *radar = view != nullptr ? view->World().FindResource<engine::gameplay::PlayerRadar>() : nullptr;
		return !settings.radarHidden && player && radar != nullptr && radar->Has(*player);
	}

	// W3DRadar::drawData into the window's rectangle (screen pixels), inset a pixel as W3DLeftHUDDraw does.
	void Draw(GameClient &game, Engine::UI::WND::DrawList &list, Graphics::Renderer2D &renderer, float x, float y, float width, float height,
		const Engine::UI::WND::ImageRef &heroIcon, std::pair<float, float> heroSize)
	{
		if (!m_hasTerrain || !Shown(game))
			return;
		session::SessionView *view = game.View();
		if (view == nullptr)
			return;
		++m_frames;
		const std::int32_t px = static_cast<std::int32_t>(x) + 1, py = static_cast<std::int32_t>(y) + 1;
		const std::int32_t pw = static_cast<std::int32_t>(width) - 2, ph = static_cast<std::int32_t>(height) - 2;
		if (pw <= 0 || ph <= 0)
			return;
		const auto [ul, lr] = presentation::FindDrawPositions(m_terrain.extentWidth, m_terrain.extentHeight, px, py, pw, ph);
		const Graphics::Color2D black{0, 0, 0, 1}, edge{50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1};
		if (m_terrain.extentWidth / static_cast<float>(pw) >= m_terrain.extentHeight / static_cast<float>(ph))
		{
			list.Add_Rect({static_cast<float>(px), static_cast<float>(py), static_cast<float>(px + pw), static_cast<float>(ul.y - 1)}, black);
			list.Add_Rect({static_cast<float>(px), static_cast<float>(lr.y + 1), static_cast<float>(px + pw), static_cast<float>(py + ph)}, black);
			list.Add_Line({static_cast<float>(px), static_cast<float>(ul.y)}, {static_cast<float>(px + pw), static_cast<float>(ul.y)}, 1, edge);
			list.Add_Line({static_cast<float>(px), static_cast<float>(lr.y + 1)}, {static_cast<float>(px + pw), static_cast<float>(lr.y + 1)}, 1, edge);
		}
		else
		{
			list.Add_Rect({static_cast<float>(px), static_cast<float>(py), static_cast<float>(ul.x - 1), static_cast<float>(py + ph)}, black);
			list.Add_Rect({static_cast<float>(lr.x + 1), static_cast<float>(py), static_cast<float>(px + pw), static_cast<float>(py + ph)}, black);
			list.Add_Line({static_cast<float>(ul.x), static_cast<float>(py)}, {static_cast<float>(ul.x), static_cast<float>(py + ph)}, 1, edge);
			list.Add_Line({static_cast<float>(lr.x + 1), static_cast<float>(py)}, {static_cast<float>(lr.x + 1), static_cast<float>(py + ph)}, 1, edge);
		}
		const Graphics::Rect2D picture{static_cast<float>(ul.x), static_cast<float>(ul.y), static_cast<float>(lr.x), static_cast<float>(lr.y)};
		// The terrain picture and the blips, their rows from world y = 0 up: drawn flipped.
		Engine::UI::WND::ImageRef terrain;
		terrain.generated = renderer.Register_Texture({Graphics::TextureHandle(0x7AD0001u, 1), m_terrain.width, m_terrain.height, m_terrain.width * 4,
			m_terrainRevision, std::as_bytes(std::span(m_terrain.pixels))});
		terrain.uv = {0, 1, 1, 0};
		list.Add_Image(terrain, picture);
		if (m_frames % 6 == 0 || m_overlayRevision == 0)
		{
			RenderObjects(game, *view);
			++m_overlayRevision;
		}
		Engine::UI::WND::ImageRef overlay;
		overlay.generated = renderer.Register_Texture({Graphics::TextureHandle(0x7AD0002u, 1), m_terrain.width, m_terrain.height, m_terrain.width * 4,
			m_overlayRevision, std::as_bytes(std::span(m_overlay))});
		overlay.uv = {0, 1, 1, 0};
		list.Add_Image(overlay, picture);
		// The shroud.
		if (const auto viewer = game.LocalPlayer())
			if (const auto *shroud = view->World().FindResource<engine::gameplay::ShroudMap>(); shroud != nullptr && *viewer < shroud->Players())
			{
				RenderShroud(*shroud, *viewer);
				Engine::UI::WND::ImageRef layer;
				layer.generated = renderer.Register_Texture({Graphics::TextureHandle(0x7AD0003u, 1), m_terrain.width, m_terrain.height, m_terrain.width * 4,
					m_shroudRevision, std::as_bytes(std::span(m_shroud))});
				layer.uv = {0, 1, 1, 0};
				list.Add_Image(layer, picture);
			}
		const std::int32_t scaledWidth = lr.x - ul.x, scaledHeight = lr.y - ul.y;
		auto &world = view->World();
		const std::uint64_t frame = view->CurrentTick();
		// Heroes of the player's own.
		if (heroIcon.texture.Is_Valid() || heroIcon.generated.index.Is_Valid())
			for (const Blip &blip : m_heroes)
			{
				presentation::RadarPoint at = presentation::RadarToPixel(blip.cell, ul.x, ul.y, scaledWidth, scaledHeight);
				at.x -= static_cast<std::int32_t>(heroSize.first) / 2 - 1;
				at.y -= static_cast<std::int32_t>(heroSize.second) / 2;
				list.Add_Image(heroIcon, {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.x) + heroSize.first,
					static_cast<float>(at.y) + heroSize.second});
			}
		// The events (each seen for the first time plays RadarEvent).
		if (auto *radar = world.FindResource<presentation::RadarEvents>())
			for (presentation::RadarEvent &event : radar->events)
			{
				if (!event.active || event.type == presentation::RadarEventType::Fake || event.type == presentation::RadarEventType::BeaconPulse)
					continue;
				if (!event.soundPlayed)
					if (auto *audio = world.FindResource<presentation::AudioCommands>())
						audio->pending.push_back({presentation::AudioCommand::Kind::Interface, "RadarEvent"});
				event.soundPlayed = true;
				const auto cell = engine::level::presentation::WorldToRadar(m_terrain, event.world[0], event.world[1]);
				const presentation::RadarTriangle triangle =
					presentation::EventTriangle(event, {cell[0], cell[1]}, frame, ul.x, ul.y, scaledWidth, scaledHeight);
				const auto color = [](std::array<std::uint8_t, 4> c) { return Graphics::Color2D{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f}; };
				for (std::size_t side = 0; side < 3; ++side)
				{
					auto a = triangle.points[side], b = triangle.points[(side + 1) % 3];
					if (Clip(a, b, px, py, pw, ph))
						list.Add_Gradient_Line({static_cast<float>(a.x), static_cast<float>(a.y)}, {static_cast<float>(b.x), static_cast<float>(b.y)}, 1,
							color(triangle.startColor), color(triangle.endColor));
				}
			}
		// The view's outline at the middle height.
		if (const auto corners = game.ViewCornersAtZ(m_terrain.terrainAverageZ))
		{
			std::array<presentation::RadarPoint, 4> points;
			for (std::size_t index = 0; index < 4; ++index)
			{
				const presentation::RadarPoint cell{static_cast<std::int32_t>((*corners)[index][0] / m_terrain.SampleX()),
					static_cast<std::int32_t>((*corners)[index][1] / m_terrain.SampleY())};
				points[index] = presentation::RadarToPixel(cell, ul.x, ul.y, scaledWidth, scaledHeight);
			}
			const Graphics::Color2D top{225.0f / 255.0f, 225.0f / 255.0f, 0, 1}, bottom{158.0f / 255.0f, 158.0f / 255.0f, 0, 1};
			const std::array<std::pair<Graphics::Color2D, Graphics::Color2D>, 4> colors{{{top, top}, {top, bottom}, {bottom, bottom}, {bottom, top}}};
			for (std::size_t side = 0; side < 4; ++side)
			{
				auto a = points[side], b = points[(side + 1) % 4];
				if (Clip(a, b, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)))
					list.Add_Gradient_Line({static_cast<float>(a.x), static_cast<float>(a.y)}, {static_cast<float>(b.x), static_cast<float>(b.y)}, 1,
						colors[side].first, colors[side].second);
			}
		}
	}

	// LeftHUDInput's button down within the window (window pixels): the world point, if on the map.
	std::optional<std::array<float, 2>> WorldAt(float localX, float localY, float width, float height) const
	{
		if (!m_hasTerrain)
			return std::nullopt;
		const auto cell = presentation::LocalPixelToRadar({static_cast<std::int32_t>(localX), static_cast<std::int32_t>(localY)}, m_terrain.extentWidth,
			m_terrain.extentHeight, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
		if (!cell)
			return std::nullopt;
		const std::int32_t cx = std::clamp(cell->x, 0, presentation::RadarCellCount - 1), cy = std::clamp(cell->y, 0, presentation::RadarCellCount - 1);
		return std::array<float, 2>{static_cast<float>(cx) * m_terrain.SampleX(), static_cast<float>(cy) * m_terrain.SampleY()};
	}

private:
	struct Blip
	{
		presentation::RadarPoint cell;
		std::array<std::uint8_t, 4> color{};
		int priority{0};
		std::uint32_t id{0};
		bool local{false};
	};

	// Cohen-Sutherland (ClipLine2D) against the rectangle; false when the line is wholly outside.
	static bool Clip(presentation::RadarPoint &a, presentation::RadarPoint &b, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		const std::int32_t loX = x, loY = y, hiX = x + width, hiY = y + height;
		const auto code = [&](const presentation::RadarPoint &p) {
			return (p.x < loX ? 1 : 0) | (p.x > hiX ? 2 : 0) | (p.y < loY ? 4 : 0) | (p.y > hiY ? 8 : 0);
		};
		for (int guard = 0; guard < 8; ++guard)
		{
			const int ca = code(a), cb = code(b);
			if ((ca | cb) == 0)
				return true;
			if ((ca & cb) != 0)
				return false;
			const int out = ca != 0 ? ca : cb;
			presentation::RadarPoint &p = ca != 0 ? a : b;
			const float dx = static_cast<float>(b.x - a.x), dy = static_cast<float>(b.y - a.y);
			if ((out & 8) != 0 && dy != 0.0f)
				p = {static_cast<std::int32_t>(static_cast<float>(a.x) + dx * static_cast<float>(hiY - a.y) / dy), hiY};
			else if ((out & 4) != 0 && dy != 0.0f)
				p = {static_cast<std::int32_t>(static_cast<float>(a.x) + dx * static_cast<float>(loY - a.y) / dy), loY};
			else if ((out & 2) != 0 && dx != 0.0f)
				p = {hiX, static_cast<std::int32_t>(static_cast<float>(a.y) + dy * static_cast<float>(hiX - a.x) / dx)};
			else if ((out & 1) != 0 && dx != 0.0f)
				p = {loX, static_cast<std::int32_t>(static_cast<float>(a.y) + dy * static_cast<float>(loX - a.x) / dx)};
			else
				return false;
		}
		return false;
	}

	// Object::getRadarPriority (0: not on the radar): STRUCTURE 2, UNIT 3, LOCAL_UNIT_ONLY 4; unset: a garrisonable or
	// capturable object is a structure.
	static int Priority(const content::ObjectDefinition &kind)
	{
		const std::string &priority = kind.radarPriority;
		if (priority == "STRUCTURE")
			return 2;
		if (priority == "UNIT")
			return 3;
		if (priority == "LOCAL_UNIT_ONLY")
			return 4;
		if (priority.empty() || priority == "INVALID")
		{
			if (kind.Is("CAPTURABLE") ||
				std::any_of(kind.modules.begin(), kind.modules.end(), [](const content::ModuleEntry &module) { return module.type == "GarrisonContain"; }))
				return 2;
		}
		return 0;
	}

	// W3DRadar::updateObjectTexture / renderObjectList / canRenderObject.
	void RenderObjects(GameClient &game, session::SessionView &view)
	{
		namespace gp = engine::gameplay;
		std::fill(m_overlay.begin(), m_overlay.end(), std::uint8_t{0});
		m_heroes.clear();
		auto &world = view.World();
		const auto viewer = game.LocalPlayer();
		const auto *looks = world.FindResource<presentation::LookCatalog>();
		const auto *relationships = world.FindResource<gp::Relationships>();
		const std::uint64_t frame = view.CurrentTick();
		std::vector<Blip> blips;
		ecs::Query<ecs::Read<gp::Transform>, ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> query(world);
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto refs = chunk.template Get<gp::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transforms.size(); ++row)
			{
				const ecs::Entity entity = entities[row];
				if (world.Get<gp::Dying>(entity) != nullptr || world.Get<gp::OffMap>(entity) != nullptr)
					continue;
				const content::ObjectDefinition &kind = view.Definition(refs[row].index);
				const int priority = Priority(kind);
				if (priority == 0)
					continue;
				const std::uint32_t player = owners[row].player;
				const bool local = viewer && player == *viewer;
				if (priority == 4 && !local)
					continue;
				// Fogged or shrouded to the player: not shown.
				if (const auto *shroud = world.Get<gp::ObjectShroud>(entity); shroud != nullptr && viewer && !shroud->SeenBy(*viewer))
					continue;
				const auto *stealth = world.Get<gp::Stealth>(entity);
				if (stealth != nullptr && viewer && relationships != nullptr && relationships->Enemies(*viewer, player) && stealth->Hidden())
					continue;
				const auto cell = engine::level::presentation::WorldToRadar(m_terrain, Engine::Math::ToFloat(transforms[row].position.x),
					Engine::Math::ToFloat(transforms[row].position.y));
				std::array<std::uint8_t, 4> color{255, 255, 255, 255};
				if (looks != nullptr)
				{
					const auto c = looks->ColorOf(player);
					color = {static_cast<std::uint8_t>(c[0] * 255.0f), static_cast<std::uint8_t>(c[1] * 255.0f), static_cast<std::uint8_t>(c[2] * 255.0f), 255};
				}
				if (stealth != nullptr && stealth->Has(gp::stealth_flag::Stealthed))
					color[3] = presentation::StealthBlinkAlpha(frame);
				const auto *id = world.Get<gp::ObjectId>(entity);
				blips.push_back({{cell[0], cell[1]}, color, priority, id != nullptr ? id->value : 0u, local});
				if (local && kind.Is("HERO"))
					m_heroes.push_back(blips.back());
			}
		});
		// The other players' list, then the player's own; each by priority, the newest of a priority first.
		std::sort(blips.begin(), blips.end(), [](const Blip &a, const Blip &b) {
			if (a.local != b.local)
				return !a.local;
			if (a.priority != b.priority)
				return a.priority < b.priority;
			return a.id > b.id;
		});
		const auto put = [&](std::int32_t x, std::int32_t y, const std::array<std::uint8_t, 4> &color) {
			if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(m_terrain.width) || y >= static_cast<std::int32_t>(m_terrain.height))
				return;
			std::copy(color.begin(), color.end(), m_overlay.begin() + (static_cast<std::ptrdiff_t>(y) * m_terrain.width + x) * 4);
		};
		for (const Blip &blip : blips)
		{
			put(blip.cell.x, blip.cell.y, blip.color);
			put(blip.cell.x, blip.cell.y + 1, blip.color);
			put(blip.cell.x + 1, blip.cell.y + 1, blip.color);
			put(blip.cell.x + 1, blip.cell.y, blip.color);
		}
	}

	// W3DRadar::setShroudLevel for every partition cell in order (refreshShroudForLocalPlayer): the radar pixels from
	// the cell's low corner to its high one, inclusive, black at alpha 255 shrouded, 127 fogged, 0 clear. A new picture
	// only when it changed.
	void RenderShroud(const engine::gameplay::ShroudMap &shroud, std::uint32_t player)
	{
		std::fill(m_shroudScratch.begin(), m_shroudScratch.end(), std::uint8_t{0});
		const float size = Engine::Math::ToFloat(shroud.CellSize());
		for (std::int32_t y = 0; y < shroud.CellsY(); ++y)
			for (std::int32_t x = 0; x < shroud.CellsX(); ++x)
			{
				const engine::gameplay::CellShroud status = shroud.Status(player, x, y);
				const std::uint8_t alpha = status == engine::gameplay::CellShroud::Shrouded ? 255 : status == engine::gameplay::CellShroud::Fogged ? 127 : 0;
				const auto low = engine::level::presentation::WorldToRadar(m_terrain, static_cast<float>(x) * size, static_cast<float>(y) * size);
				const auto high = engine::level::presentation::WorldToRadar(m_terrain, static_cast<float>(x + 1) * size, static_cast<float>(y + 1) * size);
				for (std::int32_t py = low[1]; py <= high[1] && py < static_cast<std::int32_t>(m_terrain.height); ++py)
					for (std::int32_t px = low[0]; px <= high[0] && px < static_cast<std::int32_t>(m_terrain.width); ++px)
						m_shroudScratch[(static_cast<std::size_t>(py) * m_terrain.width + static_cast<std::size_t>(px)) * 4 + 3] = alpha;
			}
		if (m_shroudScratch != m_shroud || m_shroudRevision == 0)
		{
			m_shroud.swap(m_shroudScratch);
			++m_shroudRevision;
		}
	}

	engine::level::presentation::RadarTerrain m_terrain;
	std::vector<std::uint8_t> m_overlay;
	std::vector<std::uint8_t> m_shroud;
	std::vector<std::uint8_t> m_shroudScratch;
	std::uint32_t m_shroudRevision{0};
	std::vector<Blip> m_heroes;
	std::uint32_t m_terrainRevision{0};
	std::uint32_t m_overlayRevision{0};
	std::uint64_t m_frames{0};
	bool m_hasTerrain{false};
};
}
