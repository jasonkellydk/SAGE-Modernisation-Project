module;
#include <vector>
export module Graphics.Scene.Particles.EmitterTracks;

namespace Graphics {

// Owns prepared keyframe values independently of the decoded source. The
// projection supplies the runtime value type without coupling assets to it.
export template<class T> struct EmitterTrackValues final {
    T start;
    T random;
    std::vector<float> times;
    std::vector<T> values;

    template<class Track, class Convert>
    EmitterTrackValues(const Track& track, Convert convert)
        : start(convert(track.start)), random(convert(track.random))
    {
        times.reserve(track.keys.size());
        values.reserve(track.keys.size());
        for (const auto& key : track.keys) {
            times.push_back(key.time);
            values.push_back(convert(key.value));
        }
    }
};
}
