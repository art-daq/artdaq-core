#ifndef artdaq_core_Data_MetadataFragment_hh
#define artdaq_core_Data_MetadataFragment_hh

#include "artdaq-core/Data/Fragment.hh"
#include "artdaq-core/Data/ArtdaqMetadata.hh"

namespace artdaq {
class MetadataFragment;
}

class artdaq::MetadataFragment
{
public:
	explicit MetadataFragment(artdaq::Fragment const& frag)
	    : artdaq_fragment_(frag){}

	ArtdaqMetadata get_metadata();

	static FragmentPtr CreateMetadataFragment(ArtdaqMetadata metadata, Fragment::type_t type, Fragment::sequence_id_t seqID = 0, Fragment::timestamp_t ts = 0);
	static FragmentPtr CreateEndOfSubrunFragment(int rank, Fragment::sequence_id_t seqID = 0, Fragment::timestamp_t ts = 0) { return CreateMetadataFragment({rank}, Fragment::EndOfSubrunFragmentType, seqID, ts); }
	static FragmentPtr CreateEndOfRunFragment(int rank, Fragment::sequence_id_t seqID = 0, Fragment::timestamp_t ts = 0) { return CreateMetadataFragment({rank}, Fragment::EndOfRunFragmentType, seqID, ts); }
	static FragmentPtr CreateStartOfSubrunFragment(int rank, Fragment::sequence_id_t seqID = 0, Fragment::timestamp_t ts = 0) { return CreateMetadataFragment({rank}, Fragment::StartOfSubrunFragmentType, seqID, ts); }
	static FragmentPtr CreateStartOfRunFragment(int rank, Fragment::sequence_id_t seqID = 0, Fragment::timestamp_t ts = 0) { return CreateMetadataFragment({rank}, Fragment::StartOfRunFragmentType, seqID, ts); }

private:
	artdaq::Fragment const& artdaq_fragment_;
};

#endif