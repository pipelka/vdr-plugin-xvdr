#include "storage.h"
#include "config/config.h"
#include "vdr/tools.h"

using namespace XVDR;

Storage::Storage() {
    cString filename = AddDirectory(XVDRServerConfig.CacheDirectory, STORAGE_DB_FILE);
    if(!Open((const char*)filename)) {
	ERRORLOG("Unable to open database: '%s' - strange thing will happen !", (const char*)filename);
    }
}

Storage::~Storage() {
    Close();
}

Storage& Storage::getInstance() {
    static Storage instance;
    return instance;
}
