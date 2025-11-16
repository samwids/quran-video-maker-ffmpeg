#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "types.h"
#include "config_loader.h"
#include "cache_utils.h"
#include "recitation_utils.h"
#include "localization_utils.h"
#include "subtitle_builder.h"
#include "timing_parser.h"

namespace fs = std::filesystem;

VerseData makeSampleVerse() {
    VerseData verse;
    verse.verseKey = "1:1";
    verse.text = "بِسْمِ اللّٰهِ";
    verse.translation = "In the name of Allah";
    verse.durationInSeconds = 1.5;
    verse.localAudioPath = "";
    verse.timestampFromMs = 0;
    verse.timestampToMs = 1500;
    return verse;
}

fs::path getProjectRoot() {
    static const fs::path root = fs::absolute(fs::path(__FILE__)).parent_path().parent_path();
    return root;
}

void testConfigLoader() {
    CLIOptions opts;
    AppConfig cfg = loadConfig((getProjectRoot() / "config.json").string(), opts);
    assert(cfg.width > 0);
    assert(cfg.height > 0);
    assert(cfg.assetFolderPath == "assets");
}

void testCacheUtils() {
    std::string sanitized = CacheUtils::sanitizeLabel("1:1/r");
    assert(sanitized.find(':') == std::string::npos);
    std::string translation = CacheUtils::getTranslationText(1, "1:1");
    assert(!translation.empty());
}

void testLocalization() {
    CLIOptions opts;
    AppConfig cfg = loadConfig((getProjectRoot() / "config.json").string(), opts);
    std::string lang = LocalizationUtils::getLanguageCode(cfg);
    assert(!lang.empty());
    std::string name = LocalizationUtils::getLocalizedSurahName(1, lang);
    assert(!name.empty());
    std::string label = LocalizationUtils::getLocalizedSurahLabel(lang);
    assert(!label.empty());
}

void testRecitationUtils() {
    std::vector<VerseData> verses;
    VerseData v1 = makeSampleVerse();
    VerseData v2 = makeSampleVerse();
    v2.timestampFromMs = -500;
    v2.timestampToMs = 500;
    verses.push_back(v1);
    verses.push_back(v2);
    RecitationUtils::normalizeGaplessTimings(verses);
    assert(verses[1].timestampFromMs >= verses[0].timestampToMs);

    TimingEntry entry;
    entry.startMs = 0;
    entry.endMs = 1000;
    CLIOptions opts;
    AppConfig cfg = loadConfig((getProjectRoot() / "config.json").string(), opts);
    VerseData bismillah = RecitationUtils::buildBismillahFromTiming(entry, cfg, "audio.mp3");
    assert(bismillah.verseKey == "1:1");
    assert(bismillah.durationInSeconds > 0);
}

void testTimingParser() {
    std::string tmpFile = (fs::temp_directory_path() / "sample_timing.vtt").string();
    std::ofstream out(tmpFile);
    out << "WEBVTT\n\n";
    out << "1\n";
    out << "00:00:00.000 --> 00:00:02.000\n";
    out << "1. In the name of Allah\n";
    out << "\n";
    out.close();

    auto timings = TimingParser::parseTimingFile(tmpFile);
    assert(!timings.byKey.empty());
    assert(!timings.ordered.empty());
    fs::remove(tmpFile);
}

void testSubtitleBuilder() {
    CLIOptions opts;
    opts.surah = 1;
    opts.from = 1;
    opts.to = 1;
    AppConfig cfg = loadConfig((getProjectRoot() / "config.json").string(), opts);
    std::vector<VerseData> verses = {makeSampleVerse()};
    std::string assPath = SubtitleBuilder::buildAssFile(cfg, opts, verses, cfg.introDuration, cfg.pauseAfterIntroDuration);
    assert(fs::exists(assPath));
}

int main() {
    fs::current_path(getProjectRoot());
    testConfigLoader();
    testCacheUtils();
    testLocalization();
    testRecitationUtils();
    testTimingParser();
    testSubtitleBuilder();
    std::cout << "All unit tests passed.\n";
    return 0;
}
