export module Graphics.Scene.Tracks.Geometry;
import std;
export import Graphics.Scene.Surfaces.Geometry;
namespace Graphics
{
// A track's edges oldest first, as columns: each edge's two end points across the track, its texture row (u runs 0 to 1
// from the left end to the right) and its opacity.
export struct TrackEdges final
{
    std::span<const std::array<float,3>> left;
    std::span<const std::array<float,3>> right;
    std::span<const float> v;
    std::span<const float> alpha;

    std::size_t size() const noexcept { return left.size(); }
};

export class TrackGeometry final
{
public:
    std::vector<SurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;

    void Build(const TrackEdges &edges, int maximum_edges, int opaque_edges,
        const std::array<float,3> &color)
    {
        vertices.clear();
        indices.clear();
        if (edges.size() < 2) return;
        vertices.reserve(edges.size()*2);
        indices.reserve((edges.size()-1)*6);
        for (std::size_t i=0; i<edges.size(); ++i) {
            float fade = 1;
            if (int(edges.size()-1-i) >= opaque_edges && maximum_edges > opaque_edges)
                fade = 1 - float(int(edges.size()-i)-opaque_edges) / float(maximum_edges-opaque_edges);
            const float alpha = float(int(std::clamp(fade * edges.alpha[i],0.0f,1.0f)*255)) / 255;
            vertices.push_back({edges.left[i], {color[0],color[1],color[2],alpha}, {0.0f,edges.v[i]}});
            vertices.push_back({edges.right[i], {color[0],color[1],color[2],alpha}, {1.0f,edges.v[i]}});
            if (i > 0) {
                const auto a = static_cast<std::uint32_t>((i-1)*2);
                const std::array<std::uint32_t,6> segment{a,a+1,a+3,a,a+3,a+2};
                indices.insert(indices.end(),segment.begin(),segment.end());
            }
        }
    }
};
}
