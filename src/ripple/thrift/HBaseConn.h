#ifndef RIPPLE_THRIFT_HBASECONNECTION_H_INCLUDED
#define RIPPLE_THRIFT_HBASECONNECTION_H_INCLUDED

#if RIPPLE_THRIFT_AVAILABLE

#include <boost/thread/tss.hpp>
#include <vector>

#include <ripple/unity/thrift.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/protocol/TCompactProtocol.h>
#include <thrift/transport/TSocketPool.h>
#include <thrift/transport/TTransportUtils.h>

#include <ripple/thrift/gen-cpp/Hbase.h>

namespace ripple
{
class HBaseConn
{
public:
    struct Setup
    {
        std::vector<std::pair<std::string, int>> hosts;
        bool isCompactProtocol;
        int32_t fetchBatchLimit = 4096;
        int32_t connTimeout = 5000;
        int32_t sendTimeout = 5000;
        int32_t recvTimeout = 5000;
    };
    
    std::unique_ptr<apache::hadoop::hbase::thrift::HbaseClient> m_client;

    HBaseConn (Setup setup, beast::Journal journal)
        : m_journal (journal)
    {
        using namespace apache::thrift;
        using namespace apache::thrift::protocol;
        using namespace apache::hadoop::hbase::thrift;

        auto socket = new transport::TSocketPool (setup.hosts);
        if (socket)
        {
            socket->setConnTimeout (setup.connTimeout);
            socket->setSendTimeout (setup.sendTimeout);
            socket->setRecvTimeout (setup.recvTimeout);
            socket->setRetryInterval (3);
        }
        m_socket.reset (socket);
        m_transport.reset (new transport::TBufferedTransport (m_socket));
        if (setup.isCompactProtocol)
            m_protocol.reset (new TCompactProtocol (m_transport));
        else
            m_protocol.reset (new TBinaryProtocol (m_transport));
        m_client.reset (new HbaseClient (m_protocol));

        open ();
    }
    
    bool isOpen()
    {
        return m_transport->isOpen ();
    }

    void open ()
    {
        using namespace apache::thrift;
        
        for (int i = 0; i < 3; i++)
        {
            try
            {
                m_transport->open ();
                if (m_transport->isOpen ())
                    return;

                m_journal.warning << "Connect to hbase failed";
                m_transport->close ();
            }
            catch (const transport::TTransportException& tte)
            {
                m_journal.error << "Open transport failed: " << tte.what () << " code " << tte.getType ();
            }
            catch (const TException& te)
            {
                m_journal.error << "Open transport failed: " << te.what ();
            }
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        throw std::runtime_error (std::string ("Connect to hbase failed"));
    }

    apache::hadoop::hbase::thrift::HbaseClient& getClient ()
    {
        return *m_client;
    }

    ~HBaseConn ()
    {
        if (m_transport)
        {
            try
            {
                m_transport->close ();
            }
            catch (const apache::thrift::transport::TTransportException& tte)
            {
                m_journal.error << "Close transport failed: " << tte.what () << " code " << tte.getType ();
            }
        }
    }

private:
    boost::shared_ptr<apache::thrift::transport::TTransport> m_socket;
    boost::shared_ptr<apache::thrift::transport::TTransport> m_transport;
    boost::shared_ptr<apache::thrift::protocol::TProtocol> m_protocol;
    beast::Journal m_journal;
};

class HBaseConnFactory
{
public:
    HBaseConnFactory (Section const& keyValues, beast::Journal journal)
        : m_journal (journal)
    {
        m_setup.isCompactProtocol = get<std::string> (keyValues, "protocol").compare ("compact") == 0;

        int port = get<int> (keyValues, "port", 9090);
        if (port <= 0)
            throw std::runtime_error ("Bad port in HbaseFactory backend");

        std::string hosts = get<std::string> (keyValues, "host");
        if (hosts.empty ())
            throw std::runtime_error ("Missing host in HbaseFactory backend");

        std::stringstream ss (hosts);
        std::string host;
        while (std::getline (ss, host, ','))
        {
            m_setup.hosts.push_back ({host, port});
        }

        if (keyValues.exists ("fetch_batch_max"))
        {
            m_setup.fetchBatchLimit = get<int> (keyValues, "fetch_batch_max");
            if (m_setup.fetchBatchLimit <= 0)
                throw std::runtime_error ("Bad fetch_batch_max in HbaseFactory backend");
        }
        
        if (keyValues.exists ("conn_timeout"))
            m_setup.connTimeout = get<int>(keyValues, "conn_timeout");

        if (keyValues.exists ("send_timeout"))
            m_setup.sendTimeout = get<int>(keyValues, "send_timeout");

        if (keyValues.exists ("recv_timeout"))
            m_setup.recvTimeout = get<int>(keyValues, "recv_timeout");
    }

    HBaseConn* getConnection ()
    {
        auto conn = m_connection.get ();
        if (!conn)
        {
            conn = new HBaseConn (m_setup, m_journal);
            m_connection.reset (conn);
        }
        else if (!conn->isOpen ())
        {
            conn->open ();
        }
        return conn;
    }

    void release ()
    {
        m_connection.reset ();
    }
    
    const HBaseConn::Setup& getSetup ()
    {
        return m_setup;
    }

private:
    boost::thread_specific_ptr<HBaseConn> m_connection;
    HBaseConn::Setup m_setup;
    beast::Journal m_journal;
};
}

#endif
#endif