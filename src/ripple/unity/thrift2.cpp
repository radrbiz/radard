#include <BeastConfig.h>

#include <ripple/unity/thrift.h>

#if RIPPLE_THRIFT_AVAILABLE

// Compile RocksDB without debugging unless specifically requested
#if !defined (NDEBUG) && !defined (RIPPLE_DEBUG_THRIFT)
#define NDEBUG
#endif

#include "thrift/lib/cpp/src/thrift/transport/TServerSocket.cpp"
#include "thrift/lib/cpp/src/thrift/server/TSimpleServer.cpp"
#include "thrift/lib/cpp/src/thrift/server/TThreadPoolServer.cpp"
#include "thrift/lib/cpp/src/thrift/server/TThreadedServer.cpp"

#endif
