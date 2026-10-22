#ifndef ARTDAQ_CORE_ARTDAQ_CORE_DATA_PACKAGEBUILDINFO_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_DATA_PACKAGEBUILDINFO_HH_

#include <string>

namespace artdaq {

/**
 * \brief Class holding information about the _artdaq_ package build.
 *
 * The PackageBuildInfo class contains the name of the package, the version,
 * and the timestamp of the build. _artdaq_ stores this information in each
 * data file.
 */
class PackageBuildInfo
{
public:
	/**
	 * \brief Default Constructor
	 */
	PackageBuildInfo() {}

	/**
	 * \brief Gets the package name
	 * \return The package name
	 */
	std::string getPackageName() const { return packageName_; }

	/**
	 * \brief Gets the package version
	 * \return The package version
	 */
	std::string getPackageVersion() const { return packageVersion_; }

	/**
	 * \brief Gets the build timestamp
	 * \return The timestamp of the build
	 */
	std::string getBuildTimestamp() const { return buildTimestamp_; }

	/**
	 * \brief Sets the package name
	 * \param str The package name
	 */
	void setPackageName(std::string const& str) { packageName_ = str; }

	/**
	 * \brief Sets the package version
	 * \param str The package version
	 */
	void setPackageVersion(std::string const& str) { packageVersion_ = str; }

	/**
	 * \brief Sets the build timestamp
	 * \param str The timestamp of the build
	 */
	void setBuildTimestamp(std::string const& str) { buildTimestamp_ = str; }

private:
	/**
	 * \brief The name of the package
	 */
	std::string packageName_;
	/**
	 * \brief The version of the package
	 */
	std::string packageVersion_;
	/**
	 * \brief Timestamp of the build for this package
	 */
	std::string buildTimestamp_;
};
}  // namespace artdaq
#endif  // ARTDAQ_CORE_ARTDAQ_CORE_DATA_PACKAGEBUILDINFO_HH_
