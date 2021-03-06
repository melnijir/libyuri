/*!
 * @file 		TSRtpStreamer.cpp
 * @author 		Zdenek Travnicek
 * @date 		15.8.2010
 * @date		16.2.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2010 - 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "TSRtpStreamer.h"
#include "yuri/core/Module.h"

namespace yuri {

namespace rtp {


REGISTER("ts_streamer",TSRtpStreamer)

core::pIOThread TSRtpStreamer::generate(log::Log &_log,core::pwThreadBase parent,core::Parameters& parameters)
{
	shared_ptr<TSRtpStreamer> yc (new TSRtpStreamer(_log,parent));
	yc->set_endpoint(parameters["address"].get<std::string>(),parameters["port"].get<yuri::size_t>());
	return yc;
}

core::pParameters TSRtpStreamer::configure()
{
	core::pParameters p (new core::Parameters());
	p->set_max_pipes(1,0);
	p->add_input_format(YURI_VIDEO_MPEGTS);
	// Just for now, let's define target as parameter
	(*p)["address"]["Adress to stream to"]=std::string();
	(*p)["port"]["Port to stream to"]="1234";
	return p;
}

TSRtpStreamer::TSRtpStreamer(log::Log &log_, core::pwThreadBase parent):
		IOThread(log_,parent,1,0,"TS RTP Streamer"),seq(0),pseq(0)
{
	latency=1;
}

TSRtpStreamer::~TSRtpStreamer()
{
	ptime end_time=microsec_clock::local_time();
	log[log::debug] << "Sent " << packets_sent << " in " << to_simple_string(end_time - first_packet)
			<< ". That make " << (double)(packets_sent)*1.0e6/(end_time - first_packet).total_microseconds() << " pkts/us" << std::endl;
}

bool TSRtpStreamer::set_endpoint(std::string address, yuri::size_t port)
{
	if (!socket) socket.reset(new asio::ASIOUDPSocket(log,get_this_ptr(),6666));
	return socket->set_endpoint(address,port);
}

bool TSRtpStreamer::step()
{
	if (!in[0] || in[0]->is_empty()) return true;
	core::pBasicFrame frame = in[0]->pop_frame();
	yuri::size_t header_size = sizeof(RTPPacket);
//	shared_array<yuri::ubyte_t> packet = allocate_memory_block(header_size+7*188);
	plane_t packet(header_size+7*188);
	RTPPacket *p = reinterpret_cast<RTPPacket*>(&packet[0]);
	memset(p,0,sizeof(RTPPacket));
	//p->version = 2;
	/*p->payload_type = 33;
	p->timestamp = seq++ *90000/25;
	p->SSRC = 0x3737;
	p->CSRC = 0x3737;
	p->CC = 1;*/
	unsigned hdr = 0x80000000;
	hdr |= 33 << 16;


	yuri::size_t remaining = PLANE_SIZE(frame,0);

	yuri::ubyte_t *data_start = &packet[0]+header_size;
	yuri::ubyte_t *data_ptr = PLANE_RAW_DATA(frame,0);
	if (first_packet == not_a_date_time) {
		first_packet = microsec_clock::local_time();
		packets_sent=0;
	}

	while (remaining) {
		//p->timestamp = htonl(pseq * 90000 / 25);
		yuri::uint_t ts = (microsec_clock::local_time()-first_packet).total_microseconds()*90/1000;
		//log[log::debug] << "ts: " << ts << std::endl;
		p->timestamp = htonl(ts);
		*reinterpret_cast<yuri::uint_t*>(p->bytes) = htonl(hdr|pseq++);

		/*p->bytes[2] = (pseq >> 8)&0xFF;
		p->bytes[3] = pseq++ & 0xFF;*/
		yuri::size_t usable=remaining>(7*188)?7*188:remaining;
		memcpy(data_start,data_ptr,usable);
		socket->write(&packet[0],usable+header_size);
		data_ptr+=usable;
		remaining-=usable;
	}
	packets_sent++;
	return true;

}


}

}
