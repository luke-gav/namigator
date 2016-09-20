#pragma once

#include "Wmo/WmoDoodadPlacement.hpp"
#include "Wmo/Root File/WmoRootChunk.hpp"

#include "utility/Include/LinearAlgebra.hpp"

#include <vector>

namespace parser
{
namespace input
{
class MODD : WmoRootChunk
{
    public:
        const int Count;
        std::vector<WmoDoodadPlacement> Doodads;

        MODD(size_t position, utility::BinaryStream *reader);
};
}
}