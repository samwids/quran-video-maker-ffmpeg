#pragma once
#include <string>
#include <vector>
#include <map>
#include <deque>

struct TimingEntry {
    std::string verseKey;
    int startMs;
    int endMs;
    std::string text;  // Primary text from timing file (optional)
    std::string translation;
    bool isBismillah = false;
    int verseNumber = -1;
    int sequentialIndex = -1;
};

namespace TimingParser {
    struct TimingParseResult {
        std::map<std::string, TimingEntry> byKey;
        std::vector<TimingEntry> ordered;
        std::map<int, std::deque<TimingEntry>> byVerseNumber;
    };

    // Parse VTT or SRT file and extract timing information
    TimingParseResult parseTimingFile(const std::string& filepath);
    
    // Helper to convert timestamp string to milliseconds
    int timestampToMs(const std::string& timestamp);
}
