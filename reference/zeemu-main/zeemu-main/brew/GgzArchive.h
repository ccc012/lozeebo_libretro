#ifndef ZEEMU_BREW_GGZ_ARCHIVE_H_
#define ZEEMU_BREW_GGZ_ARCHIVE_H_

#include <filesystem>

class GgzArchive {
public:
    static bool extract_to_cache(const std::filesystem::path& archive_path);
};

#endif
