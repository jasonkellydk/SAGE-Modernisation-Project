export module games.generalszh.presentation.hud.algorithms.eva_queue;
import std;

export import games.generalszh.presentation.hud.resources.eva_state;

// Eva::update, once a frame of the logic: nothing while off or before frame 2 (what was asked waits). Each
// announcement not already under way that should play (low power: while the watcher lacks power; the rest: asked
// since the last update) starts a check, if Eva.ini has it. Then the checks: one heard is dropped once its next check
// is due (a frame early); one not heard is dropped once it expires. While the voice is busy nothing more is said;
// else the highest priority check not yet heard (the first of equals) speaks its side's line (one of its Sounds at
// random; none for another side), and may not be heard again until its time between checks has passed. What was
// asked is forgotten.
export namespace generalszh::presentation
{
// `pick(count)` chooses among a side's lines (GameClientRandomValue(0, count - 1)); none when the watcher has none.
// Returns the line to speak (empty: silence, the check still counts as heard), if one speaks this frame.
inline std::optional<std::string> UpdateEva(EvaState &eva, std::uint64_t frame, bool watching, bool lowPower, bool speaking,
	const std::function<std::size_t(std::size_t)> &pick)
{
	if (!eva.enabled || frame < 2)
		return std::nullopt;
	for (std::uint32_t message = 0; message < eva.shouldPlay.size(); ++message)
	{
		// isTimeForCheck: not while a check of it is under way.
		if (std::any_of(eva.checks.begin(), eva.checks.end(), [&](const EvaCheck &check) { return check.message == message; }))
			continue;
		if (!watching)
			continue;
		// shouldPlayLowPower / shouldPlayGenericHandler (which takes the ask).
		bool play = false;
		if (message == 0)
			play = lowPower;
		else if (eva.shouldPlay[message])
		{
			eva.shouldPlay[message] = false;
			play = true;
		}
		// playMessage: a check, if Eva.ini has the announcement.
		if (play)
			if (const content::EvaCheckInfo *info = eva.catalog.Of(message))
				eva.checks.push_back({message, frame, frame + info->framesBetweenChecks, false});
	}
	std::optional<std::string> spoken;
	// processPlayingMessages.
	std::erase_if(eva.checks, [&](const EvaCheck &check) {
		const content::EvaCheckInfo *info = eva.catalog.Of(check.message);
		if (check.nextCheck <= frame + 1 && check.played)
			return true;
		return !check.played && check.triggeredOn + info->framesToExpire <= frame;
	});
	if (!eva.checks.empty() && !speaking)
	{
		EvaCheck *best = nullptr;
		std::uint32_t highest = 0;
		for (EvaCheck &check : eva.checks)
			if (const std::uint32_t priority = eva.catalog.Of(check.message)->priority; priority > highest && !check.played)
			{
				best = &check;
				highest = priority;
			}
		if (best != nullptr)
		{
			const content::EvaCheckInfo &info = *eva.catalog.Of(best->message);
			std::string line;
			for (const content::EvaSideSounds &side : info.sideSounds)
			{
				const bool same = side.side.size() == eva.side.size() &&
					std::equal(side.side.begin(), side.side.end(), eva.side.begin(),
						[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
				if (!same)
					continue;
				if (!side.sounds.empty())
					line = side.sounds[pick(side.sounds.size())];
				break;
			}
			best->played = true;
			best->nextCheck = frame + info.framesBetweenChecks;
			spoken = std::move(line);
		}
	}
	eva.shouldPlay.fill(false);
	return spoken;
}

// Eva::setShouldPlay.
inline void AskEva(EvaState &eva, std::uint32_t message)
{
	if (message < eva.shouldPlay.size())
		eva.shouldPlay[message] = true;
}

// Eva::setEvaEnabled: what was asked is forgotten either way.
inline void SetEvaEnabled(EvaState &eva, bool enabled)
{
	if (eva.enabled == enabled)
		return;
	eva.shouldPlay.fill(false);
	eva.enabled = enabled;
}
}
