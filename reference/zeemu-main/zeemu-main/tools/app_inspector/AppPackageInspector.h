#ifndef ZEEMU_APP_PACKAGE_INSPECTOR_H_
#define ZEEMU_APP_PACKAGE_INSPECTOR_H_

#include <iosfwd>
#include <string>

#include "cpu/cpu.h"

class ZEEMU_EXPORT AppPackageInspector {
public:
    static bool inspect(const std::string& app_directory, std::ostream& out);
};

#endif
