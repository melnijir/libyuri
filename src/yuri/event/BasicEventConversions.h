/*!
 * @file 		BasicEventConversions.h
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date 		04.07.2013
 * @date		21.11.2013
 * @copyright	Institute of Intermedia, CTU in Prague, 2013
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef BASICEVENTCONVERSIONS_H_
#define BASICEVENTCONVERSIONS_H_
#include "BasicEvent.h"
#include "EventHelpers.h"
#include <unordered_map>
#include <functional>
namespace yuri {
namespace event {

typedef std::function<pBasicEvent(const std::vector<pBasicEvent>&)>
								event_function_t;

struct 							event_function_record_t
{
	const std::string fname;
	std::vector<event_type_t> param_types;
	event_type_t return_type;
	event_function_t func_;
};

class EventFunctions
{
public:
	EXPORT void 				add_function(const event_function_record_t& frec);
	EXPORT void 				add_functions(const std::vector<event_function_record_t>& records);
	std::unordered_multimap<std::string, event_function_record_t> &
								get_map()
									{ return functions_; };
	EXPORT 						EventFunctions() {}
private:
	std::unordered_multimap<std::string, event_function_record_t>
								functions_;
	mutex						map_mutex_;
};

typedef  SingletonBase<EventFunctions>
								EventFunctionsFactory;


EXPORT
pBasicEvent 					implicit_conversion(pBasicEvent event, event_type_t target_type);

EXPORT
pBasicEvent 					call(const std::string& fname, const std::vector<pBasicEvent>& events);

template<class... Args>
pBasicEvent 					call(const std::string& fname, pBasicEvent event0, Args... args)
{
	return call(fname,{event0, args...});
}



class FuncInitHelper {
public:
	FuncInitHelper(std::initializer_list<event_function_record_t> records) {
		for (auto&& f: records) {
			EventFunctionsFactory::get_instance().add_function(std::move(f));
		}
	 }
 };




}
}


#endif /* BASICEVENTCONVERSIONS_H_ */
