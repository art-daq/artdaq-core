#ifndef ARTDAQ_CORE_ARTDAQ_CORE_DATA_DICTIONARYCONTROL_HH_
#define ARTDAQ_CORE_ARTDAQ_CORE_DATA_DICTIONARYCONTROL_HH_

// This header defines the CPP symbol HIDE_FROM_ROOT. This symbol
// can be used to hide code from Root dictionaries, as shown below
//
// Usage:
//
//   #if HIDE_FROM_ROOT
//      void function_that_you_want_to_hide() { ... };
//      void another_function_to_hide() {...};
//   #endif
//

#undef HIDE_FROM_ROOT
#if !defined(__GCCXML__) && !defined(__ROOTCLING__)  //&& defined(__GXX_EXPERIMENTAL_CXX0X__)
// NOLINTNEXTLINE
#define HIDE_FROM_ROOT 1
#endif

#endif  // ARTDAQ_CORE_ARTDAQ_CORE_DATA_DICTIONARYCONTROL_HH_
