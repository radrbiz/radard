#include <BeastConfig.h>

#include <ripple/unity/thrift.h>

#if RIPPLE_THRIFT_AVAILABLE

// Compile RocksDB without debugging unless specifically requested
#if !defined (NDEBUG) && !defined (RIPPLE_DEBUG_THRIFT)
#define NDEBUG
#endif

#include "thrift/lib/cpp/src/thrift/server/TNonblockingServer.cpp"
#include "thrift/lib/cpp/src/thrift/async/TAsyncProtocolProcessor.cpp"
#include "thrift/lib/cpp/src/thrift/async/TEvhttpServer.cpp"
#include "thrift/lib/cpp/src/thrift/async/TEvhttpClientChannel.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TZlibTransport.cpp"

#include <ripple/thrift/gen-cpp/Hbase.cpp>
#include <ripple/thrift/gen-cpp/hbase_constants.cpp>
#include <ripple/thrift/gen-cpp/hbase_types.cpp>

#include <ripple/thrift/HBaseLedgerSaver.cpp>

#endif
