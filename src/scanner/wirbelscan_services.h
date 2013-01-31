/*
 * wirbelscan_services.h
 *
 * Copyright (C) 2010 Winfried Koehler 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 * Or, point your browser to http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * The author can be reached at: handygewinnspiel AT gmx DOT de
 *
 * The project's page is http://wirbel.htpc-forum.de/wirbelscan/index2.html
 */

#ifndef __WIRBELSCAN_SERVICES_H__
#define __WIRBELSCAN_SERVICES_H__


/********************************************************************
 *
 * wirbelscans plugin service interface
 *
 * see http://wirbel.htpc-forum.de/wirbelscan/vdr-servdemo-0.0.1.tgz
 * for example on usage.
 *
 *******************************************************************/

namespace WIRBELSCAN_SERVICE { 
/* begin of namespace. to use this header file:
 * #include "../wirbelscan/wirbelscan_services.h"
 * using namespace WIRBELSCAN_SERVICE;
 */

/* --- service(s) version ----------------------------------------------------
 */

#define SPlugin  "wirbelscan_"     // prefix
#define SInfo    "GetVersion"      // plugin version and service api
#define SCommand "DoCmd#0001"      // command api 0001
#define SStatus  "Status#0002"     // query status
#define SSetup   "Setup#0001"      // get/set setup, GetSetup#XXXX/SetSetup#XXXX
#define SCountry "Country#0001"    // get list of country IDs and Names
#define SSat     "Sat#0001"        // get list of satellite IDs and Names
#define SUser    "User#0002"       // get/set single user transponder, GetUser#XXXX/SetUser#XXXX

/* --- wirbelscan_GetVersion -------------------------------------------------
 * Query wirbelscans versions, will fail only if plugin version doesnt support service at all.
 */

typedef struct {
  const char * PluginVersion;                    // plugin version
  const char * CommandVersion;                   // commands service version
  const char * StatusVersion;                    // status service version
  const char * SetupVersion;                     // get/put setup service version
  const char * CountryVersion;                   // country ID list version
  const char * SatVersion;                       // satellite ID list version
  const char * UserVersion;                      // user transponder api version, 0.0.5-pre12b or higher.
  const char * reserved2;                        // reserved, dont use.
  const char * reserved3;                        // reserved, dont use.
} cWirbelscanInfo;

/* --- wirbelscan_DoCmd ------------------------------------------------------
 * Execute commands.
 */

typedef enum {
  CmdStartScan = 0,                              // start scanning
  CmdStopScan  = 1,                              // stop scanning
  CmdStore     = 2,                              // store current setup
} s_cmd;

typedef struct {
  s_cmd cmd;                                     // see above.
  bool replycode;                                // false, if unsuccessful.
} cWirbelscanCmd;

/* --- wirbelscan_Status -----------------------------------------------------
 * Query Status. Use this to build up your osd information displayed to user.
 */

typedef enum {
  StatusUnknown  = 0,                            // no status information available, try again later.
  StatusScanning = 1,                            // scan in progress.
  StatusStopped  = 2,                            // no scan in progress (not started, finished or stopped).
  StatusBusy     = 3,                            // plugin is busy, try again later.
} cStatus;

typedef struct {
  cStatus status;                                // see above.
  char curr_device[256];                         // name of current device. meaningless, if (status != StatusScanning)
  uint16_t progress;                             // progress by means of "percent of predefined transponders". NOTE: will differ in terms of time. meaningless, if (status != StatusScanning)
  uint16_t strength;                             // current signal strength as reported from device. NOTE: updated only after switching to new transponder. meaningless, if (status != StatusScanning)
  char transponder[256];                         // current transponder. meaningless, if (status != StatusScanning)
  uint16_t numChannels;                          // current number of (all) channels, including those which are new. meaningless, if (status != StatusScanning)
  uint16_t newChannels;                          // number of channels found during this scan. meaningless, if (status != StatusScanning)
  uint16_t nextTransponders;                     // number of transponders still to be scanned from NIT on this transponder. meaningless, if (status != StatusScanning)
  uint16_t reserved2;                            // reserved, dont use.
  uint16_t reserved3;                            // reserved, dont use.
} cWirbelscanStatus;

/* --- wirbelscan_GetSetup, wirbelscan_SetSetup ------------------------------
 * Get/Set Setup. Use this to build up your setup osd displayed to user.
 */

typedef struct {
  uint16_t  verbosity;                           // 0 (errors only) .. 5 (extended debug); default = 3 (messages)
  uint16_t  logFile;                             // 0 = off, 1 = stdout, 2 = syslog
  uint16_t DVB_Type;                             // DVB-T = 0, DVB-C = 1, DVB-S/S2 = 2, PVRINPUT = 3, PVRINPUT(FM Radio) = 4, ATSC = 5, TRANSPONDER = 999
  uint16_t DVBT_Inversion;                       // AUTO/OFF = 0, AUTO/ON = 1
  uint16_t DVBC_Inversion;                       // AUTO/OFF = 0, AUTO/ON = 1
  uint16_t DVBC_Symbolrate;                      // careful here - may change. AUTO = 0, 6900 = 1, 6875 = 2  (...)  14 = 5483, 15 = ALL
  uint16_t DVBC_QAM;                             // AUTO = 0,QAM64 = 1, QAM128 = 2, QAM256 = 3, ALL = 4
  uint16_t CountryId;                            // the id according to country, found in country list,   see wirbelscan_GetCountry
  uint16_t SatId;                                // the id according to satellite, found in list,         see wirbelscan_GetSat
  uint32_t scanflags;                            // bitwise flag of wanted channels: TV = (1 << 0), RADIO = (1 << 1), FTA = (1 << 2), SCRAMBLED = (1 << 4), HDTV = (1 << 5)
  uint16_t ATSC_type;                            // VSB = 0, QAM = 1, both VSB+QAM = 2
  uint16_t stuffing[6];                          // dont use.
} cWirbelscanScanSetup;

/* --- wirbelscan_GetCountry, wirbelscan_GetSat ------------------------------
 * Use this to build up your setup OSD - user needs to choose satellite and
 * country by name and assign correct IDs to setup, see
 *    * cWirbelscanScanSetup::CountryId
 *    * cWirbelscanScanSetup::SatId.
 *
 * 1) Query needed buffer size with cPreAllocBuffer::size == 0
 * 2) allocate memory in your plugin, to be at least: cPreAllocBuffer::size * sizeof(SListItem)
 * 3) second call fills buffer with count * sizeof(SListItem) bytes.
 * 4) access to items as SListItem*
 */

typedef struct {
  int id;
  char short_name[8];
  char full_name[64];
} SListItem;

typedef struct {
  uint32_t size;
  uint32_t count;
  SListItem * buffer;
} cPreAllocBuffer;

/* --- wirbelscan_GetUser, wirbelscan_SetUser --------------------------------
 * Scan a user defined Transponder. Service() expects a pointer to uint32_t Data[3];
 * Data should be initialized and read using class cUserTransponder.
 *
 * ---------------------------------------------
 * id            : 9  country-id/sat-id
 * frequency     : 21 DVB-T: 177500..858000; ATSC/DVB-C: 73000..858000, DVB-S/S2: 2000..15000
 * polarisation  : 2  0=h, 1=v, 2=l, 3=r
 * type          : 3  DVB_Type
 * symbolrate    : 17 i.e. 27500, 6900
 * fec_hi        : 4  DVB-S/S2: 1=1/2, 2=2/3, 3=3/4, 4=4/5, 5=5/6, 6=6/7, 7=7/8, 8=8/9, 9=forbidden, 10=3/5, 11=9/10
 *                    DVB-T     1=1/2, 2=2/3, 3=3/4, 4=forbidden, 5=5/6, 6=forbidden, 7=7/8, 8..15=forbidden
 * fec_lo        : 4  DVB-T     1=1/2, 2=2/3, 3=3/4, 4=forbidden, 5=5/6, 6=forbidden, 7=7/8, 8..15=forbidden
 * modulation    : 4  DVB-C: 0=forbidden, 1=QAM16, 2=QAM32, 3=QAM64, 4=QAM128, 5=QAM256, 6..15: forbidden
 *                    DVB-T: 0=QPSK, 1=QAM16, 2=forbidden, 3=QAM64, 4..15: forbidden
 *                    DVB-S: 0=QPSK, 1=QAM16, 2..8: forbidden, 9=PSK8, 10..15:forbidden
 *                    ATSC:  0..2: forbidden, 3=QAM64, 4=forbidden, 5=QAM256, 6=QAM_AUTO, 7=VSB8, 8=VSB16, 9..15=forbidden                         
 * orbit         : 12 i.e. 192 for S19E2
 * we_flag       : 1  0=west, 1=east
 * rolloff       : 2  0=0,35, 1=0,25, 2=0,20
 * satsystem     : 1  0=DVB-S, 1=DVB-S2
 * bw            : 2  0=8MHz, 1=7MHz, 2=6MHz, 3=5MHz
 * priority      : 1  0=LP, 1=HP
 * hierarchy     : 4  0=OFF, 1=alpha1, 2=alpha2, 3=alpha4
 * guardinterval : 2  0=1/32, 1=1/16, 2=1/8, 3=1/4
 * transmission  : 2  0=2k, 1=8k, 2=4k
 * inversion     : 1  0=OFF, 1=ON
 * use_nit       : 1  0=OFF, 1=ON
 * reserved      : 3  always 0
 * ---------------------------------------------
 */

#if ! defined(uint32_t) || ! defined(uint8_t)
#include <stdint.h>
#endif

#define P(v,b,p) ((v & ((1 << b) -1)) << p)
#define G(v,b,p) ((v >> p) & ((1 << b) -1))

class cUserTransponder {
  private:
  uint32_t data[3];
  public:
  ~cUserTransponder() { };
  cUserTransponder(uint32_t * Data) {
     data[0] = *(Data + 0);
     data[1] = *(Data + 1);
     data[2] = *(Data + 2);
     };

  // DVB-T 
  cUserTransponder(uint8_t id, uint32_t frequency, uint8_t modulation, uint8_t fec_hp, uint8_t fec_lp, uint8_t bw,
                   uint8_t priority, uint8_t hierarchy, uint8_t guard, uint8_t tm, uint8_t inversion, uint8_t use_nit) {
     data[0] = P(id,9,23) | P(frequency,21,2);
     data[1] = P(fec_hp,4,8) | P(fec_lp,4,4) | P(modulation,4,0);
     data[2] = P(bw,2,14) | P(priority,1,13) | P(hierarchy,4,9) | P(guard,2,7) | P(tm,2,5) | P(inversion,1,4) | P(use_nit,1,3);
     };

  // DVB-C
  cUserTransponder(uint8_t id, uint32_t frequency, uint32_t symbolrate, uint8_t modulation, uint8_t inversion, uint8_t use_nit) {
     data[0] = P(id,9,23) | P(frequency,21,2);
     data[1] = P(1,3,29) | P(symbolrate,17,12) | P(modulation,4,0);
     data[2] = P(inversion,1,4) | P(use_nit,1,3);
     };

  // DVB-S 
  cUserTransponder(uint8_t id, uint8_t system, uint32_t frequency, uint8_t polarisation, uint32_t symbolrate,
                   uint8_t modulation, uint8_t fec, uint16_t orbit, uint8_t we_flag, uint8_t rolloff, uint8_t use_nit) { 
     data[0] = P(id,9,23) | P(frequency,21,2) | P(polarisation,2,0);
     data[1] = P(2,3,29) | P(symbolrate,17,12) | P(fec,4,8) | P(modulation,4,0);
     data[2] = P(orbit,12,20) | P(we_flag,1,19) | P(rolloff,2,17) | P(system,1,16) | P(use_nit,1,3);
     };

  // ATSC
  cUserTransponder(uint8_t id, uint32_t frequency, uint8_t modulation, uint8_t use_nit) {
     data[0] = P(id,9,23) | P(frequency,21,2);
     data[1] = P(5,3,29) | P(modulation,4,0);
     data[2] = P(use_nit,1,3);
     };

  const uint32_t * Data(void) { return data; };
  int Id(void)          { return G(data[0],9,23); };
  int Frequency(void)   { return G(data[0],21,2); };
  int Polarisation(void){ return G(data[0],2,0); };
  int Type(void)        { return G(data[1],3,29); };
  int Symbolrate(void)  { return G(data[1],17,12); };
  int FecHP(void)       { return G(data[1],4,8); };
  int FecLP(void)       { return G(data[1],4,4); };
  int Modulation(void)  { return G(data[1],4,0); };
  int Orbit(void)       { return G(data[2],12,20); };
  int EastFlag(void)    { return G(data[2],1,19); };
  int Rolloff(void)     { return G(data[2],2,17); };
  int Satsystem(void)   { return G(data[2],1,16) + 5; };
  int Bandwidth(void)   { return (8 - G(data[2],2,14)) * (int) 1E6; };
  int Priority(void)    { return G(data[2],1,13); };
  int Hierarchy(void)   { return G(data[2],4,9); };
  int Guard(void)       { return G(data[2],2,7); };
  int Transmission(void){ return G(data[2],2,5); };
  int Inversion(void)   { return G(data[2],1,4); };
  int UseNit(void)      { return G(data[2],1,3); };
  bool IsTerr(void)     { return IsType(0); };
  bool IsCable(void)    { return IsType(1); };
  bool IsSat(void)      { return IsType(2); };
  bool IsAtsc(void)     { return IsType(5); };
  bool IsType(int type) { return type == Type(); };
 
};


} /* end of namespace, dont touch */
#endif
