#include "artdaq-core/Data/MetadataFragment.hh"

artdaq::ArtdaqMetadata artdaq::MetadataFragment::get_metadata()
{
	ArtdaqMetadata output;

	auto ptr = reinterpret_cast<uint8_t const*>(artdaq_fragment_.dataBeginBytes());
	
	size_t element_size = *reinterpret_cast<size_t const*>(ptr);
	ptr += sizeof(size_t);
	assert(element_size == sizeof(output.rank));
	output.rank = *reinterpret_cast<typeof(output.rank) const*>(ptr);
	ptr += element_size;
	
	element_size = *reinterpret_cast<size_t const*>(ptr);
	ptr += sizeof(size_t);
	assert(element_size % sizeof(uint16_t) == 0);
	if (element_size > 0)
	{
		output.fragment_ids.resize(element_size / sizeof(uint16_t));
		memcpy(output.fragment_ids.data(), ptr, element_size);
		ptr += element_size;
	}

	element_size = *reinterpret_cast<size_t const*>(ptr);
	ptr += sizeof(size_t);
	if (element_size > 0)
	{
		output.metadata_tag = std::string(reinterpret_cast<char const*>(ptr));
		ptr += element_size;
	}

	element_size = *reinterpret_cast<size_t const*>(ptr);
	ptr += sizeof(size_t);
	if (element_size > 0)
	{
		output.metadata_string = std::string(reinterpret_cast<char const*>(ptr));
		ptr += element_size;
	}

	return output;
}

artdaq::FragmentPtr artdaq::MetadataFragment::CreateMetadataFragment(artdaq::ArtdaqMetadata metadata, artdaq::Fragment::type_t type, artdaq::Fragment::sequence_id_t seqID, artdaq::Fragment::timestamp_t ts)
{
	size_t fragmentSizeBytes = sizeof(size_t) * 4 + sizeof(metadata.rank) + metadata.fragment_ids.size() * sizeof(uint16_t) + (metadata.metadata_tag.size() + 1) + (metadata.metadata_string.size() + 1);

	artdaq::FragmentPtr frag(new artdaq::Fragment(static_cast<size_t>(
	    ceil(fragmentSizeBytes / static_cast<double>(sizeof(artdaq::Fragment::value_type))))));

	frag->setSystemType(type);
	frag->setSequenceID(seqID);
	frag->setTimestamp(ts);

	uint8_t* ptr = reinterpret_cast<uint8_t*>(frag->dataBegin());

	size_t element_size = sizeof(metadata.rank);
	memcpy(ptr, &element_size, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(ptr, &metadata.rank, element_size);
	ptr += element_size;

	element_size = metadata.fragment_ids.size() * sizeof(uint16_t);
	memcpy(ptr, &element_size, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(ptr, metadata.fragment_ids.data(), element_size);
	ptr += element_size;

	element_size = metadata.metadata_tag.size() + 1;
	memcpy(ptr, &element_size, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(ptr, metadata.metadata_tag.c_str(), element_size);
	ptr += element_size;

	element_size = metadata.metadata_string.size() + 1;
	memcpy(ptr, &element_size, sizeof(size_t));
	ptr += sizeof(size_t);
	memcpy(ptr, metadata.metadata_string.c_str(), element_size);
	ptr += element_size;

	return frag;
}