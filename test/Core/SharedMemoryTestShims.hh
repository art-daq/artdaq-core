#ifndef ARTDAQ_CORE_TEST_CORE_SHAREDMEMORYTESTSHIMS_HH_
#define ARTDAQ_CORE_TEST_CORE_SHAREDMEMORYTESTSHIMS_HH_

#include "artdaq-core/Utilities/TimeUtils.hh"

#include <random>

inline key_t GetRandomKey(uint16_t identifier)
{
	static std::mt19937 rng(artdaq::TimeUtils::gettimeofday_us());
	static std::uniform_int_distribution<key_t> gen(0x00000000, 0x0000FFFF);
	return gen(rng) + (identifier << 16) + getpid();
}

#endif  // ARTDAQ_CORE_TEST_CORE_SHAREDMEMORYTESTSHIMS_HH_
