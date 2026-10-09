#ifndef ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_GENERATORMACROS_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_GENERATORMACROS_HH_

#include "artdaq-core/Plugins/FragmentGenerator.hh"

#include "cetlib/compiler_macros.h"

#include <memory>

namespace fhicl {
class ParameterSet;
}  // namespace fhicl

namespace artdaq {
/**
 * \brief Constructs a FragmentGenerator instance, and returns a pointer to it
 * \param ps Parameter set for initializing the FragmentGenerator
 * \return A smart pointer to the FragmentGenerator
 */
typedef std::unique_ptr<artdaq::FragmentGenerator> makeFunc_t(fhicl::ParameterSet const& ps);
}  // namespace artdaq

#ifndef EXTERN_C_FUNC_DECLARE_START
// NOLINTNEXTLINE(build/define_used)
#define EXTERN_C_FUNC_DECLARE_START extern "C" {
#endif

// NOLINTNEXTLINE(build/define_used)
#define DEFINE_ARTDAQ_GENERATOR(klass)                                    \
	EXTERN_C_FUNC_DECLARE_START                                           \
	std::unique_ptr<artdaq::FragmentGenerator>                            \
	make(fhicl::ParameterSet const& ps)                                   \
	{                                                                     \
		return std::unique_ptr<artdaq::FragmentGenerator>(new klass(ps)); \
	}                                                                     \
	}

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_GENERATORMACROS_HH_
