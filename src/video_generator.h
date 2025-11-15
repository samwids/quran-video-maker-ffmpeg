#pragma once
#include "types.h"
#include <vector>

namespace VideoGenerator {
    void generateVideo(const CLIOptions& options, const AppConfig& config, const std::vector<VerseData>& verses);
    void generateThumbnail(const CLIOptions& options, const AppConfig& config);
}