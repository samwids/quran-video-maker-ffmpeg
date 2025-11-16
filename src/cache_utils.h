#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace CacheUtils {
    const nlohmann::json& getTranslationData(int translationId);
    const nlohmann::json& getReciterAudioData(int reciterId);
    std::string getTranslationText(int translationId, const std::string& verseKey);
    std::filesystem::path buildCachedAudioPath(const std::string& label);
    bool fileIsValid(const std::filesystem::path& path);
    std::string sanitizeLabel(std::string value);
    bool downloadFileWithRetry(const std::string& url, const std::filesystem::path& destination, int maxRetries = 4);
}
