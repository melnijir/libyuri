/*!
 * @file 		OutOfRange.h
 * @author 		Zdenek Travnicek
 * @date 		29.7.2010
 * @date		16.2.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2010 - 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef OUTOFRANGE_H_
#define OUTOFRANGE_H_

#include "Exception.h"
#include <string>

namespace yuri {

namespace exception {
class OutOfRange: public yuri::exception::Exception {
public:
	EXPORT OutOfRange():Exception(std::string("index of of range")) {}
	EXPORT OutOfRange(std::string msg):Exception(msg) {}
	EXPORT virtual ~OutOfRange() throw();
};

}

}

#endif /* OUTOFRANGE_H_ */
