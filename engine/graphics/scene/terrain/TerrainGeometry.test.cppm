module;
#define BOOST_TEST_MODULE TerrainGeometryTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <limits>
#include <algorithm>
#include <vector>
#include <span>
export module Graphics.Scene.Terrain.Geometry.Tests;
import Graphics.Scene.Terrain.Geometry;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(partial_edits_match_full_rebuild_across_visibility_batch_boundaries)
{
    std::vector<TerrainCell> cells(600);
    for (std::size_t i=0; i<cells.size(); ++i) {
        cells[i].origin = {static_cast<float>(i%30),static_cast<float>(i/30)};
        cells[i].heights.fill(3);
    }
    TerrainGeometry partial, full;
    BOOST_REQUIRE(partial.Build(cells,1));
    for (unsigned edit=0; edit<3; ++edit) {
        const std::size_t first = edit==0 ? 254 : edit==1 ? 511 : 599;
        const std::size_t count = std::min<std::size_t>(4,cells.size()-first);
        for (std::size_t i=first; i<first+count; ++i) {
            cells[i].heights.fill(edit==1 ? -8.f : 10.f);
            cells[i].alternate_diagonal = true;
            cells[i].colors[0] = {.25f,.5f,.75f,1};
            cells[i].normals[2] = {0,1,0};
        }
        BOOST_REQUIRE(partial.Update(first,std::span(cells).subspan(first,count)));
        BOOST_REQUIRE(full.Build(cells,1));
        BOOST_CHECK(std::equal(partial.Vertices().begin(),partial.Vertices().end(),full.Vertices().begin(),
            [](const auto& a,const auto& b) {
                return a.position==b.position && a.normal==b.normal && a.color==b.color
                    && a.base_uv==b.base_uv && a.blend_uv==b.blend_uv;
            }));
        BOOST_CHECK_EQUAL_COLLECTIONS(partial.Indices().begin(),partial.Indices().end(),full.Indices().begin(),full.Indices().end());
        BOOST_CHECK(std::equal(partial.Batches().begin(),partial.Batches().end(),full.Batches().begin(),
            [](const auto& a,const auto& b) {
                return a.minimum==b.minimum && a.maximum==b.maximum
                    && a.first_index==b.first_index && a.index_count==b.index_count;
            }));
    }
    auto invalid = cells.back();
    invalid.heights[0] = std::numeric_limits<float>::quiet_NaN();
    BOOST_CHECK(!partial.Update(599,std::span(&invalid,1)));
    BOOST_CHECK(!partial.Update(600,std::span(cells).first(1)));
    BOOST_CHECK_EQUAL(partial.Vertices()[599*4].position[2],10.f);
}

BOOST_AUTO_TEST_CASE(worker_counts_preserve_topology_attributes_and_visibility_batches)
{
    std::vector<TerrainCell> cells(70001);
    for (std::size_t index=0;index<cells.size();++index) {
        auto& cell = cells[index];
        cell.origin = {static_cast<float>(index%257),static_cast<float>(index/257)};
        cell.alternate_diagonal = index%2 != 0;
        cell.heights = {0,static_cast<float>(index%13),1,2};
        cell.base_uv[0] = {0.125f,static_cast<float>(index%7)};
        cell.blend_uv[3] = {0.75f,0.5f};
        cell.colors[2] = {1,0.25f,0.5f,0.75f};
    }
    TerrainGeometry serial, parallel;
    BOOST_REQUIRE(serial.Build(cells,1));
    BOOST_REQUIRE(parallel.Build(cells,8));
    BOOST_REQUIRE_EQUAL(serial.Vertices().size(),parallel.Vertices().size());
    BOOST_CHECK(std::equal(serial.Vertices().begin(),serial.Vertices().end(),parallel.Vertices().begin(),
        [](const auto& a,const auto& b) {
            return a.position==b.position && a.normal==b.normal && a.color==b.color
                && a.base_uv==b.base_uv && a.blend_uv==b.blend_uv;
        }));
    BOOST_CHECK_EQUAL_COLLECTIONS(serial.Indices().begin(),serial.Indices().end(),
        parallel.Indices().begin(),parallel.Indices().end());
    BOOST_REQUIRE_EQUAL(serial.Batches().size(),parallel.Batches().size());
    BOOST_CHECK(std::equal(serial.Batches().begin(),serial.Batches().end(),parallel.Batches().begin(),
        [](const auto& a,const auto& b) {
            return a.minimum==b.minimum && a.maximum==b.maximum
                && a.first_index==b.first_index && a.index_count==b.index_count;
        }));
    // Include the final partial batch and a failed reload after worker use.
    BOOST_CHECK_EQUAL(parallel.Batches().back().index_count,(cells.size()%256)*6);
    cells.back().heights[3] = std::numeric_limits<float>::quiet_NaN();
    BOOST_CHECK(!parallel.Build(cells,8));
    BOOST_CHECK_EQUAL(parallel.Indices().size(),serial.Indices().size());
}

BOOST_AUTO_TEST_CASE(cell_topology_preserves_heights_and_discontinuous_uvs)
{
    std::array<TerrainCell, 2> cells{};
    cells[0].origin = {-10, 20};
    cells[0].spacing = {10, 10};
    cells[0].heights = {1, 2, 3, 4};
    cells[0].base_uv[1] = {0.25f, 0.75f};
    cells[0].blend_uv[1] = {0.5f, 1.0f};
    cells[0].colors[1] = {1, 0, 0, 0.5f};
    cells[1].alternate_diagonal = true;
    TerrainGeometry geometry;
    BOOST_REQUIRE(geometry.Build(cells));
    BOOST_REQUIRE_EQUAL(geometry.Vertices().size(), 8);
    BOOST_REQUIRE_EQUAL(geometry.Indices().size(), 12);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].position[0], 0);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].position[1], 20);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].position[2], 2);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].base_uv[0], 0.25f);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].blend_uv[0], 0.5f);
    BOOST_CHECK_EQUAL(geometry.Vertices()[1].color[3], 0.5f);
    const std::array<unsigned, 12> expected{0, 2, 3, 0, 1, 2, 5, 7, 4, 5, 6, 7};
    BOOST_CHECK_EQUAL_COLLECTIONS(geometry.Indices().begin(), geometry.Indices().end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(invalid_edit_preserves_previous_geometry_and_empty_edit_clears_it)
{
    TerrainGeometry geometry;
    std::array<TerrainCell, 1> cells{};
    BOOST_REQUIRE(geometry.Build(cells));
    cells[0].heights[2] = std::numeric_limits<float>::quiet_NaN();
    BOOST_CHECK(!geometry.Build(cells));
    BOOST_REQUIRE_EQUAL(geometry.Vertices().size(), 4);
    BOOST_CHECK_EQUAL(geometry.Vertices()[2].position[2], 0);
    cells[0].heights[2] = 0;
    cells[0].spacing[0] = 0;
    BOOST_CHECK(!geometry.Build(cells));
    BOOST_REQUIRE(geometry.Build({}));
    BOOST_CHECK(geometry.Vertices().empty());
    BOOST_CHECK(geometry.Indices().empty());
}

BOOST_AUTO_TEST_CASE(displacement_refines_triangles_and_updates_geometry_bounds)
{
    TerrainCell source; source.spacing={4,4}; source.heights={0,2,7,1};
    source.base_uv={{{0,0},{1,0},{1,1},{0,1}}};
    for (bool alternate : {false,true}) {
        source.alternate_diagonal=alternate;
        std::array<TerrainCell,16> flat,displaced;
        BOOST_REQUIRE(Subdivide_Terrain_Cell(source,4,flat,[](float,float){return 0.f;}));
        BOOST_REQUIRE(Subdivide_Terrain_Cell(source,4,displaced,[](float x,float y){return -.5f*x*y;}));
        for (unsigned i=0;i<16;++i) for (unsigned k=0;k<4;++k) {
            const float x=displaced[i].base_uv[k][0]*4, y=displaced[i].base_uv[k][1]*4;
            BOOST_CHECK_SMALL(displaced[i].heights[k]-flat[i].heights[k]+.5f*x*y,1e-5f);
        }
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<3;++x) {
            BOOST_TEST(displaced[y*4+x].heights[1]==displaced[y*4+x+1].heights[0]);
            BOOST_TEST(displaced[y*4+x].heights[2]==displaced[y*4+x+1].heights[3]);
        }
        TerrainGeometry geometry;
        BOOST_REQUIRE(geometry.Build(displaced));
        BOOST_TEST(geometry.Indices().size()==96u);
        for (const auto& vertex:geometry.Vertices()) {
            BOOST_TEST(vertex.position[2]>=geometry.Batches()[0].minimum[2]);
            BOOST_TEST(vertex.position[2]<=geometry.Batches()[0].maximum[2]);
        }
    }
    std::array<TerrainCell,1> one;
    BOOST_TEST(!Subdivide_Terrain_Cell(source,0,one,[](float,float){return 0.f;}));
}
