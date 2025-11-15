#include "timing_parser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <algorithm>

namespace TimingParser {

int timestampToMs(const std::string& timestamp) {
    // Handle both VTT (00:00:00.000) and SRT (00:00:00,000) formats
    std::string ts = timestamp;
    std::replace(ts.begin(), ts.end(), ',', '.');

    std::regex ts_regex(R"((\d{1,2}):(\d{2}):(\d{2})\.(\d{3}))");
    std::smatch match;

    if (std::regex_search(ts, match, ts_regex)) {
        int hours = std::stoi(match[1]);
        int minutes = std::stoi(match[2]);
        int seconds = std::stoi(match[3]);
        int milliseconds = std::stoi(match[4]);

        return hours * 3600000 + minutes * 60000 + seconds * 1000 + milliseconds;
    }

    return 0;
}

std::map<std::string, TimingEntry> parseTimingFile(const std::string& filepath) {
    std::map<std::string, TimingEntry> timings;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open timing file: " + filepath);
    }

    // Detect format (VTT starts with "WEBVTT", SRT starts with numbers)
    std::string firstLine;
    std::getline(file, firstLine);
    bool isVTT = (firstLine.find("WEBVTT") != std::string::npos);

    if (!isVTT) {
        file.clear();
        file.seekg(0);
    }

    std::string line;
    int currentIndex = 0;

    // Arabic to Western numeral map (UTF-8 strings)
    std::map<std::string, char> arabic_to_western = {
        {"٠", '0'}, {"١", '1'}, {"٢", '2'}, {"٣", '3'}, {"٤", '4'},
        {"٥", '5'}, {"٦", '6'}, {"٧", '7'}, {"٨", '8'}, {"٩", '9'}
    };

    while (std::getline(file, line)) {
        if (line.empty() || line.find("WEBVTT") != std::string::npos) continue;

        // Check if this is a sequence number (SRT)
        if (std::regex_match(line, std::regex(R"(^\d+$)"))) {
            currentIndex = std::stoi(line);
            continue;
        }

        // Timestamp line
        std::regex timestamp_regex(R"((\d{2}:\d{2}:\d{2}[.,]\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2}[.,]\d{3}))");
        std::smatch match;

        if (std::regex_search(line, match, timestamp_regex)) {
            std::string startTime = match[1];
            std::string endTime = match[2];

            std::string arabicText;
            std::string translationText;

            while (std::getline(file, line) && !line.empty()) {
                if (arabicText.empty()) {
                    arabicText = line;
                } else {
                    translationText += line + " ";
                }
            }

            // Extract Arabic verse number
            std::regex verse_regex(R"([٠-٩]+)");
            std::smatch verse_match;
            std::string verseNumArabic;

            if (std::regex_search(arabicText, verse_match, verse_regex)) {
                verseNumArabic = verse_match[0];
            }

            // Convert UTF-8 Arabic numerals to Western
            std::string verseNumWestern;
            for (size_t i = 0; i < verseNumArabic.size();) {
                unsigned char c = verseNumArabic[i];
                std::string utf8Char;
                if ((c & 0x80) == 0) { // 1-byte
                    utf8Char = verseNumArabic.substr(i, 1);
                    i += 1;
                } else if ((c & 0xE0) == 0xC0) { // 2-byte
                    utf8Char = verseNumArabic.substr(i, 2);
                    i += 2;
                } else if ((c & 0xF0) == 0xE0) { // 3-byte
                    utf8Char = verseNumArabic.substr(i, 3);
                    i += 3;
                } else { // 4-byte
                    utf8Char = verseNumArabic.substr(i, 4);
                    i += 4;
                }

                if (arabic_to_western.count(utf8Char)) {
                    verseNumWestern += arabic_to_western[utf8Char];
                }
            }

            int verseNum = verseNumWestern.empty() ? currentIndex : std::stoi(verseNumWestern);
            std::string verseKey = "SURAH:" + std::to_string(verseNum);

            TimingEntry entry;
            entry.verseKey = verseKey;
            entry.startMs = timestampToMs(startTime);
            entry.endMs = timestampToMs(endTime);
            entry.text = arabicText;

            timings[verseKey] = entry;
            currentIndex++;
        }
    }

    std::cout << "Parsed " << timings.size() << " timing entries from file." << std::endl;
    return timings;
}

} // namespace TimingParser
