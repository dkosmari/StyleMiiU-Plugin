// -*- mode: c++ -*-
#pragma once

#include <filesystem>
#include <vector>

struct ReplaceParams {
    std::filesystem::path menPack;
    std::filesystem::path men2Pack;
    std::filesystem::path cafeBaristaMenBfsar;
    std::filesystem::path bgmBfstm;
    std::filesystem::path contentExtraDir;
};

bool
ReplaceContent(const ReplaceParams& args);
