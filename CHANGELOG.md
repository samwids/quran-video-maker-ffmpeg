# Changelog

All notable changes to this project will be documented in this file. This project follows [Semantic Versioning](https://semver.org) and takes inspiration from [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - 2025-11-22
### Added
- First public release of the `quran-video-maker-ffmpeg` CLI for producing Quran verse videos with synchronized Arabic text, translations, and recitations.
- Gapped rendering pipeline for surah ranges; gapless workflows supported only with user-supplied audio and timing files while built-in gapless data stays disabled.
- Custom recitation inputs via `--custom-audio` and `--custom-timing` (VTT/SRT) with automatic Bismillah insertion and verse-range trimming that keeps late verses aligned.
- Multi-language translations (English, Oromo, Amharic, Urdu) with localized surah labels, reciter names, numerals, and thumbnail/intro text.
- Styling and quality controls in `config.json` plus CLI overrides for fonts, padding, animation toggles, encoders, CRF/bitrate knobs, and named quality profiles.
- Text growth and fade animations with adaptive sizing/wrapping to keep long ayat on-screen.
- Thumbnail generation with randomized color themes and metadata embedding.
- Render metadata sidecars that capture the exact CLI invocation and config artifact, plus structured progress output when `--progress` is enabled.
- Audio download caching with flags to reuse or clear the cache; optional VideoToolbox hardware acceleration on macOS.
- Homebrew tap install (`brew install ashaltu/tap/qvm`) and a starter unit test suite covering config loading, timing parsing, and reciter utilities.

### Known limitations
- Built-in gapless datasets remain disabled pending timing cleanup; gapless renders require providing custom audio and timing files.
- Hardware acceleration is untested on Linux and Windows.
- Build/test coverage: verified on macOS; Linux builds have only light smoke testing; Windows builds and tests have not been done yet.
