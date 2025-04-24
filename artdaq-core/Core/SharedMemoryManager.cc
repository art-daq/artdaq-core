#define TRACE_NAME "SharedMemoryManager"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>
#include <list>
#include <map>
#include <unordered_map>
#ifndef SHM_DEST  // Lynn reports that this is missing on Mac OS X?!?
#define SHM_DEST 01000
#endif
#include <csignal>
#include "TRACE/tracemf.h"
#include "artdaq-core/Core/SharedMemoryManager.hh"
#include "cetlib_except/exception.h"

#define TLVL_DETACH 34
#define TLVL_DESTRUCTOR 35
#define TLVL_ATTACH 36
#define TLVL_GETBUFFER 37
#define TLVL_BUFFER 40
#define TLVL_BUFLCK 41
#define TLVL_READREADY 42
#define TLVL_WRITEREADY 46
#define TLVL_POS 48
#define TLVL_RESET 51
#define TLVL_BUFINFO 52
#define TLVL_WRITE 53
#define TLVL_READ 54
#define TLVL_CHKBUFFER 55

static std::list<artdaq::SharedMemoryManager const*> instances = std::list<artdaq::SharedMemoryManager const*>();

static std::unordered_map<int, struct sigaction> old_actions = std::unordered_map<int, struct sigaction>();
static bool sighandler_init = false;
static std::mutex sighandler_mutex;

static void signal_handler(int signum)
{
	// Messagefacility may already be gone at this point, TRACE ONLY!
#if TRACE_REVNUM < 1459
	TRACE_STREAMER(TLVL_ERROR, &("SharedMemoryManager")[0], 0, 0, 0)
#else
	TRACE_STREAMER(TLVL_ERROR, TLOG2("SharedMemoryManager", 0), 0)
#endif
	    << "A signal of type " << signum << " was caught by SharedMemoryManager. Detaching all Shared Memory segments, then proceeding with default handlers!";
	for (auto ii : instances)
	{
		if (ii != nullptr)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			const_cast<artdaq::SharedMemoryManager*>(ii)->Detach(false, "", "", false /* don't force destruct segment, allows reconnection (applicable for
			               restart and/or multiple art processes (i.e. dispatcher)) */
			);
		}
		ii = nullptr;
	}

	sigset_t set;
	pthread_sigmask(SIG_UNBLOCK, nullptr, &set);
	pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

#if TRACE_REVNUM < 1459
	TRACE_STREAMER(TLVL_ERROR, &("SharedMemoryManager")[0], 0, 0, 0)
#else
	TRACE_STREAMER(TLVL_ERROR, TLOG2("SharedMemoryManager", 0), 0)
#endif
	    << "Restoring default signal handler";
	sigaction(signum, &old_actions[signum], nullptr);
}

artdaq::SharedMemoryManager::SharedMemoryManager(uint32_t shm_key, size_t buffer_count, size_t buffer_size, uint64_t buffer_timeout_us, bool destructive_read_mode)
    : shm_segment_id_(-1)
    , shm_ptr_(nullptr)
    , shm_key_(shm_key)
    , manager_id_(-1)
    , last_seen_id_(0)
    , reader_pos_(0)
    , writer_pos_(0)
{
	requested_shm_parameters_.buffer_count = buffer_count;
	requested_shm_parameters_.buffer_size = buffer_size;
	requested_shm_parameters_.buffer_timeout_us = buffer_timeout_us;
	requested_shm_parameters_.destructive_read_mode = destructive_read_mode;

	instances.push_back(this);
	Attach();

	std::lock_guard<std::mutex> lk(sighandler_mutex);

	if (!sighandler_init)  //&& manager_id_ == 0) // ELF 3/22/18: Taking out manager_id_==0 requirement as I think kill(getpid()) is enough protection
	{
		sighandler_init = true;
		std::vector<int> signals = {SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM, SIGUSR2, SIGHUP};  // SIGQUIT is used by art in normal operation
		for (auto signal : signals)
		{
			struct sigaction old_action;
			sigaction(signal, nullptr, &old_action);

			// If the old handler wasn't SIG_IGN (it's a handler that just
			//  "ignore" the signal)
			if (old_action.sa_handler != SIG_IGN)  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
			{
				struct sigaction action;
				action.sa_handler = signal_handler;
				sigemptyset(&action.sa_mask);
				for (auto sigblk : signals)
				{
					sigaddset(&action.sa_mask, sigblk);
				}
				action.sa_flags = 0;

				// Replace the signal handler of SIGINT with the one described by new_action
				sigaction(signal, &action, nullptr);
			}
			old_actions[signal] = old_action;
		}
	}
}

// The clang-tidy warning comes from Detach, which can throw an exception if called with first parameter = true (defaults to false)
artdaq::SharedMemoryManager::~SharedMemoryManager() noexcept  // NOLINT(bugprone-exception-escape)
{
	TLOG(TLVL_DESTRUCTOR) << "~SharedMemoryManager called";
	{
		static std::mutex destructor_mutex;
		std::lock_guard<std::mutex> lk(destructor_mutex);
		for (auto it = instances.begin(); it != instances.end(); ++it)
		{
			if (*it == this)
			{
				it = instances.erase(it);
				break;
			}
		}
	}
	Detach();
	{
		std::lock_guard<std::mutex> lk(sighandler_mutex);

		// Restore signal handlers
		if (sighandler_init && instances.empty())
		{
			sighandler_init = false;
			for (auto signal : old_actions)
			{
				sigaction(signal.first, &signal.second, nullptr);
			}
			old_actions.clear();
		}
	}
	TLOG(TLVL_DESTRUCTOR) << "~SharedMemoryManager done";
}

bool artdaq::SharedMemoryManager::Attach(size_t timeout_usec)
{
	if (IsValid())
	{
		if (manager_id_ == 0)
		{
			return true;
		}
		Detach();
	}

	size_t timeout_us = timeout_usec > 0 ? timeout_usec : 1000000;
	auto start_time = std::chrono::steady_clock::now();
	last_seen_id_ = 0;
	size_t shmSize = requested_shm_parameters_.buffer_count * (requested_shm_parameters_.buffer_size + sizeof(ShmBuffer)) + sizeof(ShmStruct);

	auto available = GetAvailableRAM();

	TLOG(TLVL_INFO) << "Requested shared memory size " << PrintBytes(shmSize)
	                << " (" << requested_shm_parameters_.buffer_count << " buffers * " << PrintBytes(requested_shm_parameters_.buffer_size) << ")"
	                << ", available RAM " << PrintBytes(available);
	if (shmSize > 0.8 * available)
	{
		TLOG(TLVL_WARNING) << "Requested shared memory size is greater than 80% of available RAM! Allocation of shared memory will likely fail!";
	}

	// 19-Feb-2019, KAB: separating out the determination of whether a given process owns the shared
	// memory (indicated by manager_id_ == 0) and whether or not the shared memory already exists.
	if (requested_shm_parameters_.buffer_count > 0 && requested_shm_parameters_.buffer_size > 0 && manager_id_ <= 0)
	{
		manager_id_ = 0;
	}

	shm_segment_id_ = shmget(shm_key_, shmSize, 0666);
	if (shm_segment_id_ == -1)
	{
		if (manager_id_ == 0)
		{
			TLOG(TLVL_ATTACH) << "Creating shared memory segment with key " << std::hex << std::showbase << shm_key_ << " and size " << std::dec << shmSize;
			shm_segment_id_ = shmget(shm_key_, shmSize, IPC_CREAT | 0666);

			if (shm_segment_id_ == -1)
			{
				TLOG(TLVL_ERROR) << "Error creating shared memory segment with key " << std::hex << std::showbase << shm_key_ << ", errno=" << std::dec << errno << " (" << strerror(errno) << ")";
			}
		}
		else
		{
			while (shm_segment_id_ == -1 && TimeUtils::GetElapsedTimeMicroseconds(start_time) < timeout_us)
			{
				shm_segment_id_ = shmget(shm_key_, shmSize, 0666);
			}
		}
	}
	TLOG(TLVL_ATTACH) << "shm_key == " << std::hex << std::showbase << shm_key_ << ", shm_segment_id == " << std::dec << shm_segment_id_;

	if (shm_segment_id_ > -1)
	{
		TLOG(TLVL_ATTACH)
		    << "Attached to shared memory segment with ID = " << shm_segment_id_
		    << " and size " << shmSize
		    << " bytes";
		shm_ptr_ = static_cast<ShmStruct*>(shmat(shm_segment_id_, nullptr, 0));
		TLOG(TLVL_ATTACH)
		    << "Attached to shared memory segment at address "
		    << std::hex << std::showbase << static_cast<void*>(shm_ptr_) << std::dec;
		if ((shm_ptr_ != nullptr) && shm_ptr_ != reinterpret_cast<void*>(-1))  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		{
			if (manager_id_ == 0)
			{
				if (shm_ptr_->ready_magic == 0xCAFE1111)
				{
					TLOG(TLVL_WARNING) << "Owner encountered already-initialized Shared Memory! "
					                   << "Once the system is shut down, you can use one of the following commands "
					                   << "to clean up this shared memory: 'ipcrm -M " << std::hex << std::showbase << shm_key_
					                   << "' or 'ipcrm -m " << std::dec << shm_segment_id_ << "'.";
					// exit(-2);
				}
				TLOG(TLVL_ATTACH) << "Owner initializing Shared Memory";
				shm_ptr_->next_id = 1;
				shm_ptr_->next_sequence_id = 0;
				shm_ptr_->buffer_size = requested_shm_parameters_.buffer_size;
				shm_ptr_->buffer_count = requested_shm_parameters_.buffer_count;
				shm_ptr_->buffer_timeout_us = requested_shm_parameters_.buffer_timeout_us;
				shm_ptr_->destructive_read_mode = requested_shm_parameters_.destructive_read_mode;

				buffer_ptrs_ = std::vector<ShmBuffer*>(shm_ptr_->buffer_count);
				for (int ii = 0; ii < static_cast<int>(requested_shm_parameters_.buffer_count); ++ii)
				{
					buffer_ptrs_[ii] = reinterpret_cast<ShmBuffer*>(reinterpret_cast<uint8_t*>(shm_ptr_ + 1) + ii * sizeof(ShmBuffer));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
					if (getBufferInfo_(ii) == nullptr)
					{
						return false;
					}
					getBufferInfo_(ii)->writePos = 0;
					getBufferInfo_(ii)->readPos = 0;
					getBufferInfo_(ii)->semaphore = ShmBufferSem(BufferSemaphoreFlags::Empty, -1);
					getBufferInfo_(ii)->last_touch_time = TimeUtils::gettimeofday_us();
				}

				shm_ptr_->ready_magic = 0xCAFE1111;
			}
			else
			{
				TLOG(TLVL_ATTACH) << "Waiting for owner to initalize Shared Memory";
				while (shm_ptr_->ready_magic != 0xCAFE1111) { usleep(1000); }
				TLOG(TLVL_ATTACH) << "Getting ID from Shared Memory";
				GetNewId();
				TLOG(TLVL_ATTACH) << "Getting Shared Memory Size parameters";

				requested_shm_parameters_.buffer_count = shm_ptr_->buffer_count;
				buffer_ptrs_ = std::vector<ShmBuffer*>(shm_ptr_->buffer_count);
				for (int ii = 0; ii < shm_ptr_->buffer_count; ++ii)
				{
					buffer_ptrs_[ii] = reinterpret_cast<ShmBuffer*>(reinterpret_cast<uint8_t*>(shm_ptr_ + 1) + ii * sizeof(ShmBuffer));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
				}
			}

			// last_seen_id_ = shm_ptr_->next_sequence_id;

			TLOG(TLVL_ATTACH) << "Initialization Complete: "
			                  << "key: " << std::hex << std::showbase << shm_key_
			                  << ", manager ID: " << std::dec << manager_id_
			                  << ", Buffer size: " << shm_ptr_->buffer_size
			                  << ", Buffer count: " << shm_ptr_->buffer_count;
			return true;
		}

		TLOG(TLVL_ERROR) << "Failed to attach to shared memory segment "
		                 << shm_segment_id_;
		return false;
	}

	TLOG(TLVL_ERROR) << "Failed to connect to shared memory segment with key " << std::hex << std::showbase << shm_key_
	                 << ", errno=" << std::dec << errno << " (" << strerror(errno) << ")"
	                 << ".  Please check "
	                 << "if a stale shared memory segment needs to "
	                 << "be cleaned up. (ipcs, ipcrm -m <segId>)";
	return false;
}

bool artdaq::SharedMemoryManager::claimBufferForReading_(ShmBufferSem semaphore, ShmBuffer* buffer_ptr, int buffer_num)
{
	if (buffer_ptr == nullptr)
	{
		return false;
	}

	ShmBufferSem claim(BufferSemaphoreFlags::Reading, manager_id_);
	auto sequence_id = buffer_ptr->sequence_id.load();
	if (!buffer_ptr->semaphore.compare_exchange_strong(semaphore, claim))
	{
		return false;
	}
	if (!checkBuffer_(buffer_ptr, BufferSemaphoreFlags::Reading, false))
	{
		TLOG(TLVL_GETBUFFER) << "Failed to acquire buffer " << buffer_num << " (someone else changed manager ID while I was changing sem)";
		return false;
	}
	buffer_ptr->readPos = 0;
	touchBuffer_(buffer_ptr);
	if (!checkBuffer_(buffer_ptr, BufferSemaphoreFlags::Reading, false))
	{
		TLOG(TLVL_GETBUFFER) << "Failed to acquire buffer " << buffer_num << " (someone else changed manager ID while I was touching buffer SHOULD NOT HAPPEN!)";
		return false;
	}

	TLOG(TLVL_GETBUFFER + 2) << "Claimed buffer " << buffer_num << " with sequence id " << sequence_id << " (last seen=" << last_seen_id_ << ")";
	if (sequence_id > last_seen_id_)
	{
		last_seen_id_ = sequence_id;
	}

	return true;
}

int artdaq::SharedMemoryManager::GetBufferForReading()
{
	TLOG(TLVL_GETBUFFER) << "GetBufferForReading BEGIN";
	RegisterReader();

	TLOG(TLVL_GETBUFFER) << "Scanning " << shm_ptr_->buffer_count << " buffers";

	for (int retry = 0; retry < 5; retry++)
	{
		ShmBufferSem semaphore;
		int buffer_num = -1;
		auto rp = reader_pos_.load();
		auto reader_count = GetReaderCount();

		std::map<size_t, std::pair<int, ShmBufferSem>> potential_buffers;

		for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
		{
			buffer_num = (ii + rp) % shm_ptr_->buffer_count;

			// TLOG(TLVL_GETBUFFER + 1) << "Checking if buffer " << buffer_num << " is stale. Shm destructive_read_mode=" << shm_ptr_->destructive_read_mode;
			ResetBuffer(buffer_num);

			auto buf = getBufferInfo_(buffer_num);
			if (buf == nullptr)
			{
				continue;
			}

			semaphore = buf->semaphore.load();
			auto sequence_id = buf->sequence_id.load();

			if (semaphore.flags == BufferSemaphoreFlags::Full && (semaphore.id == -1 || semaphore.id == manager_id_) && (shm_ptr_->destructive_read_mode || sequence_id > last_seen_id_))
			{
				TLOG(TLVL_GETBUFFER + 1) << "ID " << manager_id_ << " Buffer " << buffer_num << ": sem=" << FlagToString(semaphore.flags)
				                         << " (looking for " << FlagToString(BufferSemaphoreFlags::Full) << "), sem_id=" << semaphore.id << ", seq_id=" << sequence_id << ", last_seen_id_=" << last_seen_id_ << ", reader_count=" << reader_count;
				// Claim the buffer if I haven't claimed buffers before, if we are in Broadcast mode, it is in my sequence, or it is from a previous RR iteration
				if (last_seen_id_ == 0 || !shm_ptr_->destructive_read_mode || sequence_id % reader_count == last_seen_id_ % reader_count || sequence_id + (rr_catch_up_factor_ * reader_count) < last_seen_id_)
				{
					potential_buffers[sequence_id] = std::make_pair(buffer_num, semaphore);
				}
			}
		}

		TLOG(TLVL_GETBUFFER + 1) << "Picking from " << potential_buffers.size() << " potential buffers";
		if (potential_buffers.size() > 0)
		{
			for (auto&& [seqID, buf_pair] : potential_buffers)
			{
				auto&& [buf_num, semaphore] = buf_pair;
				TLOG(TLVL_GETBUFFER + 1) << "Attempting to claim buffer " << buf_num << " with sequence_id " << seqID;
				auto buf = getBufferInfo_(buf_num);
				if (buf != nullptr && claimBufferForReading_(semaphore, buf, buf_num))
				{
					TLOG(TLVL_GETBUFFER) << "Returning " << buf_num;
					return buf_num;
				}
			}
		}
	}

	TLOG(TLVL_GETBUFFER) << "Returning -1 because no buffers are ready";
	return -1;
}

int artdaq::SharedMemoryManager::GetBufferForWriting(bool overwrite)
{
	TLOG(TLVL_GETBUFFER + 1) << "GetBufferForWriting BEGIN, overwrite=" << (overwrite ? "true" : "false");

	RegisterWriter();

	auto wp = writer_pos_.load();

	TLOG(TLVL_GETBUFFER) << "GetBufferForWriting scanning " << shm_ptr_->buffer_count << " buffers";

	// First, only look for "Empty" buffers
	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
		auto buffer = (ii + wp) % shm_ptr_->buffer_count;

		ResetBuffer(buffer);

		auto buf = getBufferInfo_(buffer);
		if (buf == nullptr)
		{
			continue;
		}

		auto semaphore = buf->semaphore.load();

		if (semaphore.flags == BufferSemaphoreFlags::Empty && semaphore.id == -1)
		{
			touchBuffer_(buf);
			ShmBufferSem claim(BufferSemaphoreFlags::Writing, manager_id_);
			if (!buf->semaphore.compare_exchange_strong(semaphore, claim))
			{
				continue;
			}
			if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
			{
				continue;
			}
			writer_pos_ = (buffer + 1) % shm_ptr_->buffer_count;
			buf->sequence_id = ++shm_ptr_->next_sequence_id;
			buf->writePos = 0;
			if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
			{
				continue;
			}
			touchBuffer_(buf);
			TLOG(TLVL_GETBUFFER + 1) << "GetBufferForWriting returning empty buffer " << buffer;
			return buffer;
		}
	}

	if (overwrite)
	{
		// Then, look for "Full" buffers
		for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
		{
			auto buffer = (ii + wp) % shm_ptr_->buffer_count;

			ResetBuffer(buffer);

			auto buf = getBufferInfo_(buffer);
			if (buf == nullptr)
			{
				continue;
			}

			auto semaphore = buf->semaphore.load();

			if (semaphore.flags == BufferSemaphoreFlags::Full && semaphore.id == -1)
			{
				touchBuffer_(buf);
				ShmBufferSem claim(BufferSemaphoreFlags::Writing, manager_id_);
				if (!buf->semaphore.compare_exchange_strong(semaphore, claim))
				{
					continue;
				}
				if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
				{
					continue;
				}
				writer_pos_ = (buffer + 1) % shm_ptr_->buffer_count;
				buf->sequence_id = ++shm_ptr_->next_sequence_id;
				buf->writePos = 0;
				if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
				{
					continue;
				}
				touchBuffer_(buf);
				TLOG(TLVL_GETBUFFER + 1) << "GetBufferForWriting returning full buffer (overwrite mode) " << buffer;
				return buffer;
			}
		}

		// Finally, if we still haven't found a buffer, we have to clobber a reader...
		for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
		{
			auto buffer = (ii + wp) % shm_ptr_->buffer_count;

			ResetBuffer(buffer);

			auto buf = getBufferInfo_(buffer);
			if (buf == nullptr)
			{
				continue;
			}

			auto semaphore = buf->semaphore.load();

			if (semaphore.flags == BufferSemaphoreFlags::Reading)
			{
				touchBuffer_(buf);
				ShmBufferSem claim(BufferSemaphoreFlags::Writing, manager_id_);
				if (!buf->semaphore.compare_exchange_strong(semaphore, claim))
				{
					continue;
				}
				if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
				{
					continue;
				}
				writer_pos_ = (buffer + 1) % shm_ptr_->buffer_count;
				buf->sequence_id = ++shm_ptr_->next_sequence_id;
				buf->writePos = 0;
				if (!checkBuffer_(buf, BufferSemaphoreFlags::Writing, false))
				{
					continue;
				}
				touchBuffer_(buf);
				TLOG(TLVL_GETBUFFER + 1) << "GetBufferForWriting clobbering reader on buffer " << buffer << " (overwrite mode)";
				return buffer;
			}
		}
	}
	TLOG(TLVL_GETBUFFER + 1) << "GetBufferForWriting Returning -1 because no buffers are ready";
	return -1;
}

size_t artdaq::SharedMemoryManager::ReadReadyCount()
{
	if (!IsValid())
	{
		return 0;
	}

	TLOG(TLVL_READREADY) << std::hex << std::showbase << shm_key_ << " ReadReadyCount BEGIN" << std::dec;
	TLOG(TLVL_READREADY) << "ReadReadyCount scanning " << shm_ptr_->buffer_count << " buffers";
	size_t count = 0;
	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
#ifndef __OPTIMIZE__
		TLOG(TLVL_READREADY + 1) << std::hex << std::showbase << shm_key_ << std::dec << " ReadReadyCount: Checking if buffer " << ii << " is stale.";
#endif
		ResetBuffer(ii);
		auto buf = getBufferInfo_(ii);
		if (buf == nullptr)
		{
			continue;
		}

		auto semaphore = buf->semaphore.load();
#ifndef __OPTIMIZE__
		TLOG(TLVL_READREADY + 2) << std::hex << std::showbase << shm_key_ << std::dec << " ReadReadyCount: Buffer " << ii << ": sem=" << FlagToString(semaphore.flags)
		                         << " (looking for " << FlagToString(BufferSemaphoreFlags::Full) << "), sem_id=" << semaphore.id << " )";
#endif
		if (semaphore.flags == BufferSemaphoreFlags::Full && (semaphore.id == -1 || semaphore.id == manager_id_) && (shm_ptr_->destructive_read_mode || buf->sequence_id > last_seen_id_))
		{
#ifndef __OPTIMIZE__
			TLOG(TLVL_READREADY + 3) << std::hex << std::showbase << shm_key_ << std::dec << " ReadReadyCount: Buffer " << ii << " is either unowned or owned by this manager, and is marked full.";
#endif
			touchBuffer_(buf);
			++count;
		}
	}
	return count;
}

size_t artdaq::SharedMemoryManager::WriteReadyCount(bool overwrite)
{
	if (!IsValid())
	{
		return 0;
	}
	TLOG(TLVL_WRITEREADY) << std::hex << std::showbase << shm_key_ << " WriteReadyCount BEGIN" << std::dec;
	TLOG(TLVL_WRITEREADY) << "WriteReadyCount(" << overwrite << ") scanning " << shm_ptr_->buffer_count << " buffers";
	size_t count = 0;
	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
		// ELF, 3/19/2019: This TRACE call is a major performance hit with many buffers
#ifndef __OPTIMIZE__
		TLOG(TLVL_WRITEREADY + 1) << std::hex << std::showbase << shm_key_ << std::dec << " WriteReadyCount: Checking if buffer " << ii << " is stale.";
#endif
		ResetBuffer(ii);
		auto buf = getBufferInfo_(ii);
		if (buf == nullptr)
		{
			continue;
		}
		auto semaphore = buf->semaphore.load();
		if ((semaphore.flags == BufferSemaphoreFlags::Empty && semaphore.id == -1) || (overwrite && semaphore.flags != BufferSemaphoreFlags::Writing))
		{
#ifndef __OPTIMIZE__
			TLOG(TLVL_WRITEREADY + 1) << std::hex << std::showbase << shm_key_ << std::dec << " WriteReadyCount: Buffer " << ii << " is either empty or is available for overwrite.";
#endif
			++count;
		}
	}
	return count;
}

bool artdaq::SharedMemoryManager::ReadyForRead()
{
	if (!IsValid())
	{
		return false;
	}

	TLOG(TLVL_READREADY) << std::hex << std::showbase << shm_key_ << " ReadyForRead BEGIN" << std::dec;

	auto rp = reader_pos_.load();

	TLOG(TLVL_READREADY) << "ReadyForRead scanning " << shm_ptr_->buffer_count << " buffers";

	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
		auto buffer = (rp + ii) % shm_ptr_->buffer_count;

#ifndef __OPTIMIZE__
		TLOG(TLVL_READREADY + 1) << std::hex << std::showbase << shm_key_ << std::dec << " ReadyForRead: Checking if buffer " << buffer << " is stale.";
#endif
		ResetBuffer(buffer);
		auto buf = getBufferInfo_(buffer);
		if (buf == nullptr)
		{
			continue;
		}

		auto semaphore = buf->semaphore.load();
#ifndef __OPTIMIZE__
		TLOG(TLVL_READREADY + 2) << std::hex << std::showbase << shm_key_ << std::dec << " ReadyForRead: Buffer " << buffer << ": sem=" << FlagToString(semaphore.flags) << " (looking for " << FlagToString(BufferSemaphoreFlags::Full) << "), sem_id="
		                         << semaphore.id << " )"
		                         << " seq_id=" << buf->sequence_id << " >? " << last_seen_id_;
#endif

		if (semaphore.flags == BufferSemaphoreFlags::Full && (semaphore.id == -1 || semaphore.id == manager_id_) && (shm_ptr_->destructive_read_mode || buf->sequence_id > last_seen_id_))
		{
			TLOG(TLVL_READREADY + 3) << std::hex << std::showbase << shm_key_ << std::dec << " ReadyForRead: Buffer " << buffer << " is either unowned or owned by this manager, and is marked full.";
			touchBuffer_(buf);
			return true;
		}
	}
	return false;
}

bool artdaq::SharedMemoryManager::ReadyForWrite(bool overwrite)
{
	if (!IsValid())
	{
		return false;
	}
	TLOG(TLVL_WRITEREADY) << std::hex << std::showbase << shm_key_ << " ReadyForWrite BEGIN" << std::dec;

	auto wp = writer_pos_.load();

	TLOG(TLVL_WRITEREADY) << "ReadyForWrite scanning " << shm_ptr_->buffer_count << " buffers";

	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
		auto buffer = (wp + ii) % shm_ptr_->buffer_count;
		TLOG(TLVL_WRITEREADY + 1) << std::hex << std::showbase << shm_key_ << std::dec << " ReadyForWrite: Checking if buffer " << buffer << " is stale.";
		ResetBuffer(buffer);
		auto buf = getBufferInfo_(buffer);
		if (buf == nullptr)
		{
			continue;
		}
		auto semaphore = buf->semaphore.load();
		if ((semaphore.flags == BufferSemaphoreFlags::Empty && semaphore.id == -1) || (overwrite && semaphore.flags != BufferSemaphoreFlags::Writing))
		{
			TLOG(TLVL_WRITEREADY + 1) << std::hex << std::showbase << shm_key_
			                          << std::dec
			                          << " ReadyForWrite: Buffer " << ii << " is either empty or available for overwrite.";
			return true;
		}
	}
	return false;
}

std::deque<int> artdaq::SharedMemoryManager::GetBuffersOwnedByManager()
{
	std::deque<int> output;
	size_t buffer_count = size();
	if (!IsValid() || buffer_count == 0)
	{
		return output;
	}
	TLOG(TLVL_BUFFER) << "GetBuffersOwnedByManager BEGIN";
	for (size_t ii = 0; ii < buffer_count; ++ii)
	{
		auto buf = getBufferInfo_(ii);
		if (buf == nullptr)
		{
			continue;
		}
		if (buf->semaphore.load().id == manager_id_)
		{
			output.push_back(ii);
		}
	}

	TLOG(TLVL_BUFFER) << "GetBuffersOwnedByManager: own " << output.size() << " / " << buffer_count << " buffers.";
	return output;
}

size_t artdaq::SharedMemoryManager::BufferDataSize(int buffer)
{
	TLOG(TLVL_BUFFER) << "BufferDataSize(" << buffer << ") called.";

	if (!shm_ptr_ || buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return 0;
	}
	touchBuffer_(buf);

	TLOG(TLVL_BUFFER) << "BufferDataSize: buffer " << buffer << ", size=" << buf->writePos;
	return buf->writePos;
}

void artdaq::SharedMemoryManager::ResetReadPos(int buffer)
{
	TLOG(TLVL_POS) << "ResetReadPos(" << buffer << ") called.";

	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if ((buf == nullptr) || buf->semaphore.load().id != manager_id_)
	{
		return;
	}
	touchBuffer_(buf);
	buf->readPos = 0;

	TLOG(TLVL_POS) << "ResetReadPos(" << buffer << ") ended.";
}

void artdaq::SharedMemoryManager::ResetWritePos(int buffer)
{
	TLOG(TLVL_POS + 1) << "ResetWritePos(" << buffer << ") called.";

	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return;
	}
	checkBuffer_(buf, BufferSemaphoreFlags::Writing);
	touchBuffer_(buf);
	buf->writePos = 0;

	TLOG(TLVL_POS + 1) << "ResetWritePos(" << buffer << ") ended.";
}

void artdaq::SharedMemoryManager::IncrementReadPos(int buffer, size_t read)
{
	TLOG(TLVL_POS) << "IncrementReadPos called: buffer= " << buffer << ", bytes to read=" << read;

	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if ((buf == nullptr) || buf->semaphore.load().id != manager_id_)
	{
		return;
	}
	touchBuffer_(buf);
	TLOG(TLVL_POS) << "IncrementReadPos: buffer= " << buffer << ", readPos=" << buf->readPos << ", bytes read=" << read;
	buf->readPos = buf->readPos + read;
	TLOG(TLVL_POS) << "IncrementReadPos: buffer= " << buffer << ", New readPos is " << buf->readPos;
	if (read == 0)
	{
		Detach(true, "LogicError", "Cannot increment Read pos by 0! (buffer=" + std::to_string(buffer) + ", readPos=" + std::to_string(buf->readPos) + ", writePos=" + std::to_string(buf->writePos) + ")");
	}
}

bool artdaq::SharedMemoryManager::IncrementWritePos(int buffer, size_t written)
{
	TLOG(TLVL_POS + 1) << "IncrementWritePos called: buffer= " << buffer << ", bytes written=" << written;

	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return false;
	}
	checkBuffer_(buf, BufferSemaphoreFlags::Writing);
	touchBuffer_(buf);
	if (buf->writePos + written > shm_ptr_->buffer_size)
	{
		TLOG(TLVL_ERROR) << "Requested write size is larger than the buffer size! (sz=" << std::dec << shm_ptr_->buffer_size << ", cur + req=" << std::dec << buf->writePos + written << ", diff=" << std::dec << (buf->writePos + written - shm_ptr_->buffer_size) << ")";
		return false;
	}
	TLOG(TLVL_POS + 1) << "IncrementWritePos: buffer= " << buffer << ", writePos=" << buf->writePos << ", bytes written=" << written;
	buf->writePos += written;
	TLOG(TLVL_POS + 1) << "IncrementWritePos: buffer= " << buffer << ", New writePos is " << buf->writePos;
	if (written == 0)
	{
		Detach(true, "LogicError", "Cannot increment Write pos by 0!");
	}

	return true;
}

bool artdaq::SharedMemoryManager::MoreDataInBuffer(int buffer)
{
	TLOG(TLVL_POS + 2) << "MoreDataInBuffer(" << buffer << ") called.";

	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return false;
	}
	TLOG(TLVL_POS + 2) << "MoreDataInBuffer: buffer= " << buffer << ", readPos=" << std::to_string(buf->readPos) << ", writePos=" << buf->writePos;
	return buf->readPos < buf->writePos;
}

bool artdaq::SharedMemoryManager::CheckBuffer(int buffer, BufferSemaphoreFlags flags)
{
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	return checkBuffer_(getBufferInfo_(buffer), flags, false);
}

void artdaq::SharedMemoryManager::MarkBufferFull(int buffer, int destination)
{
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto shmBuf = getBufferInfo_(buffer);
	if (shmBuf == nullptr)
	{
		return;
	}
	touchBuffer_(shmBuf);
	auto semaphore = shmBuf->semaphore.load();
	auto release = ShmBufferSem(BufferSemaphoreFlags::Full, destination);
	if (semaphore.id == manager_id_)
	{
		auto check = shmBuf->semaphore.compare_exchange_strong(semaphore, release);
		if (!check)
		{
			Detach(true, "LogicError", "Unable to release buffer because of inconsistent semaphore state!");
		}
	}
}

void artdaq::SharedMemoryManager::MarkBufferEmpty(int buffer, bool force, bool detachOnException)
{
	TLOG(TLVL_POS + 3) << "MarkBufferEmpty BEGIN, buffer=" << buffer << ", force=" << force << ", manager_id_=" << manager_id_;
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}
	auto shmBuf = getBufferInfo_(buffer);
	if (shmBuf == nullptr)
	{
		return;
	}
	if (!force)
	{
		auto ret = checkBuffer_(shmBuf, BufferSemaphoreFlags::Reading, detachOnException);
		if (!ret) return;
	}
	touchBuffer_(shmBuf);

	shmBuf->readPos = 0;
	auto semaphore = shmBuf->semaphore.load();
	ShmBufferSem release(BufferSemaphoreFlags::Reading, -1);

	if ((force && (manager_id_ == 0 || manager_id_ == semaphore.id)) || (!force && shm_ptr_->destructive_read_mode))
	{
		TLOG(TLVL_POS + 3) << "MarkBufferEmpty Resetting buffer " << buffer << " (SeqID " << shmBuf->sequence_id << ") to Empty state";
		shmBuf->writePos = 0;
		release.flags = BufferSemaphoreFlags::Empty;
		if (reader_pos_ == static_cast<unsigned>(buffer))
		{
			TLOG(TLVL_POS + 3) << "Incrementing reader_pos_ from " << reader_pos_ << " to " << (buffer + 1) % shm_ptr_->buffer_count;
			reader_pos_ = (buffer + 1) % shm_ptr_->buffer_count;
		}
	}
	else
	{
		release.flags = BufferSemaphoreFlags::Full;
	}
	auto check = shmBuf->semaphore.compare_exchange_strong(semaphore, release);
	if (!check)
	{
		Detach(true, "LogicError", "Unable to release buffer because of inconsistent semaphore state!");
	}
	TLOG(TLVL_POS + 3) << "MarkBufferEmpty END, buffer=" << buffer << ", force=" << force;
}

bool artdaq::SharedMemoryManager::isBufferStale_(ShmBuffer* shmBuf)
{
	size_t delta = TimeUtils::gettimeofday_us() - shmBuf->last_touch_time;
	if (delta > 0xFFFFFFFF)
	{
		TLOG(TLVL_RESET) << "Buffer has touch time in the future, setting it to current time and ignoring...";
		shmBuf->last_touch_time = TimeUtils::gettimeofday_us();
		return false;
	}
	if (shm_ptr_->buffer_timeout_us == 0 || delta <= shm_ptr_->buffer_timeout_us || shmBuf->semaphore.load().flags == BufferSemaphoreFlags::Empty)
	{
		return false;
	}
	TLOG(TLVL_RESET) << "Buffer at " << static_cast<void*>(shmBuf) << " is stale, time=" << TimeUtils::gettimeofday_us() << ", last touch=" << shmBuf->last_touch_time << ", d=" << delta << ", timeout=" << shm_ptr_->buffer_timeout_us;
	return true;
}

bool artdaq::SharedMemoryManager::ResetBuffer(int buffer)
{
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}

	auto shmBuf = getBufferInfo_(buffer);
	if (shmBuf == nullptr)
	{
		return false;
	}

	if (!isBufferStale_(shmBuf))
	{
		return false;
	}

	auto semaphore = shmBuf->semaphore.load();
	if (semaphore.id == manager_id_ && semaphore.flags == BufferSemaphoreFlags::Writing)
	{
		return true;
	}

	if (!shm_ptr_->destructive_read_mode && semaphore.flags == BufferSemaphoreFlags::Full && manager_id_ == 0)
	{
		TLOG(TLVL_RESET) << "Resetting old broadcast mode buffer " << buffer << " (seqid=" << shmBuf->sequence_id << "). State: Full-->Empty";
		shmBuf->writePos = 0;
		ShmBufferSem release(BufferSemaphoreFlags::Empty, -1);
		auto check = shmBuf->semaphore.compare_exchange_strong(semaphore, release);
		if (!check)
		{
			Detach(true, "LogicError", "Unable to release buffer because of inconsistent semaphore state!");
		}
		if (reader_pos_ == static_cast<unsigned>(buffer))
		{
			reader_pos_ = (buffer + 1) % shm_ptr_->buffer_count;
		}
		return true;
	}

	if (semaphore.id != manager_id_ && semaphore.flags == BufferSemaphoreFlags::Reading && manager_id_ == 0)
	{
		// Ron wants to re-check for potential interleave of buffer state updates
		size_t delta = TimeUtils::gettimeofday_us() - shmBuf->last_touch_time;
		if (delta <= shm_ptr_->buffer_timeout_us)
		{
			return false;
		}
		TLOG(TLVL_WARNING) << "Stale Read buffer " << buffer << " at " << static_cast<void*>(shmBuf)
		                   << " ( " << delta << " / " << shm_ptr_->buffer_timeout_us << " us ) detected! (seqid="
		                   << shmBuf->sequence_id << ", owner=" << semaphore.id << ") Resetting... Reading-->Full";
		shmBuf->readPos = 0;
		ShmBufferSem release(BufferSemaphoreFlags::Full, -1);
		auto check = shmBuf->semaphore.compare_exchange_strong(semaphore, release);
		if (!check)
		{
			Detach(true, "LogicError", "Unable to release buffer because of inconsistent semaphore state!");
		}
		return true;
	}
	return false;
}

bool artdaq::SharedMemoryManager::IsEndOfData() const
{
	if (!IsValid())
	{
		return true;
	}

	struct shmid_ds info;
	auto sts = shmctl(shm_segment_id_, IPC_STAT, &info);
	if (sts < 0)
	{
		TLOG(TLVL_BUFINFO) << "Error accessing Shared Memory info: " << errno << " (" << strerror(errno) << ").";
		return true;
	}

	if ((info.shm_perm.mode & SHM_DEST) != 0)
	{
		TLOG(TLVL_INFO) << "Shared Memory marked for destruction. Probably an end-of-data condition!";
		return true;
	}

	return false;
}

uint16_t artdaq::SharedMemoryManager::GetAttachedCount() const
{
	if (!IsValid())
	{
		return 0;
	}

	struct shmid_ds info;
	auto sts = shmctl(shm_segment_id_, IPC_STAT, &info);
	if (sts < 0)
	{
		TLOG(TLVL_BUFINFO) << "Error accessing Shared Memory info: " << errno << " (" << strerror(errno) << ").";
		return 0;
	}

	return info.shm_nattch;
}

size_t artdaq::SharedMemoryManager::Write(int buffer, void* data, size_t size)
{
	TLOG(TLVL_WRITE) << "Write BEGIN";
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}
	auto shmBuf = getBufferInfo_(buffer);
	if (shmBuf == nullptr)
	{
		return -1;
	}
	checkBuffer_(shmBuf, BufferSemaphoreFlags::Writing);
	touchBuffer_(shmBuf);
	TLOG(TLVL_WRITE) << "Buffer Write Pos is " << std::dec << shmBuf->writePos << ", write size is " << size;
	if (shmBuf->writePos + size > shm_ptr_->buffer_size)
	{
		TLOG(TLVL_ERROR) << "Attempted to write more data than fits into Shared Memory, bufferSize=" << std::dec << shm_ptr_->buffer_size
		                 << ",writePos=" << shmBuf->writePos << ",writeSize=" << size;
		Detach(true, "SharedMemoryWrite", "Attempted to write more data than fits into Shared Memory! \nRe-run with a larger buffer size!");
	}

	auto pos = GetWritePos(buffer);
	memcpy(pos, data, size);
	touchBuffer_(shmBuf);
	shmBuf->writePos = shmBuf->writePos + size;

	auto last_seen = last_seen_id_.load();
	while (last_seen < shmBuf->sequence_id && !last_seen_id_.compare_exchange_weak(last_seen, shmBuf->sequence_id)) {}

	TLOG(TLVL_WRITE) << "Write END";
	return size;
}

bool artdaq::SharedMemoryManager::Read(int buffer, void* data, size_t size)
{
	if (buffer >= shm_ptr_->buffer_count)
	{
		Detach(true, "ArgumentOutOfRange", "The specified buffer does not exist!");
	}
	auto shmBuf = getBufferInfo_(buffer);
	if (shmBuf == nullptr)
	{
		return false;
	}
	checkBuffer_(shmBuf, BufferSemaphoreFlags::Reading);
	touchBuffer_(shmBuf);
	if (shmBuf->readPos + size > shm_ptr_->buffer_size)
	{
		TLOG(TLVL_ERROR) << "Attempted to read more data than fits into Shared Memory, bufferSize=" << shm_ptr_->buffer_size
		                 << ",readPos=" << shmBuf->readPos << ",readSize=" << size;
		Detach(true, "SharedMemoryRead", "Attempted to read more data than exists in Shared Memory!");
	}

	auto pos = GetReadPos(buffer);
	TLOG(TLVL_READ) << "Before memcpy in Read(), size is " << size;
	memcpy(data, pos, size);
	TLOG(TLVL_READ) << "After memcpy in Read()";
	auto sts = checkBuffer_(shmBuf, BufferSemaphoreFlags::Reading, false);
	if (sts)
	{
		shmBuf->readPos += size;
		touchBuffer_(shmBuf);
		return true;
	}
	return false;
}

std::string artdaq::SharedMemoryManager::toString()
{
	if (shm_ptr_ == nullptr)
	{
		return "Not connected to shared memory";
	}
	std::ostringstream ostr;
	ostr << "ShmStruct: " << std::endl
	     << "Next ID Number: " << shm_ptr_->next_id << std::endl
	     << "Buffer Count: " << shm_ptr_->buffer_count << std::endl
	     << "Buffer Size: " << std::to_string(shm_ptr_->buffer_size) << " bytes" << std::endl
	     << "Buffers Written: " << std::to_string(shm_ptr_->next_sequence_id) << std::endl
	     << "Rank of Writer: " << shm_ptr_->rank << std::endl
	     << "Writers: " << std::hex << shm_ptr_->writer_mask << std::endl
	     << "Readers: " << std::hex << shm_ptr_->reader_mask << std::endl
	     << "Ready Magic Bytes: " << std::hex << std::showbase << shm_ptr_->ready_magic << std::dec << std::endl
	     << std::endl;

	for (auto ii = 0; ii < shm_ptr_->buffer_count; ++ii)
	{
		auto buf = getBufferInfo_(ii);
		if (buf == nullptr)
		{
			continue;
		}

		ostr << "ShmBuffer " << std::dec << ii << std::endl
		     << "sequenceID: " << std::to_string(buf->sequence_id) << std::endl
		     << "writePos: " << std::to_string(buf->writePos) << std::endl
		     << "readPos: " << std::to_string(buf->readPos) << std::endl
		     << "sem: " << FlagToString(buf->semaphore.load().flags) << std::endl
		     << "Owner: " << std::to_string(buf->semaphore.load().id) << std::endl
		     << "Last Touch Time: " << std::to_string(buf->last_touch_time / 1000000.0) << std::endl
		     << std::endl;
	}

	return ostr.str();
}

void* artdaq::SharedMemoryManager::GetReadPos(int buffer)
{
	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return nullptr;
	}
	return bufferStart_(buffer) + buf->readPos;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}
void* artdaq::SharedMemoryManager::GetWritePos(int buffer)
{
	auto buf = getBufferInfo_(buffer);
	if (buf == nullptr)
	{
		return nullptr;
	}
	return bufferStart_(buffer) + buf->writePos;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

void* artdaq::SharedMemoryManager::GetBufferStart(int buffer)
{
	return bufferStart_(buffer);
}

std::vector<std::pair<int, artdaq::SharedMemoryManager::BufferSemaphoreFlags>> artdaq::SharedMemoryManager::GetBufferReport()
{
	auto output = std::vector<std::pair<int, BufferSemaphoreFlags>>(size());
	for (size_t ii = 0; ii < size(); ++ii)
	{
		auto buf = getBufferInfo_(ii);
		auto semaphore = buf->semaphore.load();
		output[ii] = std::make_pair(semaphore.id, semaphore.flags);
	}
	return output;
}

void artdaq::SharedMemoryManager::GetNewId()
{
	if (manager_id_ < 0 && IsValid()) manager_id_ = shm_ptr_->next_id.fetch_add(1);
	if (manager_id_ > 63)
	{
		TLOG(TLVL_ERROR) << "Too many processes attached to shared memory, writer/reader tracking will be broken! manager_id_=" << manager_id_;
	}
}

bool artdaq::SharedMemoryManager::checkBuffer_(ShmBuffer* buffer, BufferSemaphoreFlags flags, bool exceptions)
{
	if (buffer == nullptr)
	{
		if (exceptions)
		{
			Detach(true, "BufferNotThereException", "Request to check buffer that does not exist!");
		}
		return false;
	}

	auto semaphore = buffer->semaphore.load();
	TLOG(TLVL_CHKBUFFER) << "checkBuffer_: Checking that buffer with SeqID " << buffer->sequence_id << " has sem_id " << manager_id_ << " (Current: " << semaphore.id << ") and is in state "
	                     << FlagToString(flags) << " (current: " << FlagToString(semaphore.flags) << ")";
	if (exceptions)
	{
		if (semaphore.flags != flags)
		{
			Detach(true, "StateAccessViolation", "Shared Memory buffer is not in the correct state! (expected " + FlagToString(flags) + ", actual " + FlagToString(semaphore.flags) + ")");
		}
		if (semaphore.id != manager_id_)
		{
			Detach(true, "OwnerAccessViolation", "Shared Memory buffer is not owned by this manager instance! (Expected: " + std::to_string(manager_id_) + ", Actual: " + std::to_string(semaphore.id) + ")");
		}
	}
	bool ret = (semaphore.id == manager_id_ || (semaphore.id == -1 && (flags == BufferSemaphoreFlags::Full || flags == BufferSemaphoreFlags::Empty))) && semaphore.flags == flags;

	if (!ret)
	{
		TLOG(TLVL_WARNING) << "CheckBuffer detected issue with buffer with SeqID " << buffer->sequence_id << "!"
		                   << " ID: " << semaphore.id << " (Expected " << manager_id_ << "), Flag: " << FlagToString(semaphore.flags) << " (Expected " << FlagToString(flags) << "). "
		                   << R"(ID -1 is okay if expected flag is "Full" or "Empty".)";
	}

	return ret;
}

void artdaq::SharedMemoryManager::touchBuffer_(ShmBuffer* buffer)
{
	if (buffer == nullptr)
	{
		// TLOG(TLVL_CHKBUFFER + 1) << "touchBuffer_: Not touching buffer at " << static_cast<void*>(buffer) << " with sequence_id " << buffer->sequence_id;
		return;
	}
	auto semaphore = buffer->semaphore.load();
	if ((semaphore.id != -1 && semaphore.id != manager_id_))
	{
		// TLOG(TLVL_CHKBUFFER + 1) << "touchBuffer_: Not touching buffer at " << static_cast<void*>(buffer) << " with sequence_id " << buffer->sequence_id;
		return;
	}
	TLOG(TLVL_CHKBUFFER + 1) << "touchBuffer_: Touching buffer at " << static_cast<void*>(buffer) << " with sequence_id " << buffer->sequence_id;
	buffer->last_touch_time = TimeUtils::gettimeofday_us();
}

void artdaq::SharedMemoryManager::Detach(bool throwException, const std::string& category, const std::string& message, bool force)
{
	TLOG(TLVL_DETACH) << "Detach BEGIN: throwException: " << std::boolalpha << throwException << ", force: " << force;
	if (IsValid())
	{
		TLOG(TLVL_DETACH) << "Detach: Resetting owned buffers";
		auto bufs = GetBuffersOwnedByManager();
		for (auto buf : bufs)
		{
			auto shmBuf = getBufferInfo_(buf);
			if (shmBuf == nullptr)
			{
				continue;
			}
			auto semaphore = shmBuf->semaphore.load();
			if (semaphore.flags == BufferSemaphoreFlags::Writing)
			{
				ShmBufferSem release(BufferSemaphoreFlags::Empty, -1);
				shmBuf->semaphore.compare_exchange_strong(semaphore, release);  // Ignoring return code in Detach
			}
			else if (semaphore.flags == BufferSemaphoreFlags::Reading)
			{
				ShmBufferSem release(BufferSemaphoreFlags::Full, -1);
				shmBuf->semaphore.compare_exchange_strong(semaphore, release);  // Ignoring return code in Detach
			}
		}
		UnregisterReader();
		UnregisterWriter();
	}

	if (shm_ptr_ != nullptr)
	{
		TLOG(TLVL_DETACH) << "Detach: Detaching shared memory";
		shmdt(shm_ptr_);
		shm_ptr_ = nullptr;
	}

	if ((force || manager_id_ == 0) && shm_segment_id_ > -1)
	{
		TLOG(TLVL_DETACH) << "Detach: Marking Shared memory for removal";
		shmctl(shm_segment_id_, IPC_RMID, nullptr);
		shm_segment_id_ = -1;
	}

	// Reset manager_id_
	manager_id_ = -1;

	if (!category.empty() && !message.empty())
	{
		TLOG(TLVL_ERROR) << category << ": " << message;

		if (throwException)
		{
			TRACE_CNTL("modeM", (uint64_t)0);
			throw cet::exception(category) << message;  // NOLINT(cert-err60-cpp)
		}
	}
}

// Local Variables:
// mode: c++
// End:
