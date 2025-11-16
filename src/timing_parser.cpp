#include "timing_parser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <algorithm>
#include <vector>
#include <cctype>
#include <optional>
#include <map>

namespace {
const std::map<std::string, char> kArabicDigits = {
    {"٠", '0'}, {"١", '1'}, {"٢", '2'}, {"٣", '3'}, {"٤", '4'},
    {"٥", '5'}, {"٦", '6'}, {"٧", '7'}, {"٨", '8'}, {"٩", '9'}
};

std::string strip_carriage_return(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

bool contains_arabic_letters(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t advance = 1;
        unsigned int codepoint = c;
        if ((c & 0x80) == 0) {
            advance = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            advance = 2;
            codepoint = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i+1]) & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            advance = 3;
            codepoint = ((c & 0x0F) << 12) |
                        ((static_cast<unsigned char>(text[i+1]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(text[i+2]) & 0x3F);
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            advance = 4;
            codepoint = ((c & 0x07) << 18) |
                        ((static_cast<unsigned char>(text[i+1]) & 0x3F) << 12) |
                        ((static_cast<unsigned char>(text[i+2]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(text[i+3]) & 0x3F);
        }

        if ((codepoint >= 0x0600 && codepoint <= 0x06FF) ||
            (codepoint >= 0x0750 && codepoint <= 0x077F) ||
            (codepoint >= 0x08A0 && codepoint <= 0x08FF)) {
            return true;
        }
        i += advance;
    }
    return false;
}

std::string convert_arabic_digits_to_ascii(const std::string& text) {
    std::string converted;
    converted.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0) {
            converted.push_back(static_cast<char>(c));
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            advance = 2;
            std::string utf8_char = text.substr(i, advance);
            auto it = kArabicDigits.find(utf8_char);
            if (it != kArabicDigits.end()) {
                converted.push_back(it->second);
            } else {
                converted.append(utf8_char);
            }
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            advance = 3;
            std::string utf8_char = text.substr(i, advance);
            auto it = kArabicDigits.find(utf8_char);
            if (it != kArabicDigits.end()) {
                converted.push_back(it->second);
            } else {
                converted.append(utf8_char);
            }
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            advance = 4;
            std::string utf8_char = text.substr(i, advance);
            auto it = kArabicDigits.find(utf8_char);
            if (it != kArabicDigits.end()) {
                converted.push_back(it->second);
            } else {
                converted.append(utf8_char);
            }
        } else {
            converted.push_back(static_cast<char>(c));
        }
        i += advance;
    }
    return converted;
}

bool contains_bismillah_phrase(const std::string& text) {
    if (text.empty()) return false;
    static const std::vector<std::string> markers = {
        "\xEF\xB7\xBD", // ﷽ ligature
        "بِسْمِ",
        "بسم الله",
        "بسم",
        "In the name of Allah",
        "in the name of allah"
    };

    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    for (const auto& marker : markers) {
        if (text.find(marker) != std::string::npos) return true;
        if (lowered.find(marker) != std::string::npos) return true;
    }
    return false;
}

std::optional<std::string> extract_explicit_verse_key(const std::string& line) {
    std::string converted = convert_arabic_digits_to_ascii(line);
    static const std::regex ref_regex(R"((\d+)\s*[:：]\s*(\d+))");
    std::smatch match;
    if (std::regex_search(converted, match, ref_regex)) {
        return match[1].str() + ":" + match[2].str();
    }
    return std::nullopt;
}

std::optional<int> extract_verse_number(const std::string& line) {
    std::string converted = convert_arabic_digits_to_ascii(line);
    static const std::regex number_regex(R"((\d+))");
    std::smatch match;
    if (std::regex_search(converted, match, number_regex)) {
        try {
            return std::stoi(match[1]);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}
} // namespace

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

TimingParseResult parseTimingFile(const std::string& filepath) {
    TimingParseResult result;
    auto& timings = result.byKey;
    auto& ordered = result.ordered;
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
    int sequentialIndex = 0;

    while (std::getline(file, line)) {
        line = strip_carriage_return(line);
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

            std::vector<std::string> payload;
            while (std::getline(file, line)) {
                line = strip_carriage_return(line);
                if (line.empty()) break;
                payload.push_back(line);
            }

            std::string arabicText;
            std::vector<std::string> translationLines;
            arabicText.reserve(128);

            std::optional<std::string> explicitKey;
            std::optional<int> verseNumber;

            for (const auto& payloadLine : payload) {
                if (!explicitKey) {
                    explicitKey = extract_explicit_verse_key(payloadLine);
                }
                if (!verseNumber) {
                    verseNumber = extract_verse_number(payloadLine);
                }

                if (arabicText.empty() && contains_arabic_letters(payloadLine)) {
                    arabicText = payloadLine;
                } else if (arabicText.empty()) {
                    arabicText = payloadLine;
                } else {
                    translationLines.push_back(payloadLine);
                }
            }

            std::string translationText;
            for (size_t i = 0; i < translationLines.size(); ++i) {
                translationText += translationLines[i];
                if (i + 1 < translationLines.size()) {
                    translationText += " ";
                }
            }

            int resolvedVerseNumber = verseNumber.value_or(currentIndex);
            std::string verseKey = explicitKey.value_or("SURAH:" + std::to_string(resolvedVerseNumber));

            TimingEntry entry;
            entry.verseKey = verseKey;
            entry.startMs = timestampToMs(startTime);
            entry.endMs = timestampToMs(endTime);
            entry.text = arabicText;
            entry.translation = translationText;
            entry.verseNumber = resolvedVerseNumber;
            entry.isBismillah = contains_bismillah_phrase(arabicText) ||
                                contains_bismillah_phrase(translationText);
            entry.sequentialIndex = ++sequentialIndex;

            timings[verseKey] = entry;
            ordered.push_back(entry);
            currentIndex++;
        }
    }

    for (const auto& entry : ordered) {
        result.byVerseNumber[entry.verseNumber].push_back(entry);
    }

    std::cout << "Parsed " << ordered.size() << " timing entries from file." << std::endl;
    return result;
}

} // namespace TimingParser
