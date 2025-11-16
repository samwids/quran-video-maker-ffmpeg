#include "cache_utils.h"
#include "quran_data.h"
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <cctype>
#include <stdexcept>
#include <system_error>
#include <chrono>
#include <thread>
#include <cpr/cpr.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    const fs::path kCacheRoot = fs::path(".cache");
    const fs::path kAudioCacheDir = kCacheRoot / "audio";

    std::mutex translationCacheMutex;
    std::unordered_map<int, json> translationCache;
    std::mutex reciterCacheMutex;
    std::unordered_map<int, json> reciterAudioCache;

    void ensure_parent(const fs::path& path) {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }
    }
}

const json& CacheUtils::getTranslationData(int translationId) {
    std::lock_guard<std::mutex> lock(translationCacheMutex);
    auto it = translationCache.find(translationId);
    if (it == translationCache.end()) {
        auto fileIt = QuranData::translationFiles.find(translationId);
        if (fileIt == QuranData::translationFiles.end()) {
            throw std::runtime_error("Unknown translationId: " + std::to_string(translationId));
        }
        std::ifstream file(fileIt->second);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open translation file: " + fileIt->second);
        }
        json data = json::parse(file, nullptr, true, true);
        it = translationCache.emplace(translationId, std::move(data)).first;
    }
    return it->second;
}

std::string CacheUtils::getTranslationText(int translationId, const std::string& verseKey) {
    const json& translations = getTranslationData(translationId);
    auto it = translations.find(verseKey);
    if (it != translations.end() && it->is_object()) {
        auto textIt = it->find("t");
        if (textIt != it->end() && textIt->is_string()) {
            return textIt->get<std::string>();
        }
    }
    return "";
}

const json& CacheUtils::getReciterAudioData(int reciterId) {
    std::lock_guard<std::mutex> lock(reciterCacheMutex);
    auto it = reciterAudioCache.find(reciterId);
    if (it == reciterAudioCache.end()) {
        auto recIt = QuranData::reciterFiles.find(reciterId);
        if (recIt == QuranData::reciterFiles.end()) {
            throw std::runtime_error("Unknown reciterId for gapped mode: " + std::to_string(reciterId));
        }
        std::ifstream file(recIt->second);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open reciter metadata file: " + recIt->second);
        }
        json data = json::parse(file, nullptr, true, true);
        it = reciterAudioCache.emplace(reciterId, std::move(data)).first;
    }
    return it->second;
}

fs::path CacheUtils::buildCachedAudioPath(const std::string& label) {
    std::error_code ec;
    fs::create_directories(kAudioCacheDir, ec);
    return kAudioCacheDir / label;
}

bool CacheUtils::fileIsValid(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec) && fs::file_size(path, ec) > 0;
}

std::string CacheUtils::sanitizeLabel(std::string value) {
    for (char& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return value;
}

bool CacheUtils::downloadFileWithRetry(const std::string& url, const fs::path& destination, int maxRetries) {
    ensure_parent(destination);
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Unable to open destination for download: " + destination.string());
        }

        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        session.SetTimeout(cpr::Timeout{60000});
        session.SetVerifySsl(cpr::VerifySsl{true});
        session.SetRedirect(cpr::Redirect{true});
        session.SetHeader(cpr::Header{{"User-Agent", "quran-video-maker/1.0"}});
        auto response = session.Download(out);
        out.close();

        if (response.error.code == cpr::ErrorCode::OK &&
            response.status_code >= 200 && response.status_code < 400 &&
            fileIsValid(destination)) {
            return true;
        }

        std::error_code ec;
        fs::remove(destination, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
    }
    return false;
}
