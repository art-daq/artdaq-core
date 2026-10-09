#ifndef ARTDAQ_CORE_ARTDAQ_CORE_BUILDINFO_GETPACKAGEBUILDINFO_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_BUILDINFO_GETPACKAGEBUILDINFO_HH_

#include "artdaq-core/Data/PackageBuildInfo.hh"

/**
 * \brief Namespace used to differentiate the artdaq_core version of GetPackageBuildInfo
 * from other versions present in the system.
 */
namespace artdaqcore {
/**
 * \brief Wrapper around the artdaqcore::GetPackageBuildInfo::getPackageBuildInfo function
 */
struct GetPackageBuildInfo
{
	/**
	 * \brief Gets the version number and build timestmap for artdaq_core
	 * \return An artdaq::PackageBuildInfo object containing the version number and build timestamp for artdaq_core
	 */
	static artdaq::PackageBuildInfo getPackageBuildInfo();
};
}  // namespace artdaqcore

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_BUILDINFO_GETPACKAGEBUILDINFO_HH_
