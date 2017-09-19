#pragma once

#include "utility/AABBTree.hpp"
#include "utility/Matrix.hpp"
#include "utility/BoundingBox.hpp"

#include <memory>
#include <string>

namespace pathfind
{
struct Model
{
    utility::AABBTree m_aabbTree;
};

// only loaded as needed
struct DoodadModel : Model
{
};

// always loaded
struct DoodadInstance
{
    utility::Matrix m_transformMatrix;
    utility::Matrix m_inverseTransformMatrix;
    utility::BoundingBox m_bounds;
    std::string m_modelFilename;
    std::vector<utility::Vertex> m_translatedVertices;  // wow coordinate space.  indices are obtained from model.
    std::weak_ptr<DoodadModel> m_model;
};

// only loaded as needed
struct WmoModel : Model
{
    std::vector<std::vector<DoodadInstance>> m_doodadSets;
    std::vector<std::vector<std::shared_ptr<DoodadModel>>> m_loadedDoodadSets;
};

// always loaded
struct WmoInstance
{
    unsigned short m_doodadSet;
    utility::Matrix m_transformMatrix;
    utility::Matrix m_inverseTransformMatrix;
    utility::BoundingBox m_bounds;
    std::string m_modelFilename;
    std::weak_ptr<WmoModel> m_model;
};
}