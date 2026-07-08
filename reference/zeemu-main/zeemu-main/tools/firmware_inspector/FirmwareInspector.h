#ifndef ZEEMU_FIRMWARE_INSPECTOR_H_
#define ZEEMU_FIRMWARE_INSPECTOR_H_

#include <iosfwd>
#include <string>

#include "cpu/cpu.h"

class ZEEMU_EXPORT FirmwareInspector {
public:
    static bool inspect(const std::string& firmware_path, std::ostream& out);
};

#endif
