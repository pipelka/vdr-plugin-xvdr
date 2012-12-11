/*
 *      XVDR Service Reference Tool
 *
 *      Copyright (C) 2012 Alexander Pipelka
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <iostream>

static std::string filename = "/etc/vdr/channels.conf";

// helper function stolen from vdr

enum eSourceType {
	stNone  = 0x00000000,
	stAtsc  = ('A' << 24),
	stCable = ('C' << 24),
	stSat   = ('S' << 24),
	stTerr  = ('T' << 24),
	st_Mask = 0xFF000000,
	st_Pos  = 0x0000FFFF,
};

int CodeFromString(const char* s) {
	if(s[0] != 0) {
		if('A' <= *s && *s <= 'Z') {
			int code = int(*s) << 24;

			if(code == stSat) {
				int pos = 0;
				bool dot = false;
				bool neg = false;

				while(*++s) {
					switch(*s) {
						case '0' ... '9':
							pos *= 10;
							pos += *s - '0';
							break;
						case '.':
							dot = true;
							break;
						case 'E':
							neg = true; // fall through to 'W'
						case 'W':

							if(!dot) {
								pos *= 10;
							}

							break;
						default:
							return stNone;
					}
				}

				if(neg) {
					pos = -pos;
				}

				code |= (pos & st_Pos);
			}

			return code;
		}
	}

	return stNone;
}

std::string CreateServiceReference(char* source, int frequency, char* vpid, int sid, int nid, int tid) {
	uint32_t hash;

	// usually (on Enigma) the frequency is part of the namespace
	// for picons this seems to be unused, so disable it by now
	frequency = 0;

	if(source[0] == 'S') {
		int code = CodeFromString(source) & st_Pos;

		if(code > 0x00007FFF) {
			code |= 0xFFFF0000;
		}

		if(code < 0) {
			code = -code;
		}
		else {
			code = 1800 + code;
		}

		hash = code << 16 | ((frequency / 1000) & 0xFFFF);
	}
	else if(source[0] == 'C') {
		hash = 0xFFFF0000 | ((frequency / 1000) & 0xFFFF);
	}
	else if(source[0] == 'T') {
		hash = 0xEEEE0000 | ((frequency / 1000000) & 0xFFFF);
	}

	int type = 1;

	if(strcmp(vpid, "0") == 0 || strcmp(vpid, "1") == 0) {
		type = 2;
	}
	else {
		int pid = 0;
		int streamtype = 0;
		sscanf(vpid, "%d =%d", &pid, &streamtype);

		if(streamtype == 2) {
			type = 1;
		}
		else if(streamtype == 27) {
			type = 19;
		}
	}

	char ref[50];
	snprintf(ref, sizeof(ref), "1_0_%i_%X_%X_%X_%X_0_0_0",
	         type,
	         sid,
	         tid,
	         nid,
	         hash);

	return ref;
}

int main(int argc, char* argv[]) {

	if(argc == 2) {
		filename = argv[1];
	}

	FILE* f = fopen(filename.c_str(), "r");

	if(f == NULL) {
		std::cerr << "Unable to open : " << filename << std::endl;
		return 1;
	}

	bool bDone = false;

	while(!bDone) {
		// channels.conf
		//  1 - Name (string)
		//  2 - Frequency (int)
		//  3 - Parameters (string)
		//  4 - Source (string)
		//  5 - Symbolrate (int)
		//  6 - Vpid (string)
		//  7 - APid (string)
		//  8 - Tpid (string)
		//  9 - CAID (string)
		// 10 - SID (int)
		// 11 - NID (int)
		// 12 - TID (int)
		// 13 - RID (int)

		char* line = NULL;
		char* name = NULL;
		int freq = 0;
		char* parameters;
		char* source = NULL;
		int symbolrate = 0;
		char* vpid = NULL;
		char* apid = NULL;
		char* tpid = NULL;
		char* caid = NULL;
		int sid = 0;
		int nid = 0;
		int tid = 0;
		int rid = 0;

		size_t n = 0;

		if(getline(&line, &n, f) == -1) {
			break;
		}

		if(line[0] == ':') {
			continue;
		}

		if(sscanf(line, "%a[^:]:%d :%a[^:]:%a[^:] :%d :%a[^:]:%a[^:]:%a[^:]:%a[^:]:%d :%d :%d :%d ",
		          &name,
		          &freq,
		          &parameters,
		          &source,
		          &symbolrate,
		          &vpid,
		          &apid,
		          &tpid,
		          &caid,
		          &sid,
		          &nid,
		          &tid,
		          &rid
		         ) == EOF) {
			break;
		}

		if(name == NULL || freq == 0 || source == NULL || vpid == NULL || sid == 0 || nid == 0 || tid == 0) {
			continue;
		}

		std::cout << name << " - " << CreateServiceReference(source, freq, vpid, sid, nid, tid) << std::endl;

		free(name);
		free(parameters);
		free(source);
		free(vpid);
		free(apid);
		free(tpid);
		free(caid);
		free(line);
	}

	fclose(f);
}
