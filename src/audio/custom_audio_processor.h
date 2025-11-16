#pragma once

#include "types.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Audio {

struct SplicePlan {
    bool enabled = false;
    bool hasBismillah = false;
    bool bismillahFromCustomSource = false;
    double bismillahStartMs = 0.0;
    double bismillahEndMs = 0.0;
    double mainStartMs = 0.0;
    double mainEndMs = 0.0;
    double paddingOffsetMs = 0.0;
    std::string sourceAudioPath;
};

class CustomAudioProcessor {
public:
    static double probeDuration(const std::string& filepath);
    static SplicePlan buildSplicePlan(const std::vector<VerseData>& verses,
                                      const CLIOptions& options);
    static void spliceRange(std::vector<VerseData>& verses,
                            const CLIOptions& options,
                            const std::filesystem::path& audioDir);
};

} // namespace Audio
