/*!
 * @file 		yuri_listings.h
 * @author 		Zdenek Travnicek
 * @date 		9.11.2014
 * @copyright	Institute of Intermedia, CTU in Prague, 2014
 * 				CESNET z.s.p.o. 2014
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 */

#ifndef YURI_LISTINGS_H_
#define YURI_LISTINGS_H_
#include "yuri/log/Log.h"
#include "yuri/core/parameter/Parameters.h"
#include <string>
#include <ostream>

namespace yuri {
namespace app {

void list_registered_items(yuri::log::Log& l_, const std::string& what, int verbosity = 0);
void list_single_class(yuri::log::Log& l_, const std::string& name, int verbosity = 0);
void list_input_class(yuri::log::Log& l_, const std::string& name, int verbosity = 0);

const std::string& get_format_name_no_throw(yuri::format_t fmt);

void list_params(yuri::log::Log& l_, const yuri::core::Parameters& params, int verbosity = 0);
void list_registered(yuri::log::Log& l_, int verbosity = 0);
void list_formats(yuri::log::Log& l_, int verbosity = 0);
void list_dgram_sockets(yuri::log::Log& l_, int verbosity = 0);
void list_stream_sockets(yuri::log::Log& l_, int verbosity = 0);
void list_functions(yuri::log::Log& l_, int verbosity = 0);
void list_pipes(yuri::log::Log& l_, int verbosity = 0);
void list_converters(yuri::log::Log& l_, int verbosity = 0);
void list_inputs(yuri::log::Log& l_, int verbosity = 0);
void list_specifiers(yuri::log::Log& l_, int verbosity = 0);

/// Machine readable listings (yuri2 --json)
void list_classes_json(std::ostream& os, const std::string& name = {});
void list_input_class_json(std::ostream& os, const std::string& name);






}
}



#endif /* YURI_LISTINGS_H_ */
