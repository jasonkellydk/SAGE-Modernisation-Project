export module games.generalszh.presentation.camera.algorithms.camera_keys;
import std;

// The camera's keys (CommandXlat's MSG_META_BEGIN/END_CAMERA_ROTATE_LEFT/RIGHT and BEGIN/END_CAMERA_ZOOM_IN/OUT set
// InGameUI's flags; InGameUI::update turns or zooms the tactical view each client frame while one is held), its view
// bookmarks (LookAtTranslator's MSG_META_SAVE_VIEWn / VIEW_VIEWn: m_viewLocation[8], View::getLocation /
// setLocation) and its reset (InGameUI::resetCamera). Presentation runs on real time: a held key turns or zooms by its
// per-frame amount times the client frames (1/30 s each) the time covers.
export namespace generalszh::presentation
{
// InGameUI's m_cameraRotatingLeft / Right, m_cameraZoomingIn / Out.
struct CameraKeys
{
	bool rotateLeft{false}, rotateRight{false}, zoomIn{false}, zoomOut{false};
};

// The view's change for `frames` client frames of held keys: View::setAngle(angle -/+ KeyboardCameraRotateSpeed), left
// only when right is not held and the other way round; View::zoomIn / zoomOut: the height above ground -/+ 10, likewise.
struct CameraKeyMotion
{
	float angle{0.0f};
	float height{0.0f};
};

inline CameraKeyMotion CameraKeyStep(const CameraKeys &keys, float frames, float rotateSpeed) noexcept
{
	CameraKeyMotion motion;
	if (keys.rotateLeft && !keys.rotateRight)
		motion.angle -= rotateSpeed * frames;
	if (keys.rotateRight && !keys.rotateLeft)
		motion.angle += rotateSpeed * frames;
	if (keys.zoomIn && !keys.zoomOut)
		motion.height -= 10.0f * frames;
	if (keys.zoomOut && !keys.zoomIn)
		motion.height += 10.0f * frames;
	return motion;
}

// A CommandXlat camera key meta-event onto the held keys; false: not one of them.
inline bool ApplyCameraKeyMeta(CameraKeys &keys, std::string_view meta) noexcept
{
	if (meta == "BEGIN_CAMERA_ROTATE_LEFT")
		keys.rotateLeft = true;
	else if (meta == "END_CAMERA_ROTATE_LEFT")
		keys.rotateLeft = false;
	else if (meta == "BEGIN_CAMERA_ROTATE_RIGHT")
		keys.rotateRight = true;
	else if (meta == "END_CAMERA_ROTATE_RIGHT")
		keys.rotateRight = false;
	else if (meta == "BEGIN_CAMERA_ZOOM_IN")
		keys.zoomIn = true;
	else if (meta == "END_CAMERA_ZOOM_IN")
		keys.zoomIn = false;
	else if (meta == "BEGIN_CAMERA_ZOOM_OUT")
		keys.zoomOut = true;
	else if (meta == "END_CAMERA_ZOOM_OUT")
		keys.zoomOut = false;
	else
		return false;
	return true;
}

// ViewLocation: where the view was (its pivot, angle, pitch and zoom); invalid until saved.
struct ViewLocation
{
	bool valid{false};
	std::array<float, 3> position{};
	float angle{0.0f}, pitch{0.0f}, zoom{0.0f};
};

inline constexpr int MaxViewLocations = 8; // MAX_VIEW_LOCS

// LookAtTranslator's bookmarks (presentation state of the client, kept from one game to the next as the translator is).
struct ViewBookmarks
{
	std::array<ViewLocation, MaxViewLocations> slots{};
};

// "SAVE_VIEW<n>" / "VIEW_VIEW<n>": the slot (1..8) and whether it saves; none: not a bookmark meta-event.
struct BookmarkMeta
{
	int slot{0};
	bool save{false};
};

inline std::optional<BookmarkMeta> ParseBookmarkMeta(std::string_view meta) noexcept
{
	const auto slotOf = [](std::string_view rest) -> int {
		return rest.size() == 1 && rest[0] >= '1' && rest[0] <= '8' ? rest[0] - '0' : 0;
	};
	if (meta.starts_with("SAVE_VIEW"))
	{
		if (const int slot = slotOf(meta.substr(9)))
			return BookmarkMeta{slot, true};
	}
	else if (meta.starts_with("VIEW_VIEW"))
	{
		if (const int slot = slotOf(meta.substr(9)))
			return BookmarkMeta{slot, false};
	}
	return std::nullopt;
}

// GUI:BookmarkXSet's "%d" replaced by the slot (UnicodeString::format).
inline std::u16string BookmarkSetMessage(std::u16string format, int slot)
{
	const std::string number = std::to_string(slot);
	if (const auto at = format.find(u"%d"); at != std::u16string::npos)
		format.replace(at, 2, std::u16string(number.begin(), number.end()));
	return format;
}
}
