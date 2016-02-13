#ifndef XVDR_ARTWORK_H
#define	XVDR_ARTWORK_H

#include "db/storage.h"
#include <string>

class cArtwork {
public:
  
  cArtwork();

  virtual ~cArtwork();

  bool get(int contentType, const std::string& title, std::string& posterUrl, std::string& backdropUrl);
  
  bool set(int contentType, const std::string& title, const std::string& posterUrl, const std::string& backdropUrl, int externalId);

  void cleanup(int afterDays = 4);

private:

  void CreateDB();
  
  XVDR::Storage& m_storage;

};

#endif	// XVDR_ARTWORK_H
