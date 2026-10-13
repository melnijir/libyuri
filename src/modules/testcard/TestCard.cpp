/*!
 * @file 		TestCard.cpp
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date		25.09.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "TestCard.h"
#include "yuri/core/Module.h"
#include "yuri/core/frame/raw_frame_params.h"
#include "yuri/core/frame/raw_frame_types.h"
#include "yuri/core/frame/RawVideoFrame.h"
namespace yuri {
namespace testcard {


IOTHREAD_GENERATOR(TestCard)

MODULE_REGISTRATION_BEGIN("testcard")
		REGISTER_IOTHREAD("testcard",TestCard)
MODULE_REGISTRATION_END()

namespace {
	std::vector<uint32_t> pattern_colors =
	{0xFF0000, 0x00FF00, 0x0000FF,
	 0xFF00FF, 0xFFFF00, 0x00FFFF};
}


core::Parameters TestCard::configure()
{
	core::Parameters p = core::IOThread::configure();
	p.set_description("TestCard");
	p["resolution"]["Test pattern resolution"]=resolution_t{800,600};
	p["fps"]["Framerate of the test pattern"]=25.0;
	p["format"]["Format of the test pattern"]="RGB";
	return p;
}


TestCard::TestCard(log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
core::IOThread(log_,parent,1,1,std::string("testcard")),resolution_(resolution_t{800, 600}),
fps_(25.0),format_(core::raw_format::yuyv422)
{
	IOTHREAD_INIT(parameters)
}

TestCard::~TestCard() noexcept
{
}

void TestCard::run()
{
	// The pattern does not change, generate it once
	core::pRawVideoFrame frame = core::RawVideoFrame::create_empty(core::raw_format::rgba32, resolution_, true);
	const size_t cnum = pattern_colors.size();
	auto it = PLANE_DATA(frame,0).begin();
	for (dimension_t line = 0; line < resolution_.height; ++line) {
		for (dimension_t color = 0; color < cnum; ++color) {
			uint32_t c = pattern_colors[color];
			for (dimension_t col = color * resolution_.width / cnum;
					col < (color+1) * resolution_.width / cnum; ++ col) {
					*it++ = (c&0xFF0000) >> 16;
					*it++ = (c&0x00FF00) >>  8;
					*it++ = (c&0x0000FF) >>  0;
					*it++ = 0xFF;
			}
		}
	}

	// Send frames at the requested rate (fps <= 0: as fast as possible)
	timestamp_t next_time{};
	while(still_running()) {
		if (fps_ > 0) {
			const auto curtime = timestamp_t{};
			if (next_time > curtime) {
				sleep(std::min(get_latency(), next_time - curtime));
				continue;
			}
			next_time = next_time + (1_s / fps_);
		}
		push_frame(0, frame);
	}
}

bool TestCard::set_param(const core::Parameter& param)
{
	if (assign_parameters(param)
			(resolution_, 	"resolution")
			(fps_,			"fps")
			.parsed<std::string>
				(format_,	"format", core::raw_format::parse_format))
		return true;
	return core::IOThread::set_param(param);
}

} /* namespace testcard */
} /* namespace yuri */
