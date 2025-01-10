#ifndef artdaq_core_Data_ArtdaqMetadata_hh
#define artdaq_core_Data_ArtdaqMetadata_hh

#include <cstdint>
#include <string>
#include <vector>

namespace artdaq {
struct ArtdaqMetadata;
}

/**
 * The ArtdaqMetadata structure represents a generic metadata element that can be sent in BeginRun, BeginSubRun, EndRun, or EndSubRun Fragments, to be included as a run- or subrun-level product in the output art file
 */
struct artdaq::ArtdaqMetadata
{
	int rank{-1};                          ///< Rank of the producing artdaq process
	std::vector<uint16_t> fragment_ids{};  ///< Fragment IDs of the generating process (if any)
	std::string metadata_tag{};            ///< User-defined tag, to help decoding the metadata_string
	std::string metadata_string{};         ///< Unstructured string data
};

#endif  // artdaq_core_Data_ArtdaqMetadata_hh