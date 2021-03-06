/*!
 * @file 		${class_name}.cpp
 * @author 		${user}
 * @date 		${date}
 * @copyright	Institute of Intermedia, CTU in Prague, ${year}
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#include "${class_name}.h"
#include "yuri/core/Module.h"

namespace yuri {
namespace ${namespace} {


IOTHREAD_GENERATOR(${class_name})

MODULE_REGISTRATION_BEGIN("${module}")
		REGISTER_IOTHREAD("${module}",${class_name})
MODULE_REGISTRATION_END()

core::Parameters ${class_name}::configure()
{
	core::Parameters p = core::IOThread::configure();
	p.set_description("${class_name}");
	return p;
}


${class_name}::${class_name}(const log::Log &log_, core::pwThreadBase parent, const core::Parameters &parameters):
core::IOThread(log_,parent,1,1,std::string("${module}"))
{
	IOTHREAD_INIT(parameters)
}

${class_name}::~${class_name}() noexcept
{
}

bool ${class_name}::step()
{
	core::pFrame frame = pop_frame(0);
	if (frame) {
		push_frame(0, frame);
	}
	return true;
}
bool ${class_name}::set_param(const core::Parameter& param)
{
	return core::IOThread::set_param(param);
}

} /* namespace ${namespace} */
} /* namespace yuri */
