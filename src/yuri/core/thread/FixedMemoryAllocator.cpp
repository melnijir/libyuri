/*!
 * @file 		FixedMemoryAllocator.cpp
 * @author 		Zdenek Travnicek
 * @date 		28.1.2012
 * @date		16.2.2013
 * @copyright	Institute of Intermedia, 2012 - 2013
 * 				Distributed under GNU Public License 3.0
 *
 */

#include "FixedMemoryAllocator.h"
#include "yuri/core/utils/uvector.h"
#include "yuri/exception/InitializationFailed.h"
#include "yuri/core/thread/IOThreadGenerator.h"
//#include <iostream>
#include <cassert>
namespace yuri {

namespace core {
MODULE_REGISTRATION_BEGIN("fixed_memory_allocator")
		REGISTER_IOTHREAD("fixed_memory_allocator",FixedMemoryAllocator)
MODULE_REGISTRATION_END()

IOTHREAD_GENERATOR(FixedMemoryAllocator)

yuri::mutex FixedMemoryAllocator::mem_lock;
std::map<yuri::size_t, std::vector<uint8_t* > > FixedMemoryAllocator::memory_pool;

Parameters FixedMemoryAllocator::configure()
{
	Parameters p = IOThread::configure();
	p.set_description("Object that preallocates memory blocks.");
	p["size"]["Block size to allocate"]=0;
	p["count"]["Number of blocks to allocate"]=0;

	//p->set_max_pipes(0,0);
	return p;
}
/** \brief allocate memory blocks and adds them to the pool
 *
 *  Public version of the method, does lock the pool before accessing it.
 *  \param size Size of the blocks to allocate (in bytes)
 *  \param count number of the blocks to allocate
 *  \return True if all blocks were allocated correctly, false otherwise
 */

bool FixedMemoryAllocator::allocate_blocks(yuri::size_t size, yuri::size_t count)
{
	lock_t l(mem_lock);
	return do_allocate_blocks(size,count);
}
/** \brief allocate memory blocks and adds them to the pool
 *
 *  Private version, does NOT lock_t the pool
 *  \param size Size of the blocks to allocate (in bytes)
 *  \param count number of the blocks to allocate
 *  \return True if all blocks were allocated correctly, false otherwise
 */
bool FixedMemoryAllocator::do_allocate_blocks(yuri::size_t size, yuri::size_t count)
{
	uint8_t *tmp;
	if (!memory_pool.count(size)) memory_pool[size]=std::vector<uint8_t*>();
	for (yuri::size_t i=0;i<count;++i) {
//		std::cerr << "Allocating " << size << "\n";
		tmp = new uint8_t[size];
		if (!tmp) return false;
		memory_pool[size].push_back(tmp);
		tmp = 0;
	}
	return true;
}
/** \brief Returns pointer to allocated block of requested size.
 *
 * Returns an allocated block from pool, if there's a block available.
 * If there's no block in the pool for the requested size,
 * the method tries to allocate it first.
 *
 * \param size Size of the requested block
 * \return Pointer to the allocated block,
 * or null pointer if the block cannot be allocated.
 */
FixedMemoryAllocator::memory_block_t FixedMemoryAllocator::get_block(yuri::size_t size)
{
	lock_t l(mem_lock);
	if (memory_pool.count(size)<1 || memory_pool[size].size()<1) {
		if (!do_allocate_blocks(size,1)) {
			throw std::bad_alloc();
		}
	}


	uint8_t * tmp = memory_pool[size].back();
	memory_block_t block = std::make_pair(tmp,Deleter(size,tmp));
	memory_pool[size].pop_back();
//	std::cout << "Serving page of " << size << ". have " << memory_pool[size].size() << " in cache" << "\n";
	return block;
}
/** \brief Returns block to the pool.
 *
 * Method returns previously allocated block to the pool.
 * Intended to be called exclusively from Deleter::operator()
 *
 * \param size Size of the block
 * \param mem pointer to the memory block (Note, it is RAW pointer)
 * \return true is returned to the pool successfully.
 */
bool FixedMemoryAllocator::return_memory(yuri::size_t size, uint8_t * mem)
{
	lock_t l(mem_lock);
	if (!memory_pool.count(size)) memory_pool[size]=std::vector<uint8_t*>();
	memory_pool[size].push_back(mem);
//	std::cout << "Returning page of " << size << ". have " << memory_pool[size].size() << " in cache" << "\n";
	return true;
}
/**\brief Removes blocks from the memory pool
 *
 * Returns up to \e count blocks of size \e size
 *
 * \param size Size of the the block
 * \param count Number of block to remove. Use 0 to remove all blocks.
 * \return true if all blocks were successfully removed, false otherwise
 *
 */
bool FixedMemoryAllocator::remove_blocks(yuri::size_t size, yuri::size_t count)
{
	lock_t l(mem_lock);
	if (!memory_pool.count(size)) return true;
	if (!count) count = memory_pool[size].size();
	while (count-- > 0) {
		delete [] memory_pool[size].back();
		memory_pool[size].pop_back();
	}
	return true;
}
size_t FixedMemoryAllocator::preallocated_blocks(size_t size)
{
	lock_t l(mem_lock);
	if (!memory_pool.count(size)) return 0;
	return memory_pool[size].size();
}
/** \brief Constructor initializes the object and calls
 * FixedMemoryAllocator::allocate_blocks to allocate requested memory blocks.
 *
 */
FixedMemoryAllocator::FixedMemoryAllocator(log::Log &_log, pwThreadBase parent, const Parameters &parameters)
		:IOThread(_log,parent,0,0,"FixedMemoryAllocator")
{
	IOTHREAD_INIT(parameters);
	set_latency(100_ms);
	if (!count || !block_size) {
		log[log::error] << "Wrong parameters specified. "
				"Please provide count and size parameters.";
		throw exception::InitializationFailed("Wrong arguments");
	} else {
		if (!allocate_blocks(block_size,count)) {
			log[log::error] << "Failed to pre-allocate requested blocks" << "\n";
			throw exception::InitializationFailed("Failed to allocate memory");
		}
	}
	log[log::info] << "Preallocated " << count << " block of " << block_size << " bytes." << "\n";
}
/** \brief Destructor tries to remove all blocks with the size the user requested.
 *
 * Note that this may remove more or less blocks that were allocated in constructor
 * Also note that when memory block is returned AFTER the destructor is called,
 * the pool will be populated with them again.
 */
FixedMemoryAllocator::~FixedMemoryAllocator() noexcept
{
	remove_blocks(block_size);
}
/** \brief Implementation of IOThread::set_param
 */
bool FixedMemoryAllocator::set_param(const Parameter &parameter)
{
	if (parameter.get_name() == "count") {
		count=parameter.get<yuri::size_t>();
	} else if (parameter.get_name() == "size") {
		block_size=parameter.get<yuri::size_t>();
	} else return IOThread::set_param(parameter);
	return true;
}
/** \brief Dummy implementation of IOThread::step()
 *
 * Method just waits for the end
 */
bool FixedMemoryAllocator::step()
{
	return true;
}
/** \brief Returns specified block of memory to the memory pool.
 *
 * \param mem pointer to the memory block to be deleted.
 */
void FixedMemoryAllocator::Deleter::operator()(void *mem) const noexcept
{
	assert(mem==original_pointer);
	try { //We should NOT throw here...
		FixedMemoryAllocator::return_memory(size,reinterpret_cast<uint8_t*>(mem));
	} catch(...){}
}

}

}