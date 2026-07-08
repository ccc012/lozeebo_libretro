#ifndef ZEEMU_PE_LOADER_H__
#define ZEEMU_PE_LOADER_H__

#include "ExecutableLoader.h"

class ZEEMU_EXPORT PELoader: public ExecutableLoader {
public:
	PELoader() {};
	virtual bool load(std::istream &executable, Memory *memory, addr_t& addr);
    ~PELoader() {};
};

#endif
