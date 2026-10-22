#ifndef ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_ARTDAQFRAGMENTNAMEHELPER_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_ARTDAQFRAGMENTNAMEHELPER_HH_

#include "artdaq-core/Data/Fragment.hh"
#include "artdaq-core/Plugins/FragmentNameHelper.hh"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace artdaq {
/**
 * @brief Default implementation of FragmentNameHelper
 */
class ArtdaqFragmentNameHelper : public FragmentNameHelper
{
public:
	/**
	 * \brief DefaultArtdaqFragmentNameHelper Destructor
	 */
	virtual ~ArtdaqFragmentNameHelper();

	/**
	 * @brief ArtdaqFragmentNameHelper Constructor
	 * @param unidentified_instance_name Name to use for unidentified Fragments
	 * @param extraTypes Additional types to register
	 */
	ArtdaqFragmentNameHelper(std::string unidentified_instance_name, std::vector<std::pair<artdaq::Fragment::type_t, std::string>> extraTypes);

private:
	ArtdaqFragmentNameHelper(ArtdaqFragmentNameHelper const&) = delete;
	ArtdaqFragmentNameHelper(ArtdaqFragmentNameHelper&&) = delete;
	ArtdaqFragmentNameHelper& operator=(ArtdaqFragmentNameHelper const&) = delete;
	ArtdaqFragmentNameHelper& operator=(ArtdaqFragmentNameHelper&&) = delete;
};
}  // namespace artdaq

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_PLUGINS_ARTDAQFRAGMENTNAMEHELPER_HH_
