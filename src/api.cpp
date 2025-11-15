#include "api.h"
#include "quran_data.h"
#include "timing_parser.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <future>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <filesystem>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    // Helper to get audio duration using FFmpeg
    double get_audio_duration(const std::string& filepath) {
        AVFormatContext* format_context = nullptr;
        if (avformat_open_input(&format_context, filepath.c_str(), nullptr, nullptr) != 0) {
            std::cerr << "Warning: Could not open audio file " << filepath << " to get duration." << std::endl;
            return 0.0;
        }
        if (avformat_find_stream_info(format_context, nullptr) < 0) {
            std::cerr << "Warning: Could not find stream info for " << filepath << " to get duration." << std::endl;
            avformat_close_input(&format_context);
            return 0.0;
        }
        double duration = (double)format_context->duration / AV_TIME_BASE;
        avformat_close_input(&format_context);
        return duration;
    }

    std::string strip_html_tags(const std::string& input) {
        std::string output;
        output.reserve(input.length());
        bool in_tag = false;
        for (char c : input) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                output += c;
            }
        }
        return output;
    }

    // GAPPED MODE: Fetch individual ayah data
    VerseData fetch_single_verse_gapped(int surah, int verseNum, const AppConfig& config, bool useCache, const fs::path& audioDir) {
        std::string verseKey = std::to_string(surah) + ":" + std::to_string(verseNum);
        fs::path cachePath = fs::path(".cache") / (verseKey + "_r" + std::to_string(config.reciterId) + "_t" + std::to_string(config.translationId) + "_gapped.json");

        if (useCache && fs::exists(cachePath)) {
            try {
                std::ifstream f(cachePath);
                json data = json::parse(f);
                std::cout << "  - Using cached data for " << verseKey << std::endl;
                return {
                    data.at("verseKey"), data.at("text"), data.at("translation"),
                    data.at("audioUrl"), data.at("durationInSeconds"), data.at("localAudioPath"),
                    data.value("timestampFromMs", 0), data.value("timestampToMs", 0), {}
                };
            } catch (const json::exception&) {
                std::cout << "  - Cache invalid for " << verseKey << ", re-fetching." << std::endl;
            }
        }

        VerseData result;
        result.verseKey = verseKey;
        result.text = "";

        // Load translation
        auto transIt = QuranData::translationFiles.find(config.translationId);
        if (transIt == QuranData::translationFiles.end())
            throw std::runtime_error("Unknown translationId: " + std::to_string(config.translationId));

        try {
            std::ifstream tfile(transIt->second);
            json translations = json::parse(tfile);
            if (translations.contains(verseKey) && translations[verseKey].contains("t"))
                result.translation = translations[verseKey]["t"].get<std::string>();
            else
                result.translation = "";
        } catch (const json::exception& e) {
            std::cerr << "Warning: Could not load translation for " << verseKey << ": " << e.what() << std::endl;
            result.translation = "";
        }

        // Load audio metadata
        auto recIt = QuranData::reciterFiles.find(config.reciterId);
        if (recIt == QuranData::reciterFiles.end())
            throw std::runtime_error("Unknown reciterId for gapped mode: " + std::to_string(config.reciterId));

        try {
            std::ifstream afile(recIt->second);
            json audioData = json::parse(afile);
            if (!audioData.contains(verseKey))
                throw std::runtime_error("Verse not found in audio JSON: " + verseKey);

            const auto& verseAudio = audioData[verseKey];
            result.audioUrl = verseAudio.at("audio_url").get<std::string>();
        } catch (const std::exception& e) {
            throw std::runtime_error("Error loading audio metadata for " + verseKey + ": " + std::string(e.what()));
        }

        // Download audio
        result.localAudioPath = (audioDir / (std::to_string(surah) + "_" + std::to_string(verseNum) + ".mp3")).string();
        std::ofstream audioFile(result.localAudioPath, std::ios::binary);
        cpr::Response audio_r = cpr::Download(audioFile, cpr::Url{result.audioUrl});
        audioFile.close();
        if (audio_r.status_code >= 400)
            throw std::runtime_error("Failed to download audio for " + verseKey + " from " + result.audioUrl);

        result.durationInSeconds = get_audio_duration(result.localAudioPath);
        if (result.durationInSeconds == 0.0)
            std::cerr << "\nWarning: Could not determine duration for " << verseKey << ".\n";

        // Save to cache
        if (useCache) {
            fs::create_directories(".cache");
            json cacheData = {
                {"verseKey", result.verseKey}, {"text", result.text}, {"translation", result.translation},
                {"audioUrl", result.audioUrl}, {"durationInSeconds", result.durationInSeconds},
                {"localAudioPath", result.localAudioPath}
            };
            std::ofstream o(cachePath);
            o << std::setw(4) << cacheData << std::endl;
        }

        return result;
    }

    // GAPLESS MODE: Fetch verse data with timing from surah audio or custom source
    std::vector<VerseData> fetch_verses_gapless(int surah, int from, int to, const AppConfig& config, bool useCache, const fs::path& audioDir, const CLIOptions& options) {
        std::cout << "  - Using GAPLESS mode (surah-by-surah)" << std::endl;
        
        std::string localAudioPath;
        std::map<std::string, TimingEntry> timings;
        
        // Check if using custom recitation
        if (!options.customAudioPath.empty() && !options.customTimingFile.empty()) {
            std::cout << "  - Using CUSTOM recitation" << std::endl;
            
            // Parse timing file
            timings = TimingParser::parseTimingFile(options.customTimingFile);
            
            // Download or copy audio file
            if (options.customAudioPath.find("http://") == 0 || options.customAudioPath.find("https://") == 0) {
                // Download from URL
                localAudioPath = (audioDir / ("custom_surah_" + std::to_string(surah) + ".mp3")).string();
                if (!useCache || !fs::exists(localAudioPath)) {
                    std::cout << "  - Downloading custom audio from " << options.customAudioPath << std::endl;
                    std::ofstream audioFile(localAudioPath, std::ios::binary);
                    cpr::Response audio_r = cpr::Download(audioFile, cpr::Url{options.customAudioPath});
                    audioFile.close();
                    if (audio_r.status_code >= 400)
                        throw std::runtime_error("Failed to download custom audio from " + options.customAudioPath);
                } else {
                    std::cout << "  - Using cached custom audio" << std::endl;
                }
            } else {
                // Use local file path
                localAudioPath = options.customAudioPath;
                if (!fs::exists(localAudioPath))
                    throw std::runtime_error("Custom audio file not found: " + localAudioPath);
            }
        } else {
            // Use standard reciter data
            auto recDirIt = QuranData::gaplessReciterDirs.find(config.reciterId);
            if (recDirIt == QuranData::gaplessReciterDirs.end())
                throw std::runtime_error("Reciter ID " + std::to_string(config.reciterId) + " not available for gapless mode");

            std::string reciterDir = recDirIt->second;
            fs::path surahJsonPath = fs::path(reciterDir) / "surah.json";
            fs::path segmentsJsonPath = fs::path(reciterDir) / "segments.json";

            if (!fs::exists(surahJsonPath) || !fs::exists(segmentsJsonPath))
                throw std::runtime_error("Missing surah.json or segments.json for reciter in " + reciterDir);

            // Load surah metadata (audio URL)
            std::ifstream surahFile(surahJsonPath);
            json surahData = json::parse(surahFile);
            std::string surahKey = std::to_string(surah);
            if (!surahData.contains(surahKey))
                throw std::runtime_error("Surah " + surahKey + " not found in surah.json");

            std::string audioUrl = surahData[surahKey]["audio_url"].get<std::string>();
            
            // Download the full surah audio
            localAudioPath = (audioDir / ("surah_" + std::to_string(surah) + "_r" + std::to_string(config.reciterId) + ".mp3")).string();
            
            if (!useCache || !fs::exists(localAudioPath)) {
                std::cout << "  - Downloading full surah audio from " << audioUrl << std::endl;
                std::ofstream audioFile(localAudioPath, std::ios::binary);
                cpr::Response audio_r = cpr::Download(audioFile, cpr::Url{audioUrl});
                audioFile.close();
                if (audio_r.status_code >= 400)
                    throw std::runtime_error("Failed to download surah audio from " + audioUrl);
            } else {
                std::cout << "  - Using cached surah audio" << std::endl;
            }

            // Load segments (timing information)
            std::ifstream segmentsFile(segmentsJsonPath);
            json segmentsData = json::parse(segmentsFile);
            
            // Convert segments to timing map
            for (int verseNum = from; verseNum <= to; ++verseNum) {
                std::string verseKey = std::to_string(surah) + ":" + std::to_string(verseNum);
                if (segmentsData.contains(verseKey)) {
                    const auto& verseSegment = segmentsData[verseKey];
                    TimingEntry entry;
                    entry.verseKey = verseKey;
                    entry.startMs = verseSegment["timestamp_from"].get<int>();
                    entry.endMs = verseSegment["timestamp_to"].get<int>();
                    timings[verseKey] = entry;
                }
            }
        }

        // Load translations
        auto transIt = QuranData::translationFiles.find(config.translationId);
        if (transIt == QuranData::translationFiles.end())
            throw std::runtime_error("Unknown translationId: " + std::to_string(config.translationId));
        
        std::ifstream tfile(transIt->second);
        json translations = json::parse(tfile);

        // Build verse data
        std::vector<VerseData> results;
        for (int verseNum = from; verseNum <= to; ++verseNum) {
            std::string verseKey = std::to_string(surah) + ":" + std::to_string(verseNum);
            std::string timingKey = options.customTimingFile.empty() ? verseKey : ("SURAH:" + std::to_string(verseNum));
            
            if (!timings.count(verseKey) && !timings.count(timingKey))
                throw std::runtime_error("Verse " + verseKey + " not found in timing data");

            const TimingEntry& timing = timings.count(verseKey) ? timings[verseKey] : timings[timingKey];
            
            VerseData verse;
            verse.verseKey = verseKey;
            verse.audioUrl = "";  // Not needed for gapless
            verse.localAudioPath = localAudioPath;  // Same for all verses
            verse.timestampFromMs = timing.startMs;
            verse.timestampToMs = timing.endMs;
            verse.durationInSeconds = (verse.timestampToMs - verse.timestampFromMs) / 1000.0;
            
            // Get translation
            if (translations.contains(verseKey) && translations[verseKey].contains("t"))
                verse.translation = translations[verseKey]["t"].get<std::string>();
            else
                verse.translation = "";
            
            verse.text = "";  // Will be filled later
            
            results.push_back(verse);
        }

        return results;
    }
}

void pop_back_utf8(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (s[i] & 0xC0) == 0x80) {
        --i;
    }
    s.erase(i);
}

std::vector<VerseData> API::fetchQuranData(const CLIOptions& options, const AppConfig& config) {
    std::cout << "Fetching data for Surah " << options.surah << ", verses " << options.from << "-" << options.to << "..." << std::endl;
    
    fs::path audioDir = fs::temp_directory_path() / "quran_video_audio";
    fs::create_directory(audioDir);

    std::vector<VerseData> results;
    
    // Choose mode based on config
    if (config.recitationMode == RecitationMode::GAPLESS) {
        results = fetch_verses_gapless(options.surah, options.from, options.to, config, !options.noCache, audioDir, options);
    } else {
        // GAPPED mode - parallel fetch
        std::vector<std::future<VerseData>> futures;
        for (int i = options.from; i <= options.to; ++i) {
            futures.push_back(std::async(std::launch::async, fetch_single_verse_gapped, options.surah, i, config, !options.noCache, audioDir));
        }

        results.reserve(futures.size());
        for (auto& fut : futures) {
            results.push_back(fut.get());
        }
        
        std::sort(results.begin(), results.end(), [](const VerseData& a, const VerseData& b) {
            int verseA = std::stoi(a.verseKey.substr(a.verseKey.find(':') + 1));
            int verseB = std::stoi(b.verseKey.substr(b.verseKey.find(':') + 1));
            return verseA < verseB;
        });
    }

    // Load QPC Uthmani text for all verses
    std::ifstream file(config.quranWordByWordPath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << config.quranWordByWordPath << "\n";
        return results;
    }
    json quranData;
    file >> quranData;

    // Add Bismillah if needed
    // TODO: this has a major bug fix this so we get the audio from gapped for both cases, use a high quality default bismillah
    if (options.surah != 1 && options.surah != 9) {
        if (config.recitationMode == RecitationMode::GAPLESS) {
            // For gapless, we need special handling of Bismillah timing
            auto bismillahVerses = fetch_verses_gapless(1, 1, 1, config, !options.noCache, audioDir, options);
            if (!bismillahVerses.empty()) {
                results.insert(results.begin(), bismillahVerses[0]);
            }
        } else {
            results.insert(results.begin(), fetch_single_verse_gapped(1, 1, config, !options.noCache, audioDir));
        }
    }

    // Fill in QPC Arabic text
    for (auto& verse : results) {
        std::string keyPrefix = verse.verseKey + ":";
        std::vector<std::pair<int, std::string>> words;
        for (auto it = quranData.begin(); it != quranData.end(); ++it) {
            if (it.key().rfind(keyPrefix, 0) == 0) {
                auto parts = it.key().substr(keyPrefix.size());
                int wordIndex = std::stoi(parts);
                words.emplace_back(wordIndex, it.value().value("text", ""));
            }
        }
        std::sort(words.begin(), words.end(),
                [](auto& a, auto& b) { return a.first < b.first; });

        std::string text;
        for (auto& w : words)
            text += w.second + " ";

        if (!text.empty())
            verse.text = text;
    }

    // Remove last word from Bismillah if it's not Surah 1 or 9
    if (options.surah != 1 && options.surah != 9) {
        if (!results.empty() && !results[0].text.empty()) {
            results[0].text.pop_back();
            results[0].text.pop_back();
            results[0].text.pop_back();
        }
    }

    return results;
}