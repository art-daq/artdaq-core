#ifndef ARTDAQ_CORE_ARTDAQ_CORE_DATA_ARTDAQMETADATA_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_DATA_ARTDAQMETADATA_HH_

#include <cstdint>
#include <string>
#include <vector>

namespace artdaq {

/**
 * The ArtdaqMetadata structure represents a generic metadata element that can be sent in BeginRun, BeginSubRun, EndRun, or EndSubRun Fragments, to be included as a run- or subrun-level product in the output art file
 */
struct ArtdaqMetadata
{
	int rank{-1};                          ///< Rank of the producing artdaq process
	std::vector<uint16_t> fragment_ids{};  ///< Fragment IDs of the generating process (if any)
	std::string metadata_tag{};            ///< User-defined tag, to help decoding the metadata_string
	std::string metadata_string{};         ///< Unstructured string data
};
}  // namespace artdaq

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_DATA_ARTDAQMETADATA_HH_
