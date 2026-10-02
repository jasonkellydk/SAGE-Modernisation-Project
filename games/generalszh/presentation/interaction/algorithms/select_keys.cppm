export module games.generalszh.presentation.interaction.algorithms.select_keys;
import std;

export import engine.ecs.core.entity;

// CommandXlat's selection keys over TheGameClient's drawable list (each new drawable at its front: newest first):
// SELECT_ALL (Q) and SELECT_ALL_AIRCRAFT (W) through InGameUI::selectAllUnitsByType, SELECT_MATCHING_UNITS (E) through
// selectUnitsMatchingCurrentSelection (each across the screen first, then across the map when that took nothing), and
// SELECT_NEXT_UNIT / SELECT_PREV_UNIT / SELECT_NEXT_WORKER / SELECT_PREV_WORKER (Ctrl+arrows) walking the list from the
// first selected drawable, with the original's wrap-around as written. InGameUI's MaxSelectionSize (when above 0) caps
// what region selects take, saying GUI:MaxSelectionSize once.
export namespace generalszh::presentation
{
// One drawable of the list, as the keys ask about its object (none: a drawable without one).
struct KeyDrawable
{
	ecs::Entity entity;
	bool hasObject{true};
	std::uint32_t equivalence{0}; // ThingTemplate::isEquivalentTo: same final template or reskin family
	bool local{false};            // isLocallyControlled
	bool contained{false};        // isContained
	bool mobile{false};           // isMobile: not IMMOBILE, not disabled
	bool selected{false};         // Drawable::isSelected
	bool dead{false};             // isEffectivelyDead
	bool selectable{false};       // isSelectable
	bool offMap{false};           // isOffMap
	bool onScreen{false};         // its centre projects inside the view
	bool carBomb{false};          // OBJECT_STATUS_IS_CARBOMB
	bool structure{false}, aircraft{false}, dozer{false}, harvester{false}, ignoresSelectAll{false}, noSelect{false};
	std::array<float, 3> position{};

	bool MassSelectable() const noexcept { return selectable && !structure; }
};

struct KeyMessage
{
	std::string label;
	int number{-1}; // GUI:MaxSelectionSize's %d
	bool operator==(const KeyMessage &) const = default;
};

// What a key does: drop the whole selection first, select these (in order), look at a place, say these.
struct KeySelection
{
	bool deselectAll{false};
	std::vector<ecs::Entity> select;
	std::optional<std::array<float, 3>> lookAt;
	std::vector<KeyMessage> messages;
	bool createGroup{false}; // MSG_CREATE_SELECTED_GROUP (with its select voice); the matching key's is _NO_SOUND
	bool unitVoice{false};   // the selected one's own VoiceSelect too (SELECT_NEXT_WORKER from nothing selected)
};

namespace select_keys_detail
{
struct Region
{
	const std::vector<KeyDrawable> &list;
	std::vector<bool> selected; // as the walk selects
	int maxSelect{0};
	int count{0};               // getSelectCount
	bool warned{false};         // m_displayedMaxWarning
	KeySelection &out;

	// The cap or the selection (kindOfUnitSelection / similarUnitSelection's tail).
	bool Take(std::size_t index)
	{
		if (maxSelect > 0 && count >= maxSelect)
		{
			if (!warned)
			{
				warned = true;
				out.messages.push_back({"GUI:MaxSelectionSize", maxSelect});
			}
			return false;
		}
		selected[index] = true;
		++count;
		warned = false;
		out.select.push_back(list[index].entity);
		return true;
	}
};

// selectAllUnitsByTypeAcrossRegion: (onScreen) each drawable whose object has all of the required kinds and none of the
// clearing ones, is the player's, not contained, not selected, alive, mass selectable and on the map.
inline int ByTypeAcross(Region &region, bool screen, bool aircraft)
{
	int taken = 0;
	for (std::size_t index = 0; index < region.list.size(); ++index)
	{
		const KeyDrawable &d = region.list[index];
		if (!d.hasObject || (screen && !d.onScreen))
			continue;
		const bool kinds = (!aircraft || d.aircraft) && !d.dozer && !d.harvester && !d.ignoresSelectAll;
		if (kinds && d.local && !d.contained && !region.selected[index] && !d.dead && d.MassSelectable() && !d.offMap && region.Take(index))
			++taken;
	}
	region.warned = false;
	return taken;
}

// selectMatchingAcrossRegion: -1 when nothing of the player's is selected; else each equivalence of the player's
// selected objects in turn (a std::set of templates: here by equivalence value), every drawable of it (or any car bomb
// when a car bomb is selected) that is the player's, not contained, not selected, mass selectable and on the map.
inline int MatchingAcross(Region &region, bool screen)
{
	std::set<std::uint32_t> kinds;
	bool carBomb = false;
	for (std::size_t index = 0; index < region.list.size(); ++index)
	{
		const KeyDrawable &d = region.list[index];
		if (region.selected[index] && d.hasObject && d.local)
		{
			kinds.insert(d.equivalence);
			carBomb = carBomb || d.carBomb;
		}
	}
	if (kinds.empty())
		return -1;
	int taken = 0;
	for (const std::uint32_t kind : kinds)
	{
		for (std::size_t index = 0; index < region.list.size(); ++index)
		{
			const KeyDrawable &d = region.list[index];
			if (!d.hasObject || (screen && !d.onScreen))
				continue;
			const bool equivalent = d.equivalence == kind || (carBomb && d.carBomb);
			if (equivalent && d.local && !d.contained && !region.selected[index] && d.MassSelectable() && !d.offMap && region.Take(index))
				++taken;
		}
		region.warned = false;
	}
	return taken;
}

inline const KeyDrawable *FirstSelected(const std::vector<KeyDrawable> &list, ecs::Entity first)
{
	for (const KeyDrawable &d : list)
		if (d.entity == first)
			return &d;
	return nullptr;
}
}

// SelectionTranslator::selectFriends' cap on a click or drag selection: of `adding` (in the drawable list's order), each
// is taken while the selection (`already` plus those taken) is under `maxSelect` (when above 0); the first refused one
// says GUI:MaxSelectionSize, unless the translator said it before (`displayedWarning`, m_displayedMaxWarning: set,
// never cleared, so once a game). True in `warn` when it is to be said now.
struct CapResult
{
	std::vector<ecs::Entity> taken;
	bool warn{false};
};

inline CapResult CapSelection(std::span<const ecs::Entity> adding, std::size_t already, int maxSelect, bool &displayedWarning)
{
	CapResult out;
	std::size_t count = already;
	for (const ecs::Entity entity : adding)
	{
		if (maxSelect > 0 && count >= static_cast<std::size_t>(maxSelect))
		{
			if (!displayedWarning)
			{
				displayedWarning = true;
				out.warn = true;
			}
			continue;
		}
		out.taken.push_back(entity);
		++count;
	}
	return out;
}

// MSG_META_SELECT_ALL / SELECT_ALL_AIRCRAFT. `list`: the drawables, newest first; `firstSelected`: the drawable
// selected last (getFirstSelectedDrawable).
inline KeySelection SelectAllKey(const std::vector<KeyDrawable> &list, bool aircraft, int maxSelect, ecs::Entity firstSelected)
{
	using namespace select_keys_detail;
	KeySelection out;
	std::vector<bool> selected(list.size());
	// Patch 1.03 (the SCUDSTORM exploit): a selected drawable the key may not take drops the whole selection.
	for (std::size_t index = 0; index < list.size(); ++index)
	{
		const KeyDrawable &d = list[index];
		if (!d.selected)
			continue;
		const bool disqualified = d.dozer || d.harvester || d.ignoresSelectAll;
		if (aircraft ? (disqualified || !d.aircraft) : (disqualified || d.structure))
		{
			out.deselectAll = true;
			break;
		}
	}
	int count = 0;
	for (std::size_t index = 0; index < list.size(); ++index)
		if (list[index].selected && !out.deselectAll)
		{
			selected[index] = true;
			++count;
		}
	Region region{list, selected, maxSelect, count, false, out};
	const int screen = ByTypeAcross(region, true, aircraft);
	out.createGroup = screen > 0;
	if (screen > 0)
	{
		out.messages.push_back({"GUI:SelectedAcrossScreen"});
		return out;
	}
	const int map = ByTypeAcross(region, false, aircraft);
	out.createGroup = map > 0;
	if (map == 0)
	{
		// selectAllUnitsByTypeAcrossMap: nothing new, but the first selected is not a structure: still said.
		const KeyDrawable *first = out.deselectAll ? nullptr : FirstSelected(list, firstSelected);
		if (first == nullptr || !first->hasObject || !first->structure)
			out.messages.push_back({"GUI:SelectedAcrossMap"});
	}
	else
		out.messages.push_back({"GUI:SelectedAcrossMap"});
	return out;
}

// MSG_META_SELECT_MATCHING_UNITS.
inline KeySelection SelectMatchingKey(const std::vector<KeyDrawable> &list, int maxSelect, ecs::Entity firstSelected)
{
	using namespace select_keys_detail;
	KeySelection out;
	std::vector<bool> selected(list.size());
	int count = 0;
	for (std::size_t index = 0; index < list.size(); ++index)
		if (list[index].selected)
		{
			selected[index] = true;
			++count;
		}
	Region region{list, selected, maxSelect, count, false, out};
	const int screen = MatchingAcross(region, true);
	if (screen == -1)
	{
		out.messages.push_back({"GUI:NothingSelected"});
		return out;
	}
	if (screen > 0)
	{
		out.messages.push_back({"GUI:SelectedAcrossScreen"});
		return out;
	}
	const int map = MatchingAcross(region, false);
	if (map == -1)
		out.messages.push_back({"GUI:NothingSelected"});
	else if (map == 0)
	{
		const KeyDrawable *first = FirstSelected(list, firstSelected);
		if (first == nullptr || !first->hasObject || !first->structure)
			out.messages.push_back({"GUI:SelectedAcrossMap"});
	}
	else
		out.messages.push_back({"GUI:SelectedAcrossMap"});
	return out;
}

enum class StepKey : std::uint8_t
{
	NextUnit,
	PrevUnit,
	NextWorker,
	PrevWorker,
};

// MSG_META_SELECT_NEXT/PREV_UNIT and _WORKER, walking the list (index 0 the first drawable; getNextDrawable +1,
// getPrevDrawable -1) exactly as written, its uneven tests and wrap-arounds included. `selectCount`: getSelectCount.
inline KeySelection StepSelectKey(const std::vector<KeyDrawable> &list, StepKey key, std::size_t selectCount, ecs::Entity firstSelected)
{
	KeySelection out;
	const int n = static_cast<int>(list.size());
	if (n == 0)
		return out;
	constexpr int none = -1;
	const auto next = [n](int at) { return at < 0 || at + 1 >= n ? none : at + 1; };
	const auto prev = [](int at) { return at <= 0 ? none : at - 1; };
	const auto take = [&](int at, bool alone) {
		if (alone)
			out.deselectAll = true;
		out.select.push_back(list[static_cast<std::size_t>(at)].entity);
		out.lookAt = list[static_cast<std::size_t>(at)].position;
		out.createGroup = true;
	};
	const auto d = [&](int at) -> const KeyDrawable & { return list[static_cast<std::size_t>(at)]; };
	const bool unit = key == StepKey::NextUnit || key == StepKey::PrevUnit;
	const bool forward = key == StepKey::PrevUnit || key == StepKey::PrevWorker; // "list is prepended": prev walks forwards

	if (selectCount == 0)
	{
		// Next: from the last drawable back; prev: the first drawable only (getPrevDrawable of the first is none).
		for (int at = forward ? 0 : n - 1; at != none; at = prev(at))
		{
			const KeyDrawable &x = d(at);
			if (!x.hasObject)
				break;
			bool fits = false;
			switch (key)
			{
			case StepKey::NextUnit:
			case StepKey::PrevUnit: fits = x.mobile && x.local && !x.contained && !x.noSelect; break;
			case StepKey::NextWorker: fits = x.local && !x.contained && x.dozer; break;
			case StepKey::PrevWorker: fits = x.mobile && x.local && !x.contained && x.dozer; break;
			}
			if (fits)
			{
				take(at, false);
				// SELECT_NEXT_WORKER alone also plays the worker's VoiceSelect at it (AudioEventRTS with its object).
				out.unitVoice = key == StepKey::NextWorker;
				break;
			}
		}
		return out;
	}

	int selectedAt = none;
	for (int at = 0; at < n; ++at)
		if (d(at).entity == firstSelected)
			selectedAt = at;
	if (selectedAt == none || !d(selectedAt).hasObject || !d(selectedAt).local)
		return out;
	int found = none;
	bool hack = false;
	if (!forward)
	{
		// NEXT: backwards from the one before the selected; off the front, from the last again (the hack re-steps once).
		for (int at = prev(selectedAt); at != selectedAt;)
		{
			if (hack)
			{
				if (at == none)
					break; // getNextDrawable of nothing: the original would fault here
				at = next(at);
				hack = false;
			}
			if (at == none)
			{
				at = n - 1;
				hack = true;
			}
			else
			{
				const KeyDrawable &x = d(at);
				const bool fits = unit ? (x.hasObject && x.mobile && x.local && !x.contained && !x.noSelect)
									   : (x.hasObject && x.local && !x.contained && x.dozer);
				if (fits)
				{
					found = at;
					break;
				}
			}
			at = prev(at);
		}
	}
	else
	{
		// PREV: forwards from the one after the selected; off the end, from the first again.
		for (int at = next(selectedAt); at != selectedAt;)
		{
			if (hack)
			{
				at = 0;
				hack = false;
				if (at == selectedAt)
					break;
			}
			if (at == none)
			{
				at = 0;
				hack = true;
				const KeyDrawable &x = d(at);
				// The wrap's own test (the loop would end before taking the first drawable).
				if (x.hasObject && next(at) == selectedAt && !x.selected && x.mobile && x.local && !x.contained && (!unit || !x.noSelect))
				{
					found = at;
					break;
				}
			}
			else
			{
				const KeyDrawable &x = d(at);
				const bool fits = x.hasObject && !x.selected && x.mobile && x.local && !x.contained && (unit || x.dozer);
				if (fits)
				{
					found = at;
					break;
				}
			}
			at = next(at);
		}
	}
	if (found != none)
		take(found, true);
	return out;
}
}
