#ifndef ARTDAQ_CORE_ARTDAQ_CORE_DATA_DETAIL_PARENTAGEMAP_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_DATA_DETAIL_PARENTAGEMAP_HH_

#include "canvas/Persistency/Provenance/Parentage.h"
#include "canvas/Persistency/Provenance/ParentageID.h"
#include "canvas/Persistency/Provenance/ProcessHistory.h"
#include "canvas/Persistency/Provenance/ProcessHistoryID.h"

#include <map>
#include <utility>

namespace art {
/// An art::ParentageMap, defined using a std::map
typedef std::map<ParentageID const, Parentage> ParentageMap;
typedef std::pair<ParentageID, Parentage> ParentagePair;
typedef std::pair<ParentageID const, Parentage> ConstParentagePair;
typedef std::pair<ProcessHistoryID, ProcessHistory> HistoryPair;
}  // namespace art

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_DATA_DETAIL_PARENTAGEMAP_HH_
