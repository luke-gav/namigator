#pragma once

#include "utility/Include/BinaryStream.hpp"

#include <string>
#include <list>
#include <map>

using HANDLE = void *;

namespace parser
{
class MpqManager
{
    private:
        static std::list<HANDLE> MpqHandles;

        static void LoadMpq(const std::string &filePath);

    public:
        static std::string WowDir;
        static std::list<std::string> Archives;

        static void Initialize();
        static void Initialize(const std::string &wowDir);

        static utility::BinaryStream *OpenFile(const std::string &file);
};
};