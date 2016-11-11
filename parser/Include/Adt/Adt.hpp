#pragma once

#include "parser/Include/Wmo/WmoInstance.hpp"
#include "parser/Include/Doodad/DoodadInstance.hpp"

#include "utility/Include/BoundingBox.hpp"

#include <string>
#include <list>
#include <vector>
#include <memory>
#include <cstdint>

namespace parser
{
struct AdtChunk
{
    bool m_holeMap[8][8];

    std::vector<utility::Vertex> m_terrainVertices;
    std::vector<int> m_terrainIndices;

    std::vector<utility::Vertex> m_liquidVertices;
    std::vector<int> m_liquidIndices;

    std::vector<std::uint32_t> m_wmoInstances;
    std::vector<std::uint32_t> m_doodadInstances;

    std::uint32_t m_areaId;

    float m_minZ;
    float m_maxZ;
};

class Map;

class Adt
{
    private:
        std::unique_ptr<AdtChunk> m_chunks[16][16];
        Map * const m_map;

    public:
        const int X;
        const int Y;

        utility::BoundingBox Bounds;

        Adt(Map *map, int x, int y);
        const AdtChunk *GetChunk(int chunkX, int chunkY) const;
};
}