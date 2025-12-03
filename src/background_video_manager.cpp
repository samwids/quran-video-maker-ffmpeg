#include "background_video_manager.h"
#include "r2_client.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "cache_utils.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;

namespace BackgroundVideo {

Manager::Manager(const AppConfig& config, const CLIOptions& options)
    : config_(config), options_(options) {
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    tempDir_ = fs::temp_directory_path() / ("qvm_bg_" + std::to_string(timestamp));
    fs::create_directories(tempDir_);
}

double Manager::getVideoDuration(const std::string& path) {
    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, path.c_str(), nullptr, nullptr) != 0) {
        return 0.0;
    }
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        avformat_close_input(&formatContext);
        return 0.0;
    }
    double duration = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
    avformat_close_input(&formatContext);
    return duration;
}

std::string Manager::getCachedVideoPath(const std::string& remoteKey) {
    // Use the same cache directory as audio
    cacheDir_ = CacheUtils::getCacheRoot() / "backgrounds";
    fs::create_directories(cacheDir_);
    
    // Convert remote key to safe filename
    std::string safeFilename = remoteKey;
    std::replace(safeFilename.begin(), safeFilename.end(), '/', '_');
    return (cacheDir_ / safeFilename).string();
}

bool Manager::isVideoCached(const std::string& remoteKey) {
    std::string cachedPath = getCachedVideoPath(remoteKey);
    return fs::exists(cachedPath) && fs::file_size(cachedPath) > 0;
}

void Manager::cacheVideo(const std::string& remoteKey, const std::string& localPath) {
    std::string cachePath = getCachedVideoPath(remoteKey);
    if (localPath != cachePath) {
        fs::copy_file(localPath, cachePath, fs::copy_options::overwrite_existing);
    }
}

std::string Manager::buildFilterComplex(double totalDurationSeconds, 
                                        std::vector<std::string>& outputInputFiles) {
    if (!config_.videoSelection.enableDynamicBackgrounds) {
        return "";  // Use default single input
    }

    try {
        std::cout << "Selecting dynamic background videos..." << std::endl;
        
        // Initialize components
        VideoSelector::Selector selector(
            config_.videoSelection.themeMetadataPath,
            config_.videoSelection.seed
        );
        
        R2::R2Config r2Config{
            config_.videoSelection.r2Endpoint,
            config_.videoSelection.r2AccessKey,
            config_.videoSelection.r2SecretKey,
            config_.videoSelection.r2Bucket,
            config_.videoSelection.usePublicBucket
        };
        R2::Client r2Client(r2Config);
        
        // Get verse range segments with time allocations
        auto verseRangeSegments = selector.getVerseRangeSegments(
            options_.surah, options_.from, options_.to
        );
        
        // Calculate absolute time boundaries for each range
        std::map<std::string, double> rangeStartTimes;
        std::map<std::string, double> rangeEndTimes;
        for (const auto& seg : verseRangeSegments) {
            rangeStartTimes[seg.rangeKey] = seg.startTimeFraction * totalDurationSeconds;
            rangeEndTimes[seg.rangeKey] = seg.endTimeFraction * totalDurationSeconds;
        }
        
        // Collect themes and cache videos
        std::set<std::string> allThemes;
        for (const auto& seg : verseRangeSegments) {
            allThemes.insert(seg.themes.begin(), seg.themes.end());
        }
        
        std::map<std::string, std::vector<std::string>> themeVideosCache;
        for (const auto& theme : allThemes) {
            try {
                themeVideosCache[theme] = r2Client.listVideosInTheme(theme);
            } catch (...) {
                themeVideosCache[theme] = {};
            }
        }
        
        // Build playlists
        for (const auto& seg : verseRangeSegments) {
            selector.getOrBuildPlaylist(seg, themeVideosCache, selectionState_);
        }
        
        // Collect video segments
        std::vector<VideoSegment> segments;
        double currentTime = 0.0;
        int segmentCount = 0;
        std::string currentRangeKey;
        const VideoSelector::VerseRangeSegment* currentRange = nullptr;
        
        while (currentTime < totalDurationSeconds) {
            segmentCount++;
            
            // Calculate reasonable safety segment limit based on duration
            int maxSegments = std::max(1000, static_cast<int>(totalDurationSeconds / 5.0));
            if (segmentCount > maxSegments) {
                std::cerr << "  Warning: Reached segment limit, stopping collection" << std::endl;
                break;
            }
            
            double timeFraction = currentTime / totalDurationSeconds;
            
            // Get the appropriate verse range segment for this time position
            const auto* newRange = selector.getRangeForTimePosition(verseRangeSegments, timeFraction);
            if (!newRange) break;
            
            // Check if we changed ranges
            if (currentRange != newRange) {
                if (currentRange != nullptr) {
                    std::cout << "  --- Transitioning from " << currentRange->rangeKey 
                              << " to " << newRange->rangeKey << " ---" << std::endl;
                }
                currentRange = newRange;
                currentRangeKey = newRange->rangeKey;
            }
            
            // Calculate time remaining for this range
            double rangeEndTime = rangeEndTimes[currentRangeKey];
            double timeRemainingInRange = rangeEndTime - currentTime;
            
            auto entry = selector.getNextVideoForRange(currentRangeKey, selectionState_);
            
            // Check cache first
            std::string localPath;
            if (isVideoCached(entry.videoKey)) {
                localPath = getCachedVideoPath(entry.videoKey);
                std::cout << "  Using cached: " << fs::path(entry.videoKey).filename() << std::endl;
            } else {
                // Download to temp then cache
                fs::path tempPath = tempDir_ / fs::path(entry.videoKey).filename();
                try {
                    localPath = r2Client.downloadVideo(entry.videoKey, tempPath);
                    cacheVideo(entry.videoKey, localPath);
                    tempFiles_.push_back(tempPath);
                } catch (const std::exception& e) {
                    std::cerr << "  Download failed: " << e.what() << std::endl;
                    continue;
                }
            }
            
            double duration = getVideoDuration(localPath);
            if (duration <= 0) {
                std::cerr << "  Invalid duration for video, skipping" << std::endl;
                continue;
            }
            
            VideoSegment segment;
            segment.path = localPath;
            segment.theme = entry.theme;
            segment.duration = duration;
            segment.isLocal = true;
            segment.needsTrim = false;
            segment.trimmedDuration = duration;
            
            // Check if this video would extend beyond the current range
            if (currentTime + duration > rangeEndTime && timeRemainingInRange > 0.5) {
                // This video would cross into the next range - trim it
                segment.needsTrim = true;
                segment.trimmedDuration = timeRemainingInRange;
                std::cout << "  Trimming video from " << duration << "s to " 
                          << segment.trimmedDuration << "s to fit range boundary" << std::endl;
            }
            
            // Also check if it would exceed total duration
            if (currentTime + segment.trimmedDuration > totalDurationSeconds) {
                segment.needsTrim = true;
                segment.trimmedDuration = totalDurationSeconds - currentTime;
                std::cout << "  Trimming video to " << segment.trimmedDuration 
                          << "s to match total duration" << std::endl;
            }
            
            segments.push_back(segment);
            outputInputFiles.push_back(localPath);
            currentTime += segment.trimmedDuration;
        }
        
        if (segments.empty()) {
            std::cerr << "Warning: No video segments collected" << std::endl;
            return "";
        }
        
        // Build concat filter
        std::ostringstream filter;
        
        // First, scale and trim all inputs to same size
        for (size_t i = 0; i < segments.size(); ++i) {
            filter << "[" << i << ":v]";
            
            // Trim if needed
            if (segments[i].needsTrim) {
                filter << "trim=duration=" << segments[i].trimmedDuration << ",setpts=PTS-STARTPTS,";
            }
            
            filter << "scale=" << config_.width << ":" << config_.height 
                   << ",setsar=1[v" << i << "]; ";
        }
        
        // Then concat them
        for (size_t i = 0; i < segments.size(); ++i) {
            filter << "[v" << i << "]";
        }
        filter << "concat=n=" << segments.size() << ":v=1:a=0[bg]; ";
        filter << "[bg]setpts=PTS-STARTPTS";
        
        std::cout << "  Selected " << segments.size() << " video segments, total duration: " 
                  << currentTime << " seconds" << std::endl;
        return filter.str();
        
    } catch (const std::exception& e) {
        std::cerr << "Warning: Dynamic background selection failed: " << e.what() << std::endl;
        return "";
    }
}

void Manager::cleanup() {
    for (const auto& file : tempFiles_) {
        std::error_code ec;
        fs::remove(file, ec);
    }
    if (fs::exists(tempDir_)) {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }
}

} // namespace BackgroundVideo