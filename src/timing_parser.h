#pragma once
#include <string>
#include <vector>
#include <map>

struct TimingEntry {
    std::string verseKey;
    int startMs;
    int endMs;
    std::string text;  // Arabic text from timing file (optional)
};

namespace TimingParser {
    // Parse VTT or SRT file and extract timing information
    std::map<std::string, TimingEntry> parseTimingFile(const std::string& filepath);
    
    // Helper to convert timestamp string to milliseconds
    int timestampToMs(const std::string& timestamp);
}