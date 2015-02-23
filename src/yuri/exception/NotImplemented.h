/*!
 * @file 		NotImplemented.h
 * @author 		Zdenek Travnicek
 * @date 		26.9.2010
 * @date		16.2.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2008 - 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef NOTIMPLEMENTED_H_
#define NOTIMPLEMENTED_H_

#include "Exception.h"

namespace yuri {

namespace exception {

class NotImplemented: public yuri::exception::Exception {
public:
	EXPORT NotImplemented(std::string reason = "Not Implemented");
	EXPORT virtual ~NotImplemented() throw();
};

}

}

#endif /* NOTIMPLEMENTED_H_ */
