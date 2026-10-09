#ifndef ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_MAKEFRAGMENTGENERATOR_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_MAKEFRAGMENTGENERATOR_HH_
// Using LibraryManager, find the correct library and return an instance
// of the specified generator.

namespace fhicl {
class ParameterSet;
}  // namespace fhicl

#include "artdaq-core/Plugins/FragmentGenerator.hh"

#include <memory>
#include <string>

namespace artdaq {

/**
 * \brief Instantiates the FragmentGenerator plugin with the given name, using the given ParameterSet
 * \param generator_plugin_spec Name of the Generator plugin (omit _generator.so)
 * \param ps The ParameterSet used to initialize the FragmentGenerator
 * \return A smart pointer to the FragmentGenerator instance
 */
std::unique_ptr<FragmentGenerator>
makeFragmentGenerator(std::string const& generator_plugin_spec,
                      fhicl::ParameterSet const& ps);
}  // namespace artdaq
#endif  // ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_MAKEFRAGMENTGENERATOR_HH_
