#include "localization_utils.h"
#include "quran_data.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
json load_json_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return json();
    json data = json::parse(f, nullptr, false);
    if (data.is_discarded()) return json();
    return data;
}
}

namespace LocalizationUtils {

std::string getLanguageCode(const AppConfig& config) {
    return QuranData::getTranslationLanguageCode(config.translationId);
}

std::string getLocalizedSurahName(int surah, const std::string& lang_code) {
    fs::path path = fs::path("data/surah-names") / (lang_code + ".json");
    json data = load_json_file(path);
    std::string key = std::to_string(surah);
    if (data.is_object()) {
        auto it = data.find(key);
        if (it != data.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }
    return QuranData::surahNames.at(surah);
}

std::string getLocalizedReciterName(int reciterId, const std::string& lang_code) {
    fs::path path = fs::path("data/reciter-names") / (lang_code + ".json");
    json data = load_json_file(path);
    std::string key = std::to_string(reciterId);
    if (data.is_object()) {
        auto it = data.find(key);
        if (it != data.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }
    return QuranData::reciterNames.at(reciterId);
}

std::string getLocalizedSurahLabel(const std::string& lang_code) {
    json data = load_json_file("data/misc/surah.json");
    if (data.is_object()) {
        auto it = data.find(lang_code);
        if (it != data.end() && it->is_string()) {
            return it->get<std::string>();
        }
        auto fallback = data.find("en");
        if (fallback != data.end() && fallback->is_string()) {
            return fallback->get<std::string>();
        }
    }
    return "Surah";
}

std::string getLocalizedNumber(int value, const std::string& lang_code) {
    json data = load_json_file("data/misc/numbers.json");
    auto lookup_for_lang = [&](const std::string& code) -> std::string {
        if (!data.is_object()) return "";
        auto lang_it = data.find(code);
        if (lang_it != data.end() && lang_it->is_object()) {
            std::string key = std::to_string(value);
            auto number_it = lang_it->find(key);
            if (number_it != lang_it->end() && number_it->is_string()) {
                return number_it->get<std::string>();
            }
        }
        return "";
    };

    std::string localized = lookup_for_lang(lang_code);
    if (localized.empty()) {
        localized = lookup_for_lang("en");
    }
    if (localized.empty()) {
        localized = std::to_string(value);
    }
    return localized;
}

} // namespace LocalizationUtils
