/*!
 * @file 		GenericBuilder.h
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date		9.11.2014
 * @copyright	CESNET, z.s.p.o, 2014
 * 				Distributed under modified BSD Licence, details in file doc/LICENSE
 *
 */

#ifndef GENERICBUILDER_H_
#define GENERICBUILDER_H_

#include "IOThread.h"
#include "yuri/event/BasicEventParser.h"
namespace yuri {
namespace core {

struct node_record_t {
	std::string name;
	std::string class_name;
	Parameters 	parameters;
	pIOThread	instance;
};
struct link_record_t {
	std::string name;
	std::string class_name;
	Parameters 	parameters;
	std::string source_node;
	std::string target_node;
	position_t	source_index;
	position_t	target_index;
	pPipe		pipe;
};

using node_map = std::map<std::string, node_record_t>;
using link_map = std::map<std::string, link_record_t>;

class GenericBuilder;
using pGenericBuilder = std::shared_ptr<GenericBuilder>;

bool is_special_link_target(const std::string& name);

class GenericBuilder: public IOThread, public event::BasicEventParser {
public:

	EXPORT GenericBuilder(const log::Log& log_, pwThreadBase parent, const std::string& name);
	EXPORT ~GenericBuilder() noexcept {};
	EXPORT virtual void run() override;
	EXPORT virtual bool step() override;
	EXPORT pIOThread get_node(const std::string& name);
	EXPORT virtual event::pBasicEventProducer find_producer(const std::string& name) override;
	EXPORT virtual event::pBasicEventConsumer find_consumer(const std::string& name) override;
	EXPORT virtual bool 				do_process_event(const std::string& event_name, const event::pBasicEvent& event) override;

protected:
	EXPORT void set_graph(node_map nodes, link_map links, std::string routing = {});
private:
	EXPORT virtual	void do_connect_in(position_t position, pPipe pipe) override;
	EXPORT virtual	void do_connect_out(position_t position, pPipe pipe) override;
	EXPORT virtual void receive_event_hook() noexcept override;

	node_map nodes_;
	link_map links_;
	std::string routing_;

	bool start_links();
	bool prepare_nodes();
	bool prepare_routing();
	bool start_nodes();
};



}
}

#endif /* GENERICBUILDER_H_ */
