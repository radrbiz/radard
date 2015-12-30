#include <BeastConfig.h>

#include <ripple/unity/thrift.h>

#if RIPPLE_THRIFT_AVAILABLE

// Compile RocksDB without debugging unless specifically requested
#if !defined (NDEBUG) && !defined (RIPPLE_DEBUG_THRIFT)
#define NDEBUG
#endif

#include "thrift/lib/cpp/src/thrift/Thrift.cpp"
#include "thrift/lib/cpp/src/thrift/TApplicationException.cpp"
#include "thrift/lib/cpp/src/thrift/VirtualProfiling.cpp"
#include "thrift/lib/cpp/src/thrift/concurrency/ThreadManager.cpp"
#include "thrift/lib/cpp/src/thrift/concurrency/TimerManager.cpp"
#include "thrift/lib/cpp/src/thrift/concurrency/Util.cpp"
#include "thrift/lib/cpp/src/thrift/protocol/TDebugProtocol.cpp"
#include "thrift/lib/cpp/src/thrift/protocol/TDenseProtocol.cpp"
#include "thrift/lib/cpp/src/thrift/protocol/TJSONProtocol.cpp"
#include "thrift/lib/cpp/src/thrift/protocol/TBase64Utils.cpp"
#include "thrift/lib/cpp/src/thrift/protocol/TMultiplexedProtocol.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TTransportException.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TFDTransport.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TFileTransport.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TSimpleFileTransport.cpp"
#include "thrift/lib/cpp/src/thrift/transport/THttpTransport.cpp"
#include "thrift/lib/cpp/src/thrift/transport/THttpClient.cpp"
#include "thrift/lib/cpp/src/thrift/transport/THttpServer.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TSocket.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TPipe.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TPipeServer.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TSSLSocket.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TSocketPool.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TSSLServerSocket.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TTransportUtils.cpp"
#include "thrift/lib/cpp/src/thrift/transport/TBufferTransports.cpp"
#include "thrift/lib/cpp/src/thrift/server/TServer.cpp"
#include "thrift/lib/cpp/src/thrift/async/TAsyncChannel.cpp"
#include "thrift/lib/cpp/src/thrift/processor/PeekProcessor.cpp"

#include "thrift/lib/cpp/src/thrift/concurrency/Mutex.cpp"
#include "thrift/lib/cpp/src/thrift/concurrency/Monitor.cpp"
#include "thrift/lib/cpp/src/thrift/concurrency/PosixThreadFactory.cpp"

#endif
