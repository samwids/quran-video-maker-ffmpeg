#pragma once

#include <string>
#include "types.h"

AppConfig loadConfig(const std::string& path, CLIOptions& options);
void validateAssets(const AppConfig& config);
