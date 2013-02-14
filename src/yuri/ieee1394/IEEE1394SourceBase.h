/*
 * IEEE1394InputBase.h
 *
 *  Created on: May 29, 2009
 *      Author: neneko
 */

#ifndef IEEE1394INPUTBASE_H_
#define IEEE1394INPUTBASE_H_

#include <yuri/io/BasicIOThread.h>
#include <yuri/exception/Exception.h>
#include <vector>
// Dclarations from libiec61883, so we don't have to include it here
//typedef struct raw1394_handle *raw1394handle_t;
//typedef uint16_t nodeid_t;
#include <libiec61883/iec61883.h>
namespace yuri {

namespace io {
using yuri::exception::Exception;
struct ieee1394_camera_info {
	int port;
	int node;
	uint64_t guid;
	std::string label;
};

class IEEE1394SourceBase:
public BasicIOThread
{
public:
	IEEE1394SourceBase(Log &log_,pThreadBase parent, nodeid_t node=0, int port = 0, uint64_t guid=-1, std::string id="IEEE1394");
	virtual ~IEEE1394SourceBase();
	virtual void run();
	bool initialized() { return device_ready; }
	int get_next_frame();
	//static int getNumberOfNodes(int port=0, int *local=0);
	//static int getNumberOfPorts();
	static int enumerateDevices(std::vector<ieee1394_camera_info> &devices);
protected:
	//static int receive_frame (unsigned char *data, int len, unsigned int complete, void *callback_data);
	//int process_frame(unsigned char *data, int len, int complete);
	virtual bool start_receiving() = 0;
	virtual bool stop_receiving() = 0;
	//static int busResetHandler(raw1394handle_t handle,  unsigned int generation);
	//void virtual setGeneration(unsigned int generation);
	virtual int findNodeByGuid(raw1394handle_t handle, uint64_t guid);
	protected:
	raw1394handle_t handle;
	nodeid_t node;
	int channel,port;
	int oplug, iplug, bandwidth;
	bool device_ready;
};

}

}
#endif /* IEEE1394INPUTBASE_H_ */