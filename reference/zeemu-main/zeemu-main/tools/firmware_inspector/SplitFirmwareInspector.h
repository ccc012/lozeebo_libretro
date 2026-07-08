#ifndef ZEEMU_SPLIT_FIRMWARE_INSPECTOR_H_
#define ZEEMU_SPLIT_FIRMWARE_INSPECTOR_H_

#include <iosfwd>
#include <string>

#include "cpu/cpu.h"

class ZEEMU_EXPORT SplitFirmwareInspector {
public:
    static bool inspect(const std::string& split_directory, std::ostream& out);
};

#endif
