# ======================================================================
#
# Build/install script
#
# ======================================================================

# ----------------------------------------------------------------------
# Build this project's library:

cet_make_library(SOURCE
  MonitoredQuantity.cc
  SharedMemoryEventReceiver.cc
  SharedMemoryFragmentManager.cc
  SharedMemoryManager.cc
  StatisticsCollection.cc
  LIBRARIES
  PUBLIC
	artdaq-core::Data
  artdaq-core::Utilities
	Boost::thread
  Threads::Threads
  PRIVATE
  artdaq-core::Utilities_TraceLock
	cetlib_except::cetlib_except
  TRACE::TRACE
)

install_headers()
install_source()
