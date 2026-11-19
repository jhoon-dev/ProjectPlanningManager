#pragma once
#include <string>

namespace ppm {
// Read-only aliases for files written before the PPM identifier migration.
// Engine policy/version suffixes are unchanged: renaming must not select a new algorithm.
inline std::string canonicalEngineIdentifier(std::string engine) {
    if (engine.starts_with("jobs-"))
        engine.replace(0, 5, "ppm-");
    return engine;
}
} // namespace ppm
