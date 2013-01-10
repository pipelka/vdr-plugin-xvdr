/*
 * VDR Channels proxy.
 */

#ifndef XVDRCHANNELS_H_
#define XVDRCHANNELS_H_

#include <vdr/channels.h>

class cXVDRChannels: public cRwLock {
private:
	cChannels *channels;
	uint64_t channelsHash;
	cChannels* Reorder(cChannels *channels);
	bool Read(FILE *f, cChannels *channels);
	bool Write(FILE *f, cChannels *channels);
	uint64_t ChannelsHash(cChannels *channels);
public:
	cXVDRChannels();

	/**
	 * Calculates the VDR Channels hash and compares with the cached value
	 * (channelsHash). If the value has changed an the ReorderCmd configuration
	 * parameter is specified - reorder the VDR Channels list with the ReorderCmd
	 * command and cache the reordered list.
	 *
	 * Returns the calculated hash value.
	 *
	 * TODO: Think about replacing hash with checksum.
	 */
	uint64_t CheckUpdates();

	/**
	 * Returns reference to either reordered list (if ReorderCmd is specified),
	 * or to the VDR Channels.
	 *
	 * NOTE: Lock before calling this method.
	 */
	cChannels* Get();

	/**
	 * Returns the channels hash calculated on the prev. CheckUpdates().
	 */
	uint64_t GetHash();

	/**
	 * Lock both this instance an the referencing channels list.
	 */
	bool Lock(bool Write, int TimeoutMs = 0);

	/**
	 * Unlock this instance an the referencing channels list.
	 */
	void Unlock(void);
};

extern cXVDRChannels XVDRChannels;

#endif /* XVDRCHANNELS_H_ */
