#ifndef XVDR_STORAGE_H
#define XVDR_STORAGE_H

#include "database.h"

namespace XVDR {

class Storage : public Database {
protected:

    Storage();

public:

    virtual ~Storage();

    static Storage& getInstance();
};

} // namespace XVDR

#endif // XVDR_STORAGE_H
