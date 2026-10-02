export module Engine.UI.WND.Controls;
import std;

import Engine.UI.WND;

namespace Engine::UI::WND
{

// These are the control types authored by the Generals WND format.  The
// parser has its own WindowType because it also preserves unknown type names;
// this enum is intentionally usable by tools and hand-built test fixtures.
export enum class ControlKind : std::uint8_t
{
	Unknown,
	User,
	PushButton,
	CheckBox,
	RadioButton,
	TabControl,
	ListBox,
	ComboBox,
	HorizontalSlider,
	VerticalSlider,
	ProgressBar,
	StaticText,
	TextEntry
};

// A picture shown letterboxed (the original's W3DDrawMapPreview): an image kept to the aspect of
// what it shows (`extent_width` by `extent_height`), bars beside it, marker images over it at
// fractions of the image (per 10000, from its top left). No image: a grey box.
export struct WNDPictureMarker final
{
	int x = 0;
	int y = 0;
	ImageRef image{};
	int size = 0; // square, in layout units
};

// The thin frame round it (Add_WND_Skinny_Border's FrameT/B/L/R 5-pixel tiles and FrameCorner* images).
export struct WNDFrame final
{
	ImageRef top{}, bottom{}, left{}, right{};
	ImageRef upper_left{}, upper_right{}, lower_left{}, lower_right{};
};

export struct WNDPicture final
{
	ImageRef image{};
	WNDFrame frame{};
	int extent_width = 1;
	int extent_height = 1;
	std::vector<WNDPictureMarker> markers;
};

// findDrawPositions: where a picture of `width` by `height` fits in `region`, centred, whole.
export constexpr Rect Letterbox(Rect region, int width, int height) noexcept
{
	const long long areaWidth = region.right - region.left, areaHeight = region.bottom - region.top;
	if (width <= 0 || height <= 0 || areaWidth <= 0 || areaHeight <= 0)
		return region;
	// ratioWidth >= ratioHeight: width / areaWidth >= height / areaHeight
	if (static_cast<long long>(width) * areaHeight >= static_cast<long long>(height) * areaWidth)
	{
		const long long shown = static_cast<long long>(height) * areaWidth / width;
		const int top = region.top + static_cast<int>((areaHeight - shown) / 2);
		return {region.left, top, region.right, region.bottom - (top - region.top)};
	}
	const long long shown = static_cast<long long>(width) * areaHeight / height;
	const int left = region.left + static_cast<int>((areaWidth - shown) / 2);
	return {left, region.top, region.right - (left - region.left), region.bottom};
}

// An image in a list box's cell (GadgetListBoxAddEntryImage): its size in layout units and its
// tint (a battle honour not yet gained is drawn dark).
export struct WNDListImage final
{
	ImageRef image{};
	int width = 0;
	int height = 0;
	std::uint32_t rgba = 0xFFFFFFFF;
};

export struct ControlVisual final
{
	ControlKind kind = ControlKind::Unknown;
	Graphics::Rect2D rectangle{};
	const WNDDrawState *state = nullptr;
	const WNDDrawState *thumb_state = nullptr;
	const WNDDrawState *secondary_state = nullptr;
	// All three draw states (enabled, disabled, hilite): image-style sliders draw from several.
	const WNDDrawState *states = nullptr;
	bool highlighted = false;
	// List and combo boxes: their items, the selected and pointed-at ones, the
	// first shown; a combo box's closed box, its list and its list's draw state.
	const std::u16string *entries = nullptr;
	std::size_t entry_count = 0;
	const std::uint32_t *entry_colors = nullptr; // each row's text colour (0xRRGGBBAA; 0: the list's)
	const std::vector<std::vector<WNDListImage>> *entry_images = nullptr; // each row's cells' images (by column)
	std::vector<float> row_heights;                                      // the shown rows' heights (rows of images are taller)
	std::size_t entry_color_count = 0;
	int selected = -1;
	int hovered_entry = -1;
	int list_top = 0;
	bool list_open = false;
	Graphics::Rect2D box_rectangle{};
	Graphics::Rect2D list_rectangle{};
	float row_height = 0.0f;
	std::size_t list_rows = 0;
	const WNDDrawState *list_state = nullptr;
	TextStyle highlighted_text_style{};
	// A list box's columns (percent of its width).
	const int *column_widths = nullptr;
	std::size_t column_count = 0;
	// A text entry: its caret shows while it has the keyboard; what it shows when secret.
	bool caret = false;
	std::u16string shown_text;
	float scale = 1.0f;
	int minimum = 0;
	int maximum = 100;
	int position = 50;
	int progress = 50;
	std::uint32_t list_length = 4;
	std::uint32_t list_columns = 1;
	bool checked = false;
	bool image_style = false;
	// A USER window drawn by W3DGadgetPushButtonImageDraw (DRAWCALLBACK): as a one-image push button.
	bool push_image_draw = false;
	// A command button (USE_OVERLAY_STATES), whether it is enabled, and its ALWAYS_COLOR / NOT_READY status.
	bool overlay_states = false;
	bool enabled = true;
	bool always_color = false;
	bool not_ready = false;
	bool flashing = false; // WIN_STATUS_FLASHING: the pushed overlay over it
	bool clock = false; // a push button's clock (see WNDWindow)
	int clock_percent = 0;
	bool clock_remaining = false;
	Graphics::Color2D clock_color{};
	ImageRef overlay_image{}; // a push button's overlay image (GadgetButtonDrawOverlayImage), over its whole rectangle
	bool extra_border = false; // a push button's border (GadgetButtonSetBorder), one pixel outside it
	Graphics::Color2D extra_border_color{};
	const ImageRef *highlighted_overlay = nullptr; // Cameo_hilited
	const ImageRef *pushed_overlay = nullptr;      // Cameo_push
	bool wrap_centered = false;
	const FontFace *font = nullptr;
	const FontFace *hotkey_font = nullptr;
	const std::uint16_t *text = nullptr;
	TextStyle text_style{};
	bool centered_text = false;
	bool centered_text_vertically = true;
	const WNDPicture *picture = nullptr; // a letterboxed picture instead of the window's own look
	const ImageRef *video = nullptr;     // a movie's frame over the window's own look (setVideoBuffer)
	// A list box's scroll bar.
	bool has_scroll_bar = false;
	const WNDDrawState *scroll_up = nullptr;
	const WNDDrawState *scroll_down = nullptr;
	const WNDDrawState *scroll_track = nullptr;
	const WNDDrawState *scroll_thumb = nullptr;
	Graphics::Rect2D scroll_up_rectangle{}, scroll_down_rectangle{}, scroll_track_rectangle{}, scroll_thumb_rectangle{};
	Rect region{};                       // the window's region in layout units (for a picture)
};

namespace WNDControlsDetail
{

const WNDDrawCell &Cell(const WNDDrawState &state, std::size_t index) noexcept
{
	return state.cells[(std::min)(index, WND_Draw_Cell_Count - 1)];
}

bool Has_Image(const WNDDrawState &state, std::size_t index) noexcept
{
	return Cell(state, index).image.texture.Is_Valid();
}

bool Add_Generic_Background(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawCell &cell = Cell(*visual.state, 0);
	if (cell.image.texture.Is_Valid()) {
		if (!draw_list.Add_Image(cell.image, visual.rectangle))
			return false;
	}
	else if (cell.color.alpha > 0.0f
		&& !draw_list.Add_Rect(visual.rectangle, cell.color)) {
		return false;
	}
	return cell.border_color.alpha <= 0.0f
		|| draw_list.Add_Outline(visual.rectangle, 1.0f, cell.border_color);
}

struct ListContext final
{
	Graphics::Rect2D rectangle{};
	std::uint32_t rows = 4;
};

bool Query_List_Row(void *opaque, std::uint32_t row, ListBoxRowVisual &visual) noexcept
{
	const auto &context = *static_cast<const ListContext *>(opaque);
	const float height = (context.rectangle.bottom - context.rectangle.top)
		/ static_cast<float>((std::max)(1u, context.rows));
	visual.rectangle = {
		context.rectangle.left,
		context.rectangle.top + height * static_cast<float>(row),
		context.rectangle.right,
		context.rectangle.top + height * static_cast<float>(row + 1)};
	visual.selected = row == 1;
	return true;
}

bool Emit_List_Cell(
	void *, DrawList &, std::uint32_t, std::uint32_t,
	Graphics::Rect2D, Graphics::Rect2D) noexcept
{
	// A WND list box receives its row text from the runtime list model.  The
	// visual regression gallery deliberately leaves those cells empty so that
	// the atlas-backed selection treatment is tested independently.
	return true;
}

// W3DGameWinDefaultDrawData: a window with the IMAGE status draws its image alone; any
// other draws its colour and border, whatever image its draw data names.
bool Render_User(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawCell &cell = Cell(*visual.state, 0);
	// W3DGadgetPushButtonImageDrawOne without overlay states: its state's image (the enabled one, or the disabled one
	// while disabled; none: nothing), then its overlay image (GadgetButtonDrawOverlayImage), never its colour.
	if (visual.push_image_draw)
		return (!cell.image.texture.Is_Valid() || draw_list.Add_Image(cell.image, visual.rectangle)) &&
			(!visual.overlay_image.texture.Is_Valid() || draw_list.Add_Image(visual.overlay_image, visual.rectangle));
	if (visual.image_style)
		return !cell.image.texture.Is_Valid() || draw_list.Add_Image(cell.image, visual.rectangle);
	if (cell.color.alpha > 0.0f && !draw_list.Add_Rect(visual.rectangle, cell.color))
		return false;
	return cell.border_color.alpha <= 0.0f || draw_list.Add_Outline(visual.rectangle, 1.0f, cell.border_color);
}

bool Render_Push_Button(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	PushButtonVisual button;
	button.rectangle = visual.rectangle;
	button.image_color = {1.0f, 1.0f, 1.0f, 1.0f};
	if (Has_Image(state, 0) && Has_Image(state, 5) && Has_Image(state, 6)) {
		button.segmented = true;
		button.left_image = Cell(state, 0).image;
		button.middle_image = Cell(state, 5).image;
		button.right_image = Cell(state, 6).image;
		button.left_width = Cell(state, 0).image_width * visual.scale;
		button.middle_width = Cell(state, 5).image_width * visual.scale;
		button.right_width = Cell(state, 6).image_width * visual.scale;
	}
	else if (visual.overlay_states && visual.states != nullptr && Has_Image(visual.states[0], 0)) {
		// W3DGadgetPushButtonImageDraw with USE_OVERLAY_STATES: the enabled image whatever the state; disabled (and
		// not NOT_READY) it draws grey, or dimmed to 144 with ALWAYS_COLOR.
		button.has_image = true;
		button.image = Cell(visual.states[0], 0).image;
		button.use_overlay_states = true;
		button.enabled = visual.enabled;
		button.highlighted = visual.highlighted;
		button.selected = visual.checked;
		button.has_highlighted_overlay = visual.highlighted_overlay != nullptr && visual.highlighted_overlay->texture.Is_Valid();
		if (button.has_highlighted_overlay)
			button.highlighted_overlay = *visual.highlighted_overlay;
		button.has_pushed_overlay = visual.pushed_overlay != nullptr && visual.pushed_overlay->texture.Is_Valid();
		if (button.has_pushed_overlay)
			button.pushed_overlay = *visual.pushed_overlay;
		if (!visual.enabled && !visual.not_ready) {
			if (!visual.always_color)
				button.grayscale = true;
			else
				button.image_color = {144.0f / 255.0f, 144.0f / 255.0f, 144.0f / 255.0f, 1.0f};
		}
	}
	else if (visual.checked && visual.states != nullptr && Has_Image(visual.states[&state == &visual.states[1] ? 1 : 2], 1)) {
		// Selected (W3DGadgetPushButtonImageDraw): the disabled selected image, else the hilite selected one.
		button.has_image = true;
		button.image = Cell(visual.states[&state == &visual.states[1] ? 1 : 2], 1).image;
	}
	else if (Has_Image(state, 0)) {
		button.has_image = true;
		button.image = Cell(state, 0).image;
	}
	else {
		button.has_fill = Cell(state, 0).color.alpha > 0.0f;
		button.fill_color = Cell(state, 0).color;
		button.has_border = Cell(state, 0).border_color.alpha > 0.0f;
		button.border_color = Cell(state, 0).border_color;
	}
	// W3DGadgetPushButtonImageDraw: a flashing button (WIN_STATUS_FLASHING) draws the Cameo_push image over itself.
	if (visual.flashing && visual.pushed_overlay != nullptr && visual.pushed_overlay->texture.Is_Valid()) {
		button.flashing = true;
		button.flashing_image = *visual.pushed_overlay;
	}
	// W3DGadgetPushButtonImageDraw: the button's overlay image (PushButtonData::overlayImage) over its whole window.
	if (visual.overlay_image.texture.Is_Valid()) {
		button.has_overlay = true;
		button.overlay_image = visual.overlay_image;
	}
	if (visual.clock) {
		button.has_clock = true;
		button.clock_percent = visual.clock_percent;
		button.remaining_clock = visual.clock_remaining;
		button.clock_color = visual.clock_color;
	}
	// W3DGadgetPushButtonImageDraw: drawOpenRect(start - 1, size + 2, 1, colorBorder) when a border is set.
	if (visual.extra_border) {
		button.has_extra_border = true;
		button.extra_border = {visual.rectangle.left - 1.0f, visual.rectangle.top - 1.0f, visual.rectangle.right + 1.0f, visual.rectangle.bottom + 1.0f};
		button.extra_border_color = visual.extra_border_color;
	}
	return Add_Push_Button_Background(draw_list, button)
		&& Add_Push_Button_Overlays(draw_list, button);
}

bool Render_Check_Box(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	if (visual.image_style) {
		// W3DGadgetCheckBoxImageDraw: the state's box image only (unchecked cell 1, checked cell 2),
		// a square 3 pixels in from the top and bottom; no background.
		CheckBoxVisual box;
		box.rectangle = visual.rectangle;
		box.checked = visual.checked;
		const std::size_t cell = visual.checked ? 2 : 1;
		if (Has_Image(state, cell)) {
			const float inset = 3.0f * visual.scale;
			box.has_box_image = true;
			box.box_image = Cell(state, cell).image;
			box.box_image_rectangle = {visual.rectangle.left, visual.rectangle.top + inset,
				visual.rectangle.left + (visual.rectangle.bottom - visual.rectangle.top) - 2.0f * inset, visual.rectangle.bottom - inset};
		}
		return Add_Check_Box_Visual(draw_list, box);
	}
	const float box_size = (std::max)(1.0f,
		visual.rectangle.bottom - visual.rectangle.top);
	CheckBoxVisual box;
	box.rectangle = visual.rectangle;
	box.box_rectangle = {
		visual.rectangle.left,
		visual.rectangle.top,
		visual.rectangle.left + box_size,
		visual.rectangle.top + box_size};
	box.checked = visual.checked;
	box.line_width = visual.scale;
	if (Has_Image(state, 0)) {
		box.has_background_image = true;
		box.background_image = Cell(state, 0).image;
		box.background_image_rectangle = visual.rectangle;
	}
	else {
		box.has_background_fill = Cell(state, 0).color.alpha > 0.0f;
		box.background_fill = Cell(state, 0).color;
		box.has_background_border = Cell(state, 0).border_color.alpha > 0.0f;
		box.background_border = Cell(state, 0).border_color;
	}
	if (Has_Image(state, visual.checked ? 2 : 1)) {
		box.has_box_image = true;
		box.box_image = Cell(state, visual.checked ? 2 : 1).image;
		box.box_image_rectangle = box.box_rectangle;
	}
	else {
		box.has_box_border = true;
		box.box_border = Cell(state, 1).border_color;
		box.has_box_fill = Cell(state, 1).color.alpha > 0.0f;
		box.box_fill = Cell(state, 1).color;
	}
	box.check_color = Cell(state, 2).border_color;
	return Add_Check_Box_Visual(draw_list, box);
}

bool Render_Radio_Button(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	RadioButtonVisual radio;
	radio.rectangle = visual.rectangle;
	// W3DGadgetRadioButtonImageDraw: a selected radio draws the hilite data's cells 3-5 (GadgetRadioGetSelected*Image).
	const bool selected_images = visual.image_style && visual.checked && visual.states != nullptr
		&& Has_Image(visual.states[2], 3) && Has_Image(visual.states[2], 4) && Has_Image(visual.states[2], 5);
	const WNDDrawState &images = selected_images ? visual.states[2] : state;
	const std::size_t first = selected_images ? 3 : 0;
	radio.middle_width = Has_Image(images, first + 1)
		? Cell(images, first + 1).image_width * visual.scale : 0.0f;
	if (Has_Image(images, first) && Has_Image(images, first + 1) && Has_Image(images, first + 2)) {
		radio.segmented_images = true;
		radio.left_image = Cell(images, first).image;
		radio.middle_image = Cell(images, first + 1).image;
		radio.right_image = Cell(images, first + 2).image;
		const float left = visual.rectangle.left;
		const float right = visual.rectangle.right;
		const float left_width = Cell(images, first).image_width * visual.scale;
		const float right_width = Cell(images, first + 2).image_width * visual.scale;
		radio.left_image_rectangle = {left, visual.rectangle.top,
		left + left_width, visual.rectangle.bottom};
		radio.right_image_rectangle = {right - right_width, visual.rectangle.top,
		right, visual.rectangle.bottom};
		radio.middle_image_rectangle = {radio.left_image_rectangle.right,
		visual.rectangle.top, radio.right_image_rectangle.left, visual.rectangle.bottom};
	}
	else {
		radio.has_background_fill = Cell(state, 0).color.alpha > 0.0f;
		radio.background_fill = Cell(state, 0).color;
		radio.has_background_border = Cell(state, 0).border_color.alpha > 0.0f;
		radio.background_border = Cell(state, 0).border_color;
		const float half = (visual.rectangle.right - visual.rectangle.left) * 0.5f;
		radio.left_box_rectangle = {visual.rectangle.left, visual.rectangle.top,
		visual.rectangle.left + half - 1.0f, visual.rectangle.bottom};
		radio.right_box_rectangle = {visual.rectangle.left + half + 1.0f,
		visual.rectangle.top, visual.rectangle.right, visual.rectangle.bottom};
		radio.has_box_fill = visual.checked;
		radio.box_fill = Cell(state, 1).color;
	}
	return Add_Radio_Button_Visual(draw_list, radio);
}

bool Render_Progress_Bar(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	if (Has_Image(state, 0) && Has_Image(state, 1) && Has_Image(state, 2)) {
		ProgressBarImageVisual image;
		image.rectangle = visual.rectangle;
		const float left_width = Cell(state, 0).image_width * visual.scale;
		const float right_width = Cell(state, 1).image_width * visual.scale;
		image.background_left = Cell(state, 0).image;
		image.background_center = Cell(state, 2).image;
		image.background_right = Cell(state, 1).image;
		image.background_left_rectangle = {visual.rectangle.left, visual.rectangle.top,
			visual.rectangle.left + left_width, visual.rectangle.bottom};
		image.background_right_rectangle = {visual.rectangle.right - right_width,
			visual.rectangle.top, visual.rectangle.right, visual.rectangle.bottom};
		image.background_center_rectangle = {image.background_left_rectangle.right,
			visual.rectangle.top, image.background_right_rectangle.left, visual.rectangle.bottom};
		image.background_center_width = (std::max)(1.0f,
			Cell(state, 2).image_width * visual.scale);
		image.bar_center = Has_Image(state, 6) ? Cell(state, 6).image : Cell(state, 2).image;
		image.bar_right = image.bar_center;
		const float progress = (std::clamp(visual.progress, 0, 100) / 100.0f);
		image.bar_rectangle = {image.background_center_rectangle.left,
			visual.rectangle.top,
			image.background_center_rectangle.left
				+ (image.background_center_rectangle.right - image.background_center_rectangle.left) * progress,
			visual.rectangle.bottom};
		image.bar_center_width = (std::max)(1.0f, Cell(state, 6).image_width * visual.scale);
		image.bar_right_width = image.bar_center_width;
		return Add_Progress_Bar_Image_Visual(draw_list, image);
	}
	ProgressBarVisual bar;
	bar.rectangle = visual.rectangle;
	bar.progress = visual.progress;
	bar.has_background_fill = Cell(state, 0).color.alpha > 0.0f;
	bar.background_fill = Cell(state, 0).color;
	bar.has_background_border = Cell(state, 0).border_color.alpha > 0.0f;
	bar.background_border = Cell(state, 0).border_color;
	bar.has_bar_fill = true;
	bar.bar_fill = Cell(state, 4).color;
	return Add_Progress_Bar_Visual(draw_list, bar);
}

bool Render_Slider(DrawList &draw_list, const ControlVisual &visual, bool vertical) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	if (visual.image_style && !vertical && visual.states != nullptr) {
		// W3DGadgetHorizontalSliderImageDraw: a row of boxes, filled (the disabled left image) up to the
		// position, empty (the disabled right image) after, outlined (the hilite left image) while lit.
		const WNDDrawState &enabled = visual.states[0], &disabled = visual.states[1], &hilite = visual.states[2];
		(void)enabled;
		if (Has_Image(hilite, 0) && Has_Image(disabled, 0) && Has_Image(disabled, 1)) {
			HorizontalSliderImageVisual boxes;
			boxes.highlighted_image = Cell(hilite, 0).image;
			boxes.selected_image = Cell(disabled, 0).image;
			boxes.unselected_image = Cell(disabled, 1).image;
			Layout_Horizontal_Slider_Images(boxes, visual.rectangle, Cell(disabled, 0).image_width, visual.scale,
				visual.minimum, visual.maximum, visual.position);
			boxes.highlighted = visual.highlighted;
			return Add_Horizontal_Slider_Image_Visual(draw_list, boxes);
		}
	}
	SliderVisual slider;
	slider.rectangle = visual.rectangle;
	slider.has_background_fill = Cell(state, 0).color.alpha > 0.0f;
	slider.background_fill = Cell(state, 0).color;
	slider.has_background_border = Cell(state, 0).border_color.alpha > 0.0f;
	slider.background_border = Cell(state, 0).border_color;
	if (!Add_Slider_Visual(draw_list, slider))
		return false;
	if (visual.thumb_state == nullptr || !Has_Image(*visual.thumb_state, 0))
		return true;
	const WNDDrawCell &thumb = Cell(*visual.thumb_state, 0);
	const float ratio = visual.maximum > visual.minimum
		? static_cast<float>(visual.position - visual.minimum)
			/ static_cast<float>(visual.maximum - visual.minimum) : 0.0f;
	const float width = thumb.image_width * visual.scale;
	const float height = thumb.image_height * visual.scale;
	Graphics::Rect2D target = visual.rectangle;
	if (vertical) {
		target.left = visual.rectangle.left + (visual.rectangle.right - visual.rectangle.left - width) * 0.5f;
		target.right = target.left + width;
		target.top = visual.rectangle.top + (visual.rectangle.bottom - visual.rectangle.top - height) * ratio;
		target.bottom = target.top + height;
	}
	else {
		target.left = visual.rectangle.left + (visual.rectangle.right - visual.rectangle.left - width) * ratio;
		target.right = target.left + width;
		target.top = visual.rectangle.top + (visual.rectangle.bottom - visual.rectangle.top - height) * 0.5f;
		target.bottom = target.top + height;
	}
	return draw_list.Add_Image(thumb.image, target);
}

bool Render_Text_Entry(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	const WNDDrawState *source = visual.secondary_state != nullptr
		? visual.secondary_state : visual.state;
	if (source == nullptr)
		return false;
	const WNDDrawState &state = *source;
	TextEntryVisual entry;
	entry.rectangle = visual.rectangle;
	if (Has_Image(state, 0) && Has_Image(state, 1) && Has_Image(state, 2)) {
		entry.segmented_image = true;
		entry.left_image = Cell(state, 0).image;
		entry.right_image = Cell(state, 1).image;
		entry.center_image = Cell(state, 2).image;
		entry.small_center_image = Cell(state, 3).image;
		entry.left_width = Cell(state, 0).image_width * visual.scale;
		entry.right_width = Cell(state, 1).image_width * visual.scale;
		entry.center_width = (std::max)(1u, Cell(state, 2).image_width) * visual.scale;
		entry.small_center_width = (std::max)(1u, Cell(state, 3).image_width) * visual.scale;
	}
	else {
		entry.has_fill = Cell(state, 0).color.alpha > 0.0f;
		entry.fill = Cell(state, 0).color;
		entry.has_border = Cell(state, 0).border_color.alpha > 0.0f;
		entry.border = Cell(state, 0).border_color;
	}
	return Add_Text_Entry_Background(draw_list, entry);
}

// One line of text in `rectangle`, left aligned with a small margin, vertically centred.
bool Render_Row_Text(DrawList &draw_list, const ControlVisual &visual, const std::u16string &text, Graphics::Rect2D rectangle,
	const TextStyle &style) noexcept
{
	if (visual.font == nullptr || text.empty())
		return true;
	StaticTextVisual row;
	row.rectangle = rectangle;
	row.centered_vertically = true;
	row.left_margin = 3.0f * visual.scale;
	StaticTextContent content;
	content.font = visual.font;
	content.text = draw_list.Keep_Text(text); // a row's column is built while drawing
	if (content.text == nullptr)
		return false;
	content.style = style;
	content.clip = true;
	content.clip_rectangle = rectangle;
	return Add_Static_Text(draw_list, row, content);
}

// A row's own text colour (GadgetComboBoxAddEntry / GadgetListBoxAddEntryText's colour), if it has one.
TextStyle Row_Style(const ControlVisual &visual, std::size_t entry, TextStyle style) noexcept
{
	if (entry < visual.entry_color_count && visual.entry_colors[entry] != 0) {
		const std::uint32_t rgba = visual.entry_colors[entry];
		style.color = {static_cast<float>(rgba >> 24) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
			static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f};
	}
	return style;
}

// A highlighted row: the list state's segmented highlight (left end, right end, centre), else its colour.
bool Render_Row_Highlight(DrawList &draw_list, const WNDDrawState &state, Graphics::Rect2D rectangle, float scale) noexcept
{
	if (Has_Image(state, 1) && Has_Image(state, 2) && Has_Image(state, 3)) {
		const float left_width = Cell(state, 1).image_width * scale, right_width = Cell(state, 2).image_width * scale;
		return draw_list.Add_Image(Cell(state, 1).image, {rectangle.left, rectangle.top, rectangle.left + left_width, rectangle.bottom})
			&& draw_list.Add_Image(Cell(state, 3).image, {rectangle.left + left_width, rectangle.top, rectangle.right - right_width, rectangle.bottom})
			&& draw_list.Add_Image(Cell(state, 2).image, {rectangle.right - right_width, rectangle.top, rectangle.right, rectangle.bottom});
	}
	return Cell(state, 1).color.alpha <= 0.0f || draw_list.Add_Rect(rectangle, Cell(state, 1).color);
}

bool Render_Combo_Box(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	// The closed box: its edit box, the selected item in it and the drop-down button.
	ControlVisual box = visual;
	if (visual.box_rectangle.right > visual.box_rectangle.left)
		box.rectangle = visual.box_rectangle;
	if (!Render_Text_Entry(draw_list, box))
		return false;
	float button_width = 0.0f;
	if (visual.thumb_state != nullptr) {
		const WNDDrawState &button = *visual.thumb_state;
		button_width = (std::max)(1.0f, Cell(button, 0).image_width * visual.scale);
		const Graphics::Rect2D rectangle{box.rectangle.right - button_width, box.rectangle.top, box.rectangle.right, box.rectangle.bottom};
		if (Has_Image(button, 0)) {
			if (!draw_list.Add_Image(Cell(button, 0).image, rectangle))
				return false;
		}
		else if ((Cell(button, 0).color.alpha > 0.0f && !draw_list.Add_Rect(rectangle, Cell(button, 0).color))
			|| (Cell(button, 0).border_color.alpha > 0.0f && !draw_list.Add_Outline(rectangle, 1.0f, Cell(button, 0).border_color)))
			return false;
	}
	if (visual.entries != nullptr && visual.selected >= 0 && static_cast<std::size_t>(visual.selected) < visual.entry_count
		&& !Render_Row_Text(draw_list, visual, visual.entries[visual.selected],
			{box.rectangle.left, box.rectangle.top, box.rectangle.right - button_width, box.rectangle.bottom},
			Row_Style(visual, static_cast<std::size_t>(visual.selected), visual.text_style)))
		return false;
	// No item chosen: the box's own text (GadgetComboBoxSetText, e.g. a player's name in its slot).
	if ((visual.entries == nullptr || visual.selected < 0 || static_cast<std::size_t>(visual.selected) >= visual.entry_count) && visual.text != nullptr
		&& *visual.text != 0
		&& !Render_Row_Text(draw_list, visual, std::u16string(reinterpret_cast<const char16_t *>(visual.text)),
			{box.rectangle.left, box.rectangle.top, box.rectangle.right - button_width, box.rectangle.bottom}, visual.text_style))
		return false;
	if (!visual.list_open || visual.list_state == nullptr || visual.entries == nullptr)
		return true;
	// The open list: its background (image, else colour) and border, then each shown row.
	const WNDDrawState &list = *visual.list_state;
	const Graphics::Rect2D area = visual.list_rectangle;
	if (Has_Image(list, 0)) {
		if (!draw_list.Add_Image(Cell(list, 0).image, area))
			return false;
	}
	else if (Cell(list, 0).color.alpha > 0.0f && !draw_list.Add_Rect(area, Cell(list, 0).color))
		return false;
	if (Cell(list, 0).border_color.alpha > 0.0f && !draw_list.Add_Outline(area, 1.0f, Cell(list, 0).border_color))
		return false;
	for (std::size_t row = 0; row < visual.list_rows; ++row) {
		const std::size_t entry = static_cast<std::size_t>((std::max)(visual.list_top, 0)) + row;
		if (entry >= visual.entry_count)
			break;
		const float top = area.top + 2.0f * visual.scale + static_cast<float>(row) * visual.row_height;
		const Graphics::Rect2D rectangle{area.left + 2.0f * visual.scale, top, area.right - 2.0f * visual.scale, top + visual.row_height};
		const bool lit = static_cast<int>(entry) == visual.hovered_entry || (visual.hovered_entry < 0 && static_cast<int>(entry) == visual.selected);
		if (lit && !Render_Row_Highlight(draw_list, list, rectangle, visual.scale))
			return false;
		if (!Render_Row_Text(draw_list, visual, visual.entries[entry], rectangle, lit ? visual.highlighted_text_style : Row_Style(visual, entry, visual.text_style)))
			return false;
	}
	return true;
}

bool Render_Tab_Control(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	TabControlVisual tabs;
	tabs.background_image_rectangle = visual.rectangle;
	if (Has_Image(state, 0)) {
		tabs.has_background_image = true;
		tabs.background_image = Cell(state, 0).image;
	}
	else {
		tabs.has_background_fill = Cell(state, 0).color.alpha > 0.0f;
		tabs.background_fill = Cell(state, 0).color;
		tabs.has_background_border = Cell(state, 0).border_color.alpha > 0.0f;
		tabs.background_border = Cell(state, 0).border_color;
	}
	tabs.count = 3;
	const float width = (visual.rectangle.right - visual.rectangle.left) / 3.0f;
	for (std::size_t index = 0; index != tabs.count; ++index) {
		tabs.rectangles[index] = {
			visual.rectangle.left + width * static_cast<float>(index),
			visual.rectangle.top,
			visual.rectangle.left + width * static_cast<float>(index + 1),
			visual.rectangle.bottom};
		const std::size_t cell_index = index + 1;
		if (Has_Image(state, cell_index)) {
			tabs.has_images[index] = true;
			tabs.images[index] = Cell(state, cell_index).image;
		}
		else {
			tabs.has_fills[index] = Cell(state, cell_index).color.alpha > 0.0f;
			tabs.fills[index] = Cell(state, cell_index).color;
			tabs.has_borders[index] = Cell(state, cell_index).border_color.alpha > 0.0f;
			tabs.borders[index] = Cell(state, cell_index).border_color;
		}
	}
	return Add_Tab_Control_Visual(draw_list, tabs);
}

bool Render_Row_Text(DrawList &draw_list, const ControlVisual &visual, const std::u16string &text, Graphics::Rect2D rectangle,
	const TextStyle &style) noexcept;
bool Render_Row_Highlight(DrawList &draw_list, const WNDDrawState &state, Graphics::Rect2D rectangle, float scale) noexcept;

// A list box with its items (W3DGadgetListBoxDraw): its background, then each shown row,
// the selected one highlighted, its columns split by tabs over the column widths.
bool Render_List_Box_Rows(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	const WNDDrawState &state = *visual.state;
	if (!Add_Generic_Background(draw_list, visual))
		return false;
	const Graphics::Rect2D area = visual.list_rectangle;
	float top = area.top;
	for (std::size_t row = 0; row < visual.list_rows; ++row) {
		const std::size_t entry = static_cast<std::size_t>((std::max)(visual.list_top, 0)) + row;
		if (entry >= visual.entry_count)
			break;
		const float height = row < visual.row_heights.size() ? visual.row_heights[row] : visual.row_height;
		const Graphics::Rect2D rectangle{area.left, top, area.right, top + height - visual.scale};
		top += height;
		const bool selected = static_cast<int>(entry) == visual.selected;
		if (selected && !Render_Row_Highlight(draw_list, state, rectangle, visual.scale))
			return false;
		// The row's images, each at its cell's top left in its size and tint.
		if (visual.entry_images != nullptr && entry < visual.entry_images->size()) {
			const auto &cells = (*visual.entry_images)[entry];
			float cellLeft = rectangle.left;
			for (std::size_t column = 0; column < cells.size(); ++column) {
				const float share = column < visual.column_count ? static_cast<float>(visual.column_widths[column]) / 100.0f : 1.0f;
				const float cellWidth = (rectangle.right - rectangle.left) * share;
				const WNDListImage &cell = cells[column];
				if (cell.image.texture.Is_Valid()) {
					const std::uint32_t rgba = cell.rgba;
					const Graphics::Color2D tint{static_cast<float>(rgba >> 24) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
						static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f};
					if (!draw_list.Add_Image(cell.image, {cellLeft, rectangle.top, cellLeft + static_cast<float>(cell.width) * visual.scale,
							rectangle.top + static_cast<float>(cell.height) * visual.scale}, tint))
						return false;
				}
				cellLeft += cellWidth;
			}
		}
		const std::u16string &text = visual.entries[entry];
		// GadgetListBoxAddEntryText's colour: each row's text in its own.
		const TextStyle style = Row_Style(visual, entry, selected ? visual.highlighted_text_style : visual.text_style);
		std::size_t start = 0;
		float left = rectangle.left;
		for (std::size_t column = 0; start <= text.size(); ++column) {
			const std::size_t end = text.find(u'\t', start);
			const float share = column < visual.column_count ? static_cast<float>(visual.column_widths[column]) / 100.0f
				: (column == 0 ? 1.0f : 0.0f);
			const float right = column + 1 >= (std::max<std::size_t>)(visual.column_count, 1) ? rectangle.right
				: left + (rectangle.right - rectangle.left) * share;
			if (!Render_Row_Text(draw_list, visual, text.substr(start, end == std::u16string::npos ? std::u16string::npos : end - start),
					{left, rectangle.top, right, rectangle.bottom}, style))
				return false;
			if (end == std::u16string::npos)
				break;
			start = end + 1;
			left = right;
		}
	}
	return true;
}

// The scroll bar (W3DGadgetPushButtonImageDraw for its buttons and thumb, W3DGadgetVerticalSliderImageDraw
// for its slider: top end, repeated centre, bottom end), else their colours.
bool Render_Scroll_Bar(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	const auto piece = [&](const WNDDrawState *state, Graphics::Rect2D rectangle) {
		if (state == nullptr)
			return true;
		if (Has_Image(*state, 0))
			return draw_list.Add_Image(Cell(*state, 0).image, rectangle);
		if (Cell(*state, 0).color.alpha > 0.0f && !draw_list.Add_Rect(rectangle, Cell(*state, 0).color))
			return false;
		return Cell(*state, 0).border_color.alpha <= 0.0f || draw_list.Add_Outline(rectangle, 1.0f, Cell(*state, 0).border_color);
	};
	const Graphics::Rect2D track = visual.scroll_track_rectangle;
	if (visual.scroll_track != nullptr && Has_Image(*visual.scroll_track, 0) && Has_Image(*visual.scroll_track, 1) && Has_Image(*visual.scroll_track, 2)
		&& Has_Image(*visual.scroll_track, 3)) {
		const WNDDrawState &state = *visual.scroll_track;
		VerticalSliderImageVisual slider;
		slider.top_image = Cell(state, 0).image;
		slider.bottom_image = Cell(state, 1).image;
		slider.center_image = Cell(state, 2).image;
		slider.small_center_image = Cell(state, 3).image;
		const float width = track.right - track.left;
		const float top_height = Cell(state, 0).image_height * visual.scale, bottom_height = Cell(state, 1).image_height * visual.scale;
		const float center_height = Cell(state, 2).image_height * visual.scale;
		if (top_height + bottom_height >= track.bottom - track.top) {
			slider.compact = true;
			const float middle = (track.top + track.bottom) / 2.0f;
			slider.top_rectangle = {track.left, track.top, track.left + width, middle};
			slider.bottom_rectangle = {track.left, middle, track.left + width, track.bottom};
		}
		else {
			const float top_end = track.top + top_height, bottom_start = track.bottom - bottom_height;
			const int pieces = center_height > 0.0f ? static_cast<int>((bottom_start - top_end) / center_height) : 0;
			const float center_end = top_end + static_cast<float>(pieces) * center_height;
			slider.top_rectangle = {track.left, track.top, track.left + width, top_end};
			slider.bottom_rectangle = {track.left, bottom_start, track.left + width, track.bottom};
			slider.center_rectangle = {track.left, top_end, track.left + width, center_end};
			slider.small_center_rectangle = {track.left, center_end, track.left + width, bottom_start};
			slider.center_height = center_height;
			slider.small_center_height = Cell(state, 3).image_height * visual.scale;
		}
		if (!Add_Vertical_Slider_Image_Visual(draw_list, slider))
			return false;
	}
	else if (!piece(visual.scroll_track, track))
		return false;
	return piece(visual.scroll_up, visual.scroll_up_rectangle) && piece(visual.scroll_down, visual.scroll_down_rectangle)
		&& piece(visual.scroll_thumb, visual.scroll_thumb_rectangle);
}

bool Render_List_Box(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	// A list with its items (none yet included: an empty vector's data may be null).
	if (visual.state != nullptr && (visual.entries != nullptr || visual.entry_count == 0))
		return Render_List_Box_Rows(draw_list, visual) && (!visual.has_scroll_bar || Render_Scroll_Bar(draw_list, visual));
	if (visual.state == nullptr)
		return false;
	const WNDDrawState &state = *visual.state;
	ListContext context{visual.rectangle, (std::max)(1u, visual.list_length)};
	ListBoxVisual list;
	list.clip_rectangle = visual.rectangle;
	list.row_count = context.rows;
	list.column_count = (std::max)(1u, visual.list_columns);
	list.context = &context;
	list.query_row = &Query_List_Row;
	list.emit_cell = &Emit_List_Cell;
	list.selection.rectangle = visual.rectangle;
	list.selection.segmented_image = Has_Image(state, 1)
		&& Has_Image(state, 2) && Has_Image(state, 3) && Has_Image(state, 4);
	list.selection.left_image = Cell(state, 1).image;
	list.selection.center_image = Cell(state, 3).image;
	list.selection.small_center_image = Cell(state, 4).image;
	list.selection.right_image = Cell(state, 2).image;
	list.selection.left_width = Cell(state, 1).image_width * visual.scale;
	list.selection.center_width = Cell(state, 3).image_width * visual.scale;
	list.selection.small_center_width = Cell(state, 4).image_width * visual.scale;
	list.selection.right_width = Cell(state, 2).image_width * visual.scale;
	list.selection.has_fill = Cell(state, 0).color.alpha > 0.0f;
	list.selection.fill = Cell(state, 0).color;
	list.selection.has_border = Cell(state, 0).border_color.alpha > 0.0f;
	list.selection.border = Cell(state, 0).border_color;
	return Add_List_Box_Visual(draw_list, list);
}

bool Render_Control_Text(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.font == nullptr || visual.text == nullptr || *visual.text == 0)
		return true;

	StaticTextVisual text_visual;
	text_visual.rectangle = visual.rectangle;
	text_visual.centered = visual.centered_text || visual.kind == ControlKind::PushButton;
	text_visual.centered_vertically = visual.centered_text_vertically;
	if (visual.kind == ControlKind::CheckBox)
		text_visual.left_margin = visual.rectangle.bottom - visual.rectangle.top;

	StaticTextContent content;
	content.font = visual.font;
	content.hotkey_font = visual.hotkey_font;
	content.text = visual.text;
	content.options.parse_hotkey = visual.kind == ControlKind::PushButton;
	if (visual.kind == ControlKind::StaticText) {
		// W3DGadgetStaticTextDraw: the text wraps 10 pixels short of the window's width.
		content.options.wrapping_width = (std::max)(1, static_cast<int>(visual.rectangle.right - visual.rectangle.left - 10.0f));
		content.options.centered = visual.wrap_centered;
	}
	content.style = visual.text_style;
	return Add_Static_Text(draw_list, text_visual, content);
}

}

namespace WNDControlsDetail
{
// Add_WND_Skinny_Border, in screen pixels as the original draws it.
bool Add_Skinny_Border(DrawList &draw_list, const WNDFrame &frame, int x, int y, int width, int height) noexcept
{
	const auto image = [&](const ImageRef &picture, int left, int top, int right, int bottom) {
		return !picture.texture.Is_Valid()
			|| draw_list.Add_Image(picture, {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)});
	};
	const int originalX = x, originalY = y, maximumX = x + width, maximumY = y + height;
	constexpr int Size = 5, HalfSize = Size / 2, Offset = 2, LowerOffset = 5;
	for (x = originalX + 3; x <= maximumX - (LowerOffset + Size); x += Size)
		if (!image(frame.top, x, originalY - Offset, x + Size, originalY - Offset + Size) || !image(frame.bottom, x, maximumY - LowerOffset, x + Size, maximumY - LowerOffset + Size))
			return false;
	int remainder = maximumX - 5;
	if (remainder - x >= HalfSize)
	{
		if (!image(frame.top, x, originalY - Offset, x + HalfSize, originalY - Offset + Size) || !image(frame.bottom, x, maximumY - LowerOffset, x + HalfSize, maximumY - LowerOffset + Size))
			return false;
		x += HalfSize;
	}
	if (x < remainder)
	{
		x -= HalfSize - (((remainder - x) + 1) & ~1);
		if (!image(frame.top, x, originalY - Offset, x + HalfSize, originalY - Offset + Size) || !image(frame.bottom, x, maximumY - LowerOffset, x + HalfSize, maximumY - LowerOffset + Size))
			return false;
	}
	for (y = originalY + 3; y <= maximumY - (LowerOffset + Size); y += Size)
		if (!image(frame.left, originalX - Offset, y, originalX - Offset + Size, y + Size) || !image(frame.right, maximumX - LowerOffset, y, maximumX - LowerOffset + Size, y + Size))
			return false;
	remainder = maximumY - LowerOffset;
	if (remainder - y >= HalfSize)
	{
		if (!image(frame.left, originalX - Offset, y, originalX - Offset + Size, y + HalfSize) || !image(frame.right, maximumX - LowerOffset, y, maximumX - LowerOffset + Size, y + HalfSize))
			return false;
		y += HalfSize;
	}
	if (y < remainder)
	{
		y -= HalfSize - (((remainder - y) + 1) & ~1);
		if (!image(frame.left, originalX - Offset, y, originalX - Offset + Size, y + HalfSize) || !image(frame.right, maximumX - LowerOffset, y, maximumX - LowerOffset + Size, y + HalfSize))
			return false;
	}
	return image(frame.upper_left, originalX - 2, originalY - 2, originalX + 3, originalY + 3) && image(frame.upper_right, maximumX - 5, originalY - 2, maximumX, originalY + 3)
		&& image(frame.lower_left, originalX - 2, maximumY - 5, originalX + 3, maximumY) && image(frame.lower_right, maximumX - 5, maximumY - 5, maximumX, maximumY);
}

// W3DDrawMapPreviewData: black bars beside the picture with a dark line at each edge, the picture
// (else a grey box), its markers, and a thin border round the window.
bool Render_Picture(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	const WNDPicture &picture = *visual.picture;
	const Graphics::Rect2D area = visual.rectangle;
	const auto frame = [&] {
		return Add_Skinny_Border(draw_list, picture.frame, static_cast<int>(area.left) - 1, static_cast<int>(area.top) - 1,
			static_cast<int>(area.right - area.left) + 2, static_cast<int>(area.bottom - area.top) + 2);
	};
	if (picture.extent_width <= 0 || picture.extent_height <= 0)
		return Render_User(draw_list, visual) && frame(); // no map: the window's own look
	const float scaleX = visual.region.right > visual.region.left ? (area.right - area.left) / static_cast<float>(visual.region.right - visual.region.left) : 1.0f;
	const float scaleY = visual.region.bottom > visual.region.top ? (area.bottom - area.top) / static_cast<float>(visual.region.bottom - visual.region.top) : 1.0f;
	const Rect fit = Letterbox(visual.region, picture.extent_width, picture.extent_height);
	const Graphics::Rect2D shown{area.left + static_cast<float>(fit.left - visual.region.left) * scaleX, area.top + static_cast<float>(fit.top - visual.region.top) * scaleY,
		area.left + static_cast<float>(fit.right - visual.region.left) * scaleX, area.top + static_cast<float>(fit.bottom - visual.region.top) * scaleY};
	const Graphics::Color2D black{0.0f, 0.0f, 0.0f, 1.0f}, line{50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f};
	if (shown.top > area.top || shown.bottom < area.bottom)
	{
		if (!draw_list.Add_Rect({area.left, area.top, area.right, shown.top}, black) || !draw_list.Add_Rect({area.left, shown.bottom, area.right, area.bottom}, black)
			|| !draw_list.Add_Line({area.left, shown.top}, {area.right, shown.top}, 1.0f, line)
			|| !draw_list.Add_Line({area.left, shown.bottom + 1.0f}, {area.right, shown.bottom + 1.0f}, 1.0f, line))
			return false;
	}
	else if (shown.left > area.left || shown.right < area.right)
	{
		if (!draw_list.Add_Rect({area.left, area.top, shown.left, area.bottom}, black) || !draw_list.Add_Rect({shown.right, area.top, area.right, area.bottom}, black)
			|| !draw_list.Add_Line({shown.left, area.top}, {shown.left, area.bottom}, 1.0f, line)
			|| !draw_list.Add_Line({shown.right + 1.0f, area.top}, {shown.right + 1.0f, area.bottom}, 1.0f, line))
			return false;
	}
	if (picture.image.texture.Is_Valid() ? !draw_list.Add_Image(picture.image, shown) : !draw_list.Add_Rect(shown, line))
		return false;
	for (const WNDPictureMarker &marker : picture.markers)
	{
		if (!marker.image.texture.Is_Valid())
			continue;
		const float x = shown.left + (shown.right - shown.left) * static_cast<float>(marker.x) / 10000.0f;
		const float y = shown.top + (shown.bottom - shown.top) * static_cast<float>(marker.y) / 10000.0f;
		const float half = static_cast<float>(marker.size) * scaleX / 2.0f;
		if (!draw_list.Add_Image(marker.image, {x - half, y - half, x + half, y + half}))
			return false;
	}
	return frame();
}
}

export bool Render_Control(DrawList &draw_list, const ControlVisual &visual) noexcept
{
	if (visual.picture != nullptr)
		return WNDControlsDetail::Render_Picture(draw_list, visual);
	bool rendered = false;
	switch (visual.kind) {
	case ControlKind::PushButton: rendered = WNDControlsDetail::Render_Push_Button(draw_list, visual); break;
	case ControlKind::CheckBox: rendered = WNDControlsDetail::Render_Check_Box(draw_list, visual); break;
	case ControlKind::RadioButton: rendered = WNDControlsDetail::Render_Radio_Button(draw_list, visual); break;
	case ControlKind::ListBox: rendered = WNDControlsDetail::Render_List_Box(draw_list, visual); break;
	case ControlKind::ProgressBar: rendered = WNDControlsDetail::Render_Progress_Bar(draw_list, visual); break;
	case ControlKind::HorizontalSlider: rendered = WNDControlsDetail::Render_Slider(draw_list, visual, false); break;
	case ControlKind::VerticalSlider: rendered = WNDControlsDetail::Render_Slider(draw_list, visual, true); break;
	case ControlKind::TextEntry: rendered = WNDControlsDetail::Render_Text_Entry(draw_list, visual); break;
	case ControlKind::ComboBox: rendered = WNDControlsDetail::Render_Combo_Box(draw_list, visual); break;
	case ControlKind::TabControl: rendered = WNDControlsDetail::Render_Tab_Control(draw_list, visual); break;
	case ControlKind::StaticText:
	case ControlKind::User:
	case ControlKind::Unknown:
	default: rendered = WNDControlsDetail::Render_User(draw_list, visual); break;
	}
	// Combo boxes draw their own text (the selected item); text entries theirs, left aligned, with the caret.
	if (!rendered)
		return false;
	if (visual.video != nullptr && !draw_list.Add_Image(*visual.video, visual.rectangle))
		return false;
	if (visual.kind == ControlKind::ComboBox)
		return true;
	if (visual.kind == ControlKind::TextEntry) {
		const std::u16string text = visual.text != nullptr ? std::u16string(reinterpret_cast<const char16_t *>(visual.text)) : std::u16string{};
		if (!WNDControlsDetail::Render_Row_Text(draw_list, visual, text, visual.rectangle, visual.text_style))
			return false;
		if (!visual.caret || visual.font == nullptr)
			return true;
		// The caret after the text (the original's W3DGadgetTextEntryDraw cursor), in the text colour.
		float width = 0.0f;
		for (const char16_t character : text)
			width += static_cast<float>(visual.font->Get_Char_Spacing(static_cast<std::uint16_t>(character)));
		const float left = visual.rectangle.left + 3.0f * visual.scale + width + 1.0f;
		const float height = static_cast<float>(visual.font->Height());
		const float top = visual.rectangle.top + (visual.rectangle.bottom - visual.rectangle.top - height) * 0.5f;
		return draw_list.Add_Rect({left, top, left + (std::max)(1.0f, visual.scale), top + height}, visual.text_style.color);
	}
	return WNDControlsDetail::Render_Control_Text(draw_list, visual);
}

}
