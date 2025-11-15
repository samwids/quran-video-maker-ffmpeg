#include <iostream>
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "cxxopts.hpp"

#include "types.h"
#include "api.h"
#include "video_generator.h"
#include "quran_data.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

AppConfig loadConfig(const std::string& path, CLIOptions& options);
void validateAssets(const AppConfig& config);

int main(int argc, char* argv[]) {
    cxxopts::Options cli_parser("QuranVideoMaker", "Generates Quran videos using FFmpeg");
    // TODO: fix so default values removed and values read from config instead
    cli_parser.add_options()
        ("surah", "Surah number", cxxopts::value<int>())
        ("from", "Starting verse", cxxopts::value<int>())
        ("to", "Ending verse", cxxopts::value<int>())
        ("c,config", "Path to config file", cxxopts::value<std::string>()->default_value("./config.json"))
        ("r,reciter", "Reciter ID", cxxopts::value<int>())
        ("t,translation", "Translation ID", cxxopts::value<int>())
        ("m,mode", "Recitation mode: 'gapped' (ayah-by-ayah) or 'gapless' (surah-by-surah)", cxxopts::value<std::string>())
        ("o,output", "Output filename", cxxopts::value<std::string>())
        ("width", "Video width", cxxopts::value<int>())
        ("height", "Video height", cxxopts::value<int>())
        ("fps", "Frames per second", cxxopts::value<int>())
        ("e,encoder", "Choose encoder: 'software' (default) or 'hardware'", cxxopts::value<std::string>()->default_value("software"))
        ("p,preset", "Software encoder preset for speed/quality (ultrafast, fast, medium)", cxxopts::value<std::string>()->default_value("fast"))
        ("no-cache", "Disable caching", cxxopts::value<bool>()->default_value("false"))
        ("clear-cache", "Clear all cached data", cxxopts::value<bool>()->default_value("false"))
        ("no-growth", "Disable text growth animations", cxxopts::value<bool>()->default_value("false"))
        ("bg-theme", "Background video theme (space, nature, abstract, minimal)", cxxopts::value<std::string>())
        ("custom-audio", "Custom audio file path or URL (gapless mode only)", cxxopts::value<std::string>())
        ("custom-timing", "Custom timing file (VTT or SRT format)", cxxopts::value<std::string>())
        ("h,help", "Print usage");
    
    cli_parser.parse_positional({"surah", "from", "to"});
    auto result = cli_parser.parse(argc, argv);

    if (result.count("help") || !result.count("surah") || !result.count("from") || !result.count("to")) {
        std::cout << cli_parser.help() << std::endl;
        std::cout << "\nRecitation Modes:\n"
                  << "  gapped  - Ayah-by-ayah with pauses between verses (default)\n"
                  << "  gapless - Continuous surah recitation with precise timing\n\n"
                  << "Custom Recitation (gapless mode only):\n"
                  << "  Use --custom-audio and --custom-timing together to specify:\n"
                  << "    --custom-audio <path|url>  - Path or URL to audio file\n"
                  << "    --custom-timing <file>     - VTT or SRT file with verse timings\n"
                  << "  Example:\n"
                  << "    --custom-audio ./my_recitation.mp3 --custom-timing ./timings.vtt\n\n";
        return 1;
    }

    CLIOptions options;
    options.surah = result["surah"].as<int>();
    options.from = result["from"].as<int>();
    options.to = result["to"].as<int>();
    options.configPath = result["config"].as<std::string>();
    if (result.count("reciter")) options.reciterId = result["reciter"].as<int>();
    if (result.count("translation")) options.translationId = result["translation"].as<int>();
    if (result.count("mode")) options.recitationMode = result["mode"].as<std::string>();
    if (result.count("width")) options.width = result["width"].as<int>();
    if (result.count("height")) options.height = result["height"].as<int>();
    if (result.count("fps")) options.fps = result["fps"].as<int>();
    options.noCache = result["no-cache"].as<bool>();
    options.clearCache = result["clear-cache"].as<bool>();
    options.preset = result["preset"].as<std::string>();
    options.encoder = result["encoder"].as<std::string>();
    options.enableTextGrowth = !result["no-growth"].as<bool>();
    
    // Custom recitation options
    if (result.count("custom-audio")) options.customAudioPath = result["custom-audio"].as<std::string>();
    if (result.count("custom-timing")) options.customTimingFile = result["custom-timing"].as<std::string>();
    
    // Validate custom recitation usage
    if (!options.customAudioPath.empty() || !options.customTimingFile.empty()) {
        if (options.customAudioPath.empty() || options.customTimingFile.empty()) {
            std::cerr << "Error: Both --custom-audio and --custom-timing must be specified together." << std::endl;
            return 1;
        }
        // Custom recitations only work in gapless mode
        if (options.recitationMode.empty()) {
            options.recitationMode = "gapless";
        } else if (options.recitationMode != "gapless") {
            std::cerr << "Error: Custom recitations only work in gapless mode." << std::endl;
            return 1;
        }
    }
    
    if (result.count("output")) {
        options.output = result["output"].as<std::string>();
    } else {
        fs::path default_output_dir = "out";
        if (!fs::exists(default_output_dir)) {
            if (!fs::create_directories(default_output_dir)) {
                throw std::runtime_error("Failed to create directory: " + default_output_dir.string());
            }
        }
        options.output = "out/surah-" + std::to_string(options.surah) + "_" + std::to_string(options.from) + "-" + std::to_string(options.to) + ".mp4";
    }
    
    try {
        if (options.clearCache && fs::exists(".cache")) {
            std::cout << "Clearing cache..." << std::endl;
            fs::remove_all(".cache");
        }
        
        AppConfig config = loadConfig(options.configPath, options);
        
        // Override background theme if specified
        if (result.count("bg-theme")) {
            std::string theme = result["bg-theme"].as<std::string>();
            auto it = QuranData::backgroundThemes.find(theme);
            if (it != QuranData::backgroundThemes.end()) {
                config.assetBgVideo = it->second;
            } else {
                std::cerr << "Warning: Unknown theme '" << theme << "', using default." << std::endl;
            }
        }
        
        validateAssets(config);

        std::string modeStr = (config.recitationMode == RecitationMode::GAPLESS) ? "gapless" : "gapped";
        std::cout << "Rendering Surah " << options.surah << ", verses " << options.from << "-" << options.to << std::endl;
        std::cout << "Mode: " << modeStr << std::endl;
        std::cout << "Config: " << config.width << "x" << config.height << " @ " << config.fps << "fps, reciter=" << config.reciterId << ", translation=" << config.translationId << std::endl;
        std::cout << "Text growth: " << (config.enableTextGrowth ? "enabled" : "disabled") << std::endl;

        auto verses = API::fetchQuranData(options, config);
        VideoGenerator::generateVideo(options, config, verses);
        VideoGenerator::generateThumbnail(options, config);

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

AppConfig loadConfig(const std::string& path, CLIOptions& options) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Could not open config file: " + path);
    
    json data = json::parse(f);
    AppConfig cfg;

    // Video dimensions
    cfg.width = data.value("width", 1280);
    cfg.height = data.value("height", 720);
    cfg.fps = data.value("fps", 30);
    
    // Content selection
    cfg.reciterId = data.value("reciterId", 7);
    cfg.translationId = data.value("translationId", 1);
    
    // Recitation mode
    std::string modeStr = data.value("recitationMode", "gapped");
    if (modeStr == "gapless") {
        cfg.recitationMode = RecitationMode::GAPLESS;
    } else {
        cfg.recitationMode = RecitationMode::GAPPED;
    }
    
    // Visual styling
    cfg.overlayColor = data.value("overlayColor", "0x000000@0.5");
    cfg.assetFolderPath = data.value("assetFolderPath", "assets");
    cfg.assetBgVideo = cfg.assetFolderPath + "/" + data.value("assetBgVideo", QuranData::defaultBackgroundVideo);

    // Font configuration
    cfg.arabicFont.family = data["arabicFont"].value("family", "KFGQPC HAFS Uthmanic Script");
    cfg.arabicFont.file = cfg.assetFolderPath + "/" + data["arabicFont"].value("file", QuranData::defaultArabicFont);
    cfg.arabicFont.size = data["arabicFont"].value("size", 100);
    cfg.arabicFont.color = data["arabicFont"].value("color", "FFFFFF");

    cfg.translationFont.family = data["translationFont"].value("family", "American Captain");
    cfg.translationFont.size = data["translationFont"].value("size", 50);
    cfg.translationFont.color = data["translationFont"].value("color", "D3D3D3");
    
    // Auto-select translation font based on translation ID if not specified
    if (!data["translationFont"].contains("file")) {
        cfg.translationFont.file = cfg.assetFolderPath + "/" + QuranData::getTranslationFont(cfg.translationId);
    } else {
        // TODO: consider throwing an error instead of assuming english subtitles should be produced
        cfg.translationFont.file = cfg.assetFolderPath + "/" + data["translationFont"].value("file", QuranData::defaultTranslationFont);
    }

    // Data paths
    cfg.quranWordByWordPath = data.value("quranWordByWordPath", "data/quran/qpc-hafs-word-by-word.json");

    // Timing parameters
    cfg.introDuration = data.value("introDuration", 1.0);
    cfg.pauseAfterIntroDuration = data.value("pauseAfterIntroDuration", 0.5);
    cfg.introFadeOutMs = data.value("introFadeOutMs", 500);
    
    // Text animation parameters
    cfg.enableTextGrowth = data.value("enableTextGrowth", true);
    cfg.textGrowthThreshold = data.value("textGrowthThreshold", 100);
    cfg.maxGrowthFactor = data.value("maxGrowthFactor", 1.15);
    cfg.growthRateFactor = data.value("growthRateFactor", 0.05);
    
    // Fade parameters
    cfg.fadeDurationFactor = data.value("fadeDurationFactor", 0.2);
    cfg.minFadeDuration = data.value("minFadeDuration", 0.05);
    cfg.maxFadeDuration = data.value("maxFadeDuration", 0.1);
    
    // Text wrapping parameters
    cfg.textWrapThreshold = data.value("textWrapThreshold", 20);
    cfg.arabicMaxWidthFraction = data.value("arabicMaxWidthFraction", 0.95);
    cfg.translationMaxWidthFraction = data.value("translationMaxWidthFraction", 0.85);
    
    // Layout parameters
    cfg.verticalShift = data.value("verticalShift", 40.0);
    
    // Thumbnail parameters
    if (data.contains("thumbnailColors") && data["thumbnailColors"].is_array()) {
        for (const auto& color : data["thumbnailColors"]) {
            cfg.thumbnailColors.push_back(color.get<std::string>());
        }
    }
    cfg.thumbnailNumberPadding = data.value("thumbnailNumberPadding", 100);

    // CLI overrides
    if (options.reciterId != -1) cfg.reciterId = options.reciterId;
    if (options.translationId != -1) {
        cfg.translationId = options.translationId;
        cfg.translationFont.file = cfg.assetFolderPath + "/" + QuranData::getTranslationFont(cfg.translationId);
    }
    if (!options.recitationMode.empty()) {
        if (options.recitationMode == "gapless") {
            cfg.recitationMode = RecitationMode::GAPLESS;
        } else if (options.recitationMode == "gapped") {
            cfg.recitationMode = RecitationMode::GAPPED;
        } else {
            std::cerr << "Warning: Unknown recitation mode '" << options.recitationMode << "', using config default." << std::endl;
        }
    }
    if (options.width != -1) cfg.width = options.width;
    if (options.height != -1) cfg.height = options.height;
    if (options.fps != -1) cfg.fps = options.fps;
    
    cfg.enableTextGrowth = options.enableTextGrowth;

    return cfg;
}

void validateAssets(const AppConfig& config) {
    if (!fs::exists(config.assetBgVideo)) {
        throw std::runtime_error("Background video not found: " + config.assetBgVideo);
    }
    
    if (!fs::exists(config.arabicFont.file)) {
        throw std::runtime_error("Arabic font file not found: " + config.arabicFont.file);
    }
    if (!fs::exists(config.translationFont.file)) {
        throw std::runtime_error("Translation font file not found: " + config.translationFont.file);
    }
    
    if (!fs::exists(config.quranWordByWordPath)) {
        throw std::runtime_error("Quran word-by-word data not found: " + config.quranWordByWordPath);
    }
}