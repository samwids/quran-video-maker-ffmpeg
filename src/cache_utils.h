#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace CacheUtils {
    // Configure and resolve data paths relative to the discovered config directory
    void setDataRoot(const std::filesystem::path& root);
    std::filesystem::path getDataRoot();
    std::filesystem::path resolveDataPath(const std::filesystem::path& relativePath);

    // Cache directory helpers
    void setCacheRoot(const std::filesystem::path& root);
    std::filesystem::path getCacheRoot();

    const nlohmann::json& getTranslationData(int translationId);
    const nlohmann::json& getReciterAudioData(int reciterId);
    std::string getTranslationText(int translationId, const std::string& verseKey);
    std::filesystem::path buildCachedAudioPath(const std::string& label);
    bool fileIsValid(const std::filesystem::path& path);
    std::string sanitizeLabel(std::string value);
    bool downloadFileWithRetry(const std::string& url, const std::filesystem::path& destination, int maxRetries = 4);
}
