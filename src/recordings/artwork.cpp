/* 
 * File:   artwork.cpp
 * Author: pipelka
 * 
 * Created on 8. Januar 2016, 13:34
 */

#include "recordings/artwork.h"
#include "config/config.h"

cArtwork::cArtwork() : m_storage(XVDR::Storage::getInstance()) {
  CreateDB();
}

cArtwork::~cArtwork() {
}

void cArtwork::CreateDB() {
    std::string schema =
      "CREATE TABLE IF NOT EXISTS artwork (\n"
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
      "  contenttype INTEGER NOT NULL,\n"
      "  title TEXT NOT NULL,\n"
      "  externalid INTEGER,\n"
      "  posterurl TEXT,\n"
      "  backgroundurl TEXT\n,"
      "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL\n"
      ");\n"
      "CREATE INDEX IF NOT EXISTS artwork_externalid on artwork(externalid);\n"
      "CREATE UNIQUE INDEX IF NOT EXISTS artwork_content on artwork(contenttype, title);\n";

    if(m_storage.Exec(schema) != SQLITE_OK) {
    	ERRORLOG("Unable to create database schema for artwork");
    }
}

bool cArtwork::get(int contentType, const std::string& title, std::string& posterUrl, std::string& backdropUrl) {
  sqlite3_stmt* s = m_storage.Query(
    "SELECT posterurl, backgroundurl FROM artwork WHERE contenttype=%i AND title=%Q;",
    contentType,
    title.c_str());

  if(s == NULL) {
    return false;
  }

  if(sqlite3_step(s) != SQLITE_ROW) {
    sqlite3_finalize(s);
    return false;
  }

  posterUrl = (const char*)sqlite3_column_text(s, 0);
  backdropUrl = (const char*)sqlite3_column_text(s, 1);

  sqlite3_finalize(s);
  return true;
}

bool cArtwork::set(int contentType, const std::string& title, const std::string& posterUrl, const std::string& backdropUrl, int externalId = 0) {
  // try to insert new record
 if(m_storage.Exec(
    "INSERT OR IGNORE INTO artwork(contenttype, title, posterurl, backgroundurl, externalId) VALUES(%i, %Q, %Q, %Q, %i);",
    contentType,
    title.c_str(),
    posterUrl.c_str(),
    backdropUrl.c_str(),
    externalId) == SQLITE_OK) {
   return true;
 }
  
  return m_storage.Exec(
    "UPDATE artwork SET posterurl=%Q, backgroundurl=%Q, externalId=%i WHERE contenttype=%i AND title=%Q",
    posterUrl.c_str(),
    backdropUrl.c_str(),
    externalId,
    contentType,
    title.c_str()) == SQLITE_OK;
}
