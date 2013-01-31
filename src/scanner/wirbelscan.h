/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2013 Alexander Pipelka
 *
 *      https://github.com/pipelka/vdr-plugin-xvdr
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_WIRBELSCAN_H
#define XVDR_WIRBELSCAN_H

#include <stdint.h>
#include <list>
#include <vdr/plugin.h>

#include "wirbelscan_services.h"

class cWirbelScan {
public:

  typedef std::list<WIRBELSCAN_SERVICE::SListItem> List;

  cWirbelScan();

  virtual ~cWirbelScan();

  bool Connect();

  bool GetVersion(WIRBELSCAN_SERVICE::cWirbelscanInfo* info);

  bool DoCmd(WIRBELSCAN_SERVICE::cWirbelscanCmd& cmd);

  bool GetStatus(WIRBELSCAN_SERVICE::cWirbelscanStatus& status);

  bool GetSetup(WIRBELSCAN_SERVICE::cWirbelscanScanSetup& param);

  bool SetSetup(WIRBELSCAN_SERVICE::cWirbelscanScanSetup& param);

  bool GetCountry(List& list);

  bool GetSat(List& list);

  bool IsScanning();

  // get/set user are currently unused
  // always returning false

  bool GetUser(WIRBELSCAN_SERVICE::cUserTransponder& transponder);

  bool SetUser(WIRBELSCAN_SERVICE::cUserTransponder& transponder);

protected:

  cPlugin* m_plugin;

};

#endif // XVDR_WIRBELSCAN_H
