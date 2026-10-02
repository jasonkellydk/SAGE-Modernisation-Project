export module games.generalszh.presentation.objects.algorithms.tint_envelope;
import std;

export import games.generalszh.presentation.objects.components.object_presentation;

// The original's TintEnvelope (GeneralsMD/Code/GameEngine/Source/GameClient/Drawable.cpp): a colour added over a
// drawable that rises to a peak over its attack frames, holds there for its sustain frames, and falls back to none over
// its decay frames (by the peak's share a frame, whatever it had reached). Stepped each drawn frame by the logic frames
// that frame covers (TheFramePacer's logic time scale over the frame rate, at most 1).
export namespace generalszh::presentation
{
namespace tint_envelope_detail
{
inline float Length(const std::array<float, 3> &v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
}

// TintEnvelope::play.
inline void PlayTint(TintEnvelope &envelope, const std::array<float, 3> &peak, std::uint32_t attackFrames, std::uint32_t decayFrames, float sustainFrames)
{
	envelope.peak = peak;
	const float attack = 1.0f / static_cast<float>(std::max<std::uint32_t>(1, attackFrames));
	const float decay = -1.0f / static_cast<float>(std::max<std::uint32_t>(1, decayFrames));
	std::array<float, 3> delta{};
	for (std::size_t channel = 0; channel < 3; ++channel)
	{
		envelope.attackRate[channel] = (peak[channel] - envelope.current[channel]) * attack;
		envelope.decayRate[channel] = peak[channel] * decay;
		delta[channel] = envelope.current[channel] - peak[channel];
	}
	envelope.state = TintEnvelope::Attack;
	envelope.sustain = sustainFrames;
	envelope.affect = 1;
	if (tint_envelope_detail::Length(delta) <= 0.001f) // practically there already
		envelope.state = TintEnvelope::Sustain;
}

// TintEnvelope::release: back to none from wherever it is.
inline void ReleaseTint(TintEnvelope &envelope) { envelope.state = TintEnvelope::Decay; }

// TintEnvelope::update, `timeScale` logic frames on.
inline void StepTint(TintEnvelope &envelope, float timeScale)
{
	using tint_envelope_detail::Length;
	switch (envelope.state)
	{
	case TintEnvelope::Rest:
		envelope.current = {};
		envelope.affect = 0;
		break;
	case TintEnvelope::Decay: {
		const std::array<float, 3> rate{envelope.decayRate[0] * timeScale, envelope.decayRate[1] * timeScale, envelope.decayRate[2] * timeScale};
		if (Length(rate) > Length(envelope.current) || Length(envelope.current) <= 0.001f)
		{
			envelope.state = TintEnvelope::Rest;
			envelope.affect = 0;
		}
		else
		{
			for (std::size_t channel = 0; channel < 3; ++channel)
				envelope.current[channel] += rate[channel];
			envelope.affect = 1;
		}
		break;
	}
	case TintEnvelope::Attack: {
		const std::array<float, 3> rate{envelope.attackRate[0] * timeScale, envelope.attackRate[1] * timeScale, envelope.attackRate[2] * timeScale};
		const std::array<float, 3> delta{envelope.current[0] - envelope.peak[0], envelope.current[1] - envelope.peak[1], envelope.current[2] - envelope.peak[2]};
		if (Length(rate) > Length(delta) || Length(delta) <= 0.001f)
			envelope.state = envelope.sustain != 0.0f ? TintEnvelope::Sustain : TintEnvelope::Decay;
		else
		{
			for (std::size_t channel = 0; channel < 3; ++channel)
				envelope.current[channel] += rate[channel];
			envelope.affect = 1;
		}
		break;
	}
	case TintEnvelope::Sustain:
		if (envelope.sustain > 0.0f)
			envelope.sustain -= timeScale;
		else
			ReleaseTint(envelope);
		break;
	default:
		break;
	}
}
}
