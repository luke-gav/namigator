#include "Map.hpp"
#include "Tile.hpp"

#include "MapBuilder/Include/MeshBuilder.hpp"

#include "utility/Include/MathHelper.hpp"
#include "utility/Include/LinearAlgebra.hpp"
#include "utility/Include/BoundingBox.hpp"
#include "utility/Include/Exception.hpp"

#include "Recast/Include/Recast.h"
#include "Detour/Include/DetourNavMeshBuilder.h"

#include <cassert>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>

namespace
{
class RecastContext : public rcContext
{
    private:
    const rcLogCategory m_logLevel;

    virtual void doLog(const rcLogCategory category, const char *msg, const int) override
    {
        if (!m_logLevel || category < m_logLevel)
            return;

        std::stringstream out;

        out << "Thread #" << std::setfill(' ') << std::setw(6) << std::this_thread::get_id() << " ";

        switch (category)
        {
            case rcLogCategory::RC_LOG_ERROR:
                out << "ERROR: ";
                break;
            case rcLogCategory::RC_LOG_PROGRESS:
                out << "PROGRESS: ";
                break;
            case rcLogCategory::RC_LOG_WARNING:
                out << "WARNING: ";
                break;
            default:
                out << "rcContext::doLog(" << category << "): ";
                break;
        }

        out << msg;

        MessageBox(nullptr, out.str().c_str(), "Recast Failure", 0);
    }

    public:
    RecastContext(int logLevel) : m_logLevel(static_cast<rcLogCategory>(logLevel)) {}
};

// TODO these next two functions are essentially copied from MapBuilder.  do something to remove the code duplication!

// Recast does not support multiple walkable climb values.  However, when being used for NPCs, who can walk up ADT terrain of any slope, this is what we need.
// As a workaround for this situation, we will have Recast build the compact height field with an 'infinite' value for walkable climb, and then run our own
// custom filter on the compact heightfield to enforce the walkable climb for WMOs and doodads
void SelectivelyEnforceWalkableClimb(rcCompactHeightfield &chf, int walkableClimb)
{
    for (int y = 0; y < chf.height; ++y)
        for (int x = 0; x < chf.width; ++x)
        {
            auto const &cell = chf.cells[y*chf.width + x];

            // for each span in this cell of the compact heightfield...
            for (int i = static_cast<int>(cell.index), ni = static_cast<int>(cell.index + cell.count); i < ni; ++i)
            {
                auto &span = chf.spans[i];
                const AreaFlags spanArea = static_cast<AreaFlags>(chf.areas[i]);

                // check all four directions for this span
                for (int dir = 0; dir < 4; ++dir)
                {
                    // there will be at most one connection for this span in this direction
                    auto const k = rcGetCon(span, dir);

                    // if there is no connection, do nothing
                    if (k == RC_NOT_CONNECTED)
                        continue;

                    auto const nx = x + rcGetDirOffsetX(dir);
                    auto const ny = y + rcGetDirOffsetY(dir);

                    // this should never happen since we already know there is a connection in this direction
                    assert(nx >= 0 && ny >= 0 && nx < chf.width && ny < chf.height);

                    auto const &neighborCell = chf.cells[ny*chf.width + nx];
                    auto const &neighborSpan = chf.spans[k + neighborCell.index];

                    // if the span height difference is <= the walkable climb, nothing else matters.  skip it
                    if (rcAbs(static_cast<int>(neighborSpan.y) - static_cast<int>(span.y)) <= walkableClimb)
                        continue;

                    const AreaFlags neighborSpanArea = static_cast<AreaFlags>(chf.areas[k + neighborCell.index]);

                    // if both the current span and the neighbor span are ADTs, do nothing
                    if (spanArea == AreaFlags::ADT && neighborSpanArea == AreaFlags::ADT)
                        continue;

                    rcSetCon(span, dir, RC_NOT_CONNECTED);
                }
            }
        }
}

#define ZERO(x) memset(&x, 0, sizeof(x))

// NOTE: this does not set bmin/bmax
void InitializeRecastConfig(rcConfig &config)
{
    ZERO(config);

    config.cs = MeshSettings::CellSize;
    config.ch = MeshSettings::CellHeight;
    config.walkableSlopeAngle = MeshSettings::WalkableSlope;
    config.walkableClimb = MeshSettings::VoxelWalkableClimb;
    config.walkableHeight = MeshSettings::VoxelWalkableHeight;
    config.walkableRadius = MeshSettings::VoxelWalkableRadius;
    config.maxEdgeLen = config.walkableRadius * 4;
    config.maxSimplificationError = MeshSettings::MaxSimplificationError;
    config.minRegionArea = MeshSettings::MinRegionSize;
    config.mergeRegionArea = MeshSettings::MergeRegionSize;
    config.maxVertsPerPoly = MeshSettings::VerticesPerPolygon;
    config.tileSize = MeshSettings::TileVoxelSize;
    config.borderSize = config.walkableRadius + 3;
    config.width = config.tileSize + config.borderSize * 2;
    config.height = config.tileSize + config.borderSize * 2;
    config.detailSampleDist = MeshSettings::DetailSampleDistance;
    config.detailSampleMaxError = MeshSettings::DetailSampleMaxError;
}

using SmartHeightFieldPtr = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>;
using SmartHeightFieldLayerSetPtr = std::unique_ptr<rcHeightfieldLayerSet, decltype(&rcFreeHeightfieldLayerSet)>;
using SmartCompactHeightFieldPtr = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
using SmartContourSetPtr = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>;
using SmartPolyMeshPtr = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>;
using SmartPolyMeshDetailPtr = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>;

bool RebuildMeshTile(rcContext &ctx, const rcConfig &config, int tileX, int tileY, rcHeightfield &solid, std::vector<unsigned char> &out)
{
    // initialize compact height field
    SmartCompactHeightFieldPtr chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);

    if (!rcBuildCompactHeightfield(&ctx, config.walkableHeight, (std::numeric_limits<int>::max)(), solid, *chf))
        return false;

    SelectivelyEnforceWalkableClimb(*chf, config.walkableClimb);

    if (!rcBuildDistanceField(&ctx, *chf))
        return false;

    if (!rcBuildRegions(&ctx, *chf, config.borderSize, config.minRegionArea, config.mergeRegionArea))
        return false;

    SmartContourSetPtr cset(rcAllocContourSet(), rcFreeContourSet);

    if (!rcBuildContours(&ctx, *chf, config.maxSimplificationError, config.maxEdgeLen, *cset))
        return false;

    // it is possible that this tile has no navigable geometry.  in this case, we 'succeed' by doing nothing further
    if (!cset->nconts)
        return true;

    SmartPolyMeshPtr polyMesh(rcAllocPolyMesh(), rcFreePolyMesh);

    if (!rcBuildPolyMesh(&ctx, *cset, config.maxVertsPerPoly, *polyMesh))
        return false;

    SmartPolyMeshDetailPtr polyMeshDetail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);

    if (!rcBuildPolyMeshDetail(&ctx, *polyMesh, *chf, config.detailSampleDist, config.detailSampleMaxError, *polyMeshDetail))
        return false;

    chf.reset();
    cset.reset();

    // too many vertices?
    if (polyMesh->nverts >= 0xFFFF)
    {
        std::stringstream str;
        str << "Too many mesh vertices produces for tile (" << tileX << ", " << tileY << ")" << std::endl;
        std::cerr << str.str();
        return false;
    }

    for (int i = 0; i < polyMesh->npolys; ++i)
    {
        if (!polyMesh->areas[i])
            continue;

        polyMesh->flags[i] = static_cast<unsigned short>(PolyFlags::Walkable | polyMesh->areas[i]);
    }

    dtNavMeshCreateParams params;
    ZERO(params);

    params.verts = polyMesh->verts;
    params.vertCount = polyMesh->nverts;
    params.polys = polyMesh->polys;
    params.polyAreas = polyMesh->areas;
    params.polyFlags = polyMesh->flags;
    params.polyCount = polyMesh->npolys;
    params.nvp = polyMesh->nvp;
    params.detailMeshes = polyMeshDetail->meshes;
    params.detailVerts = polyMeshDetail->verts;
    params.detailVertsCount = polyMeshDetail->nverts;
    params.detailTris = polyMeshDetail->tris;
    params.detailTriCount = polyMeshDetail->ntris;
    params.walkableHeight = MeshSettings::WalkableHeight;
    params.walkableRadius = MeshSettings::WalkableRadius;
    params.walkableClimb = MeshSettings::WalkableClimb;
    params.tileX = tileX;
    params.tileY = tileY;
    params.tileLayer = 0;
    memcpy(params.bmin, polyMesh->bmin, sizeof(polyMesh->bmin));
    memcpy(params.bmax, polyMesh->bmax, sizeof(polyMesh->bmax));
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char *outData;
    int outDataSize;
    if (!dtCreateNavMeshData(&params, &outData, &outDataSize))
        return false;

    out.clear();
    out.resize(static_cast<size_t>(outDataSize));
    memcpy(&out[0], outData, out.size());

    dtFree(outData);

    return true;
}
}

namespace pathfind
{
void Map::AddGameObject(std::uint64_t guid, unsigned int displayId, const utility::Vertex &position, float orientation, int doodadSet)
{
    auto const matrix = utility::Matrix::CreateRotationZ(orientation);
    AddGameObject(guid, displayId, position, matrix, doodadSet);
}

void Map::AddGameObject(std::uint64_t guid, unsigned int displayId, const utility::Vertex &position, const utility::Quaternion &rotation, int doodadSet)
{
    auto const matrix = utility::Matrix::CreateFromQuaternion(rotation);
    AddGameObject(guid, displayId, position, matrix, doodadSet);
}

void Map::AddGameObject(std::uint64_t guid, unsigned int displayId, const utility::Vertex &position, const utility::Matrix &rotation, int /*doodadSet*/)
{
    if (m_temporaryDoodads.find(guid) != m_temporaryDoodads.end() || m_temporaryWmos.find(guid) != m_temporaryWmos.end())
        THROW("Game object with specified GUID already exists");

    auto const matrix = utility::Matrix::CreateTranslationMatrix(position) * rotation;

    auto const doodad = m_temporaryObstaclePaths[displayId][0] == 'd' || m_temporaryObstaclePaths[displayId][0] == 'D';

    if (doodad)
    {
        auto const fileName = m_temporaryObstaclePaths[displayId].substr(7, m_temporaryObstaclePaths[displayId].length() - 11);
        auto instance = std::make_shared<DoodadInstance>();

        instance->m_transformMatrix = matrix;
        instance->m_inverseTransformMatrix = matrix.ComputeInverse();
        instance->m_modelFilename = fileName;
        auto model = EnsureDoodadModelLoaded(fileName);
        instance->m_model = model;

        instance->m_translatedVertices.reserve(model->m_aabbTree.Vertices().size());

        for (auto const &v : model->m_aabbTree.Vertices())
            instance->m_translatedVertices.emplace_back(utility::Vertex::Transform(v, matrix));

        // models are guarunteed to have more than zero vertices
        utility::BoundingBox bounds { instance->m_translatedVertices[0], instance->m_translatedVertices[0] };

        for (auto i = 1u; i < instance->m_translatedVertices.size(); ++i)
            bounds.update(instance->m_translatedVertices[i]);

        instance->m_bounds = bounds;
        m_temporaryDoodads[guid] = instance;

        for (auto const &tile : m_tiles)
        {
            if (!tile.second->m_bounds.intersect2d(instance->m_bounds))
                continue;

            tile.second->AddTemporaryDoodad(guid, instance);
        }
    }
    else
    {
        THROW("Temporary WMO obstacles are not supported");

        //auto const model = LoadWmoModel(0); // TODO make this support loading a filename

        //// if there is only one, the specified set is irrelevant.  use it!
        //if (doodadSet < 0 && model->m_doodadSets.size() > 1)
        //    THROW("No doodad set specified for WMO game object");
        //
        //if (doodadSet < 0)
        //    doodadSet = 0;

        //WmoInstance instance { static_cast<unsigned short>(doodadSet), matrix, matrix.ComputeInverse(), utility::BoundingBox(), model };
    }
}

void Tile::AddTemporaryDoodad(std::uint64_t guid, std::shared_ptr<DoodadInstance> doodad)
{
    auto const model = doodad->m_model.lock();

    std::vector<float> recastVertices;
    utility::Convert::VerticesToRecast(doodad->m_translatedVertices, recastVertices);

    std::vector<unsigned char> areas(model->m_aabbTree.Indices().size(), AreaFlags::Doodad);

    m_temporaryDoodads[guid] = std::move(doodad);

    RecastContext ctx(rcLogCategory::RC_LOG_ERROR);
    rcClearUnwalkableTriangles(&ctx, MeshSettings::WalkableSlope, &recastVertices[0], static_cast<int>(recastVertices.size() / 3),
        &model->m_aabbTree.Indices()[0], static_cast<int>(model->m_aabbTree.Indices().size() / 3), &areas[0]);
    rcRasterizeTriangles(&ctx, &recastVertices[0], static_cast<int>(recastVertices.size() / 3), &model->m_aabbTree.Indices()[0],
        &areas[0], static_cast<int>(model->m_aabbTree.Indices().size() / 3), m_heightField);

    // save all span area flags because we dont want the upcoming filtering to apply to ADT terrain
    {
        std::vector<rcSpan *> adtSpans;

        adtSpans.reserve(m_heightField.width*m_heightField.height);

        for (int i = 0; i < m_heightField.width * m_heightField.height; ++i)
            for (rcSpan *s = m_heightField.spans[i]; s; s = s->next)
                if (!!(s->area & AreaFlags::ADT))
                    adtSpans.push_back(s);

        rcFilterLedgeSpans(&ctx, MeshSettings::VoxelWalkableHeight, MeshSettings::VoxelWalkableClimb, m_heightField);

        for (auto s : adtSpans)
            s->area |= AreaFlags::ADT;
    }

    rcFilterWalkableLowHeightSpans(&ctx, MeshSettings::VoxelWalkableHeight, m_heightField);
    rcFilterLowHangingWalkableObstacles(&ctx, MeshSettings::VoxelWalkableClimb, m_heightField);

    rcConfig config;

    InitializeRecastConfig(config);

    auto const buildResult = RebuildMeshTile(ctx, config, m_x, m_y, m_heightField, m_tileData);

    assert(buildResult);

    if (m_ref)
    {
        auto const removeResult = m_map->m_navMesh.removeTile(m_ref, nullptr, nullptr);
        assert(removeResult == DT_SUCCESS);
    }

    auto const insertResult = m_map->m_navMesh.addTile(&m_tileData[0], static_cast<int>(m_tileData.size()), 0, 0, &m_ref);

    assert(insertResult == DT_SUCCESS);
}
}