#pragma once

#include "parser/Include/Map/Map.hpp"
#include "parser/Include/Wmo/Wmo.hpp"

#include "utility/Include/LinearAlgebra.hpp"
#include "utility/Include/BinaryStream.hpp"
#include "RecastDetourBuild/Include/Common.hpp"

#include <vector>
#include <string>
#include <mutex>
#include <unordered_set>
#include <map>
#include <cstdint>

namespace meshfiles
{
    class File
    {
        protected:
            // serialized heightfield and finalized mesh data, mapped by tile id
            std::map<std::pair<int, int>, utility::BinaryStream> m_tiles;

            mutable std::mutex m_mutex;

            // this function assumes that the mutex has already been locked
            void AddTile(int x, int y, utility::BinaryStream &heightfield, const utility::BinaryStream &mesh);

        public:
            virtual ~File() = default;

            virtual bool IsComplete() const = 0;
            virtual void Serialize(const std::string &filename) const = 0;
    };

    class ADT : protected File
    {
        private:
            const int m_x;
            const int m_y;

            // serialized data for WMOs and doodad IDs, mapped by tile id within the ADT
            std::map<std::pair<int, int>, utility::BinaryStream> m_wmosAndDoodadIds;

        public:
            ADT(int x, int y);

            virtual ~ADT() = default;

            // these x and y arguments refer to the tile x and y
            void AddTile(int x, int y, utility::BinaryStream &wmosAndDoodads, utility::BinaryStream &heightField, utility::BinaryStream &mesh);

            bool IsComplete() const override;
            void Serialize(const std::string &filename) const override;
    };

    class GlobalWMO : protected File
    {
        public:
            virtual ~GlobalWMO() = default;
    };
}

class MeshBuilder
{
    private:
        std::unique_ptr<parser::Map> m_map;
        const std::string m_outputPath;

        std::map<std::pair<int, int>, std::unique_ptr<meshfiles::ADT>> m_adtsInProgress;

        std::vector<std::pair<int, int>> m_pendingTiles;
        std::vector<int> m_chunkReferences; // this is a fixed size, but it is so big it can single-handedly overflow the stack

        std::unordered_set<std::string> m_bvhWmos;
        std::unordered_set<std::string> m_bvhDoodads;

        mutable std::mutex m_mutex;

        size_t m_startingTiles;
        size_t m_completedTiles;

        const int m_logLevel;

        void AddChunkReference(int chunkX, int chunkY);
        void RemoveChunkReference(int chunkX, int chunkY);

        void SerializeWmo(const parser::Wmo *wmo);
        void SerializeDoodad(const parser::Doodad *doodad);

        // these two functions assume ownership of the mutex
        meshfiles::ADT *GetInProgressADT(int x, int y);
        void RemoveADT(const meshfiles::ADT *adt);

    public:
        MeshBuilder(const std::string &dataPath, const std::string &outputPath, const std::string &mapName, int logLevel);
        MeshBuilder(const std::string &dataPath, const std::string &outputPath, const std::string &mapName, int logLevel, int adtX, int adtY);

        size_t TotalTiles() const;

        bool GetNextTile(int &tileX, int &tileY);
        
        bool IsGlobalWMO() const;

        bool GenerateAndSaveGlobalWMO();
        bool BuildAndSerializeTile(int tileX, int tileY);

        void SaveMap() const;

        float PercentComplete() const;
};
