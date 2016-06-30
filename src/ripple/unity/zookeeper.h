#ifndef RIPPLE_ZOOKEEPER_H_INCLUDED
#define RIPPLE_ZOOKEEPER_H_INCLUDED

#if USE_ZOOKEEPER

#include <boost/thread/tss.hpp>
#include <zookeeper/zookeeper.h>

namespace ripple
{
class ZkConn
{
public:
    struct Setup
    {
        std::string hosts;
        int32_t recvTimeout = 5000;
    };

    ZkConn (const Setup& setup, beast::Journal& journal)
        : m_setup (setup),
          m_journal (journal),
          m_connection (NULL)
    {
        open ();
    }

    bool isOpen ()
    {
        auto state = zoo_state (m_connection);
        if (state == ZOO_CONNECTING_STATE || state == ZOO_CONNECTED_STATE)
            return true;
        return false;
    }

    void open ()
    {
        if (isOpen ())
            return;

        for (int i = 0; i < 3; i++)
        {
            close ();
            m_connection = zookeeper_init (m_setup.hosts.c_str (), NULL, m_setup.recvTimeout, NULL, NULL, 0);
            if (m_connection)
                return;

            int errsv = errno;
            m_journal.error << "zookeeper_init failed with errno " << errsv << ": " << strerror (errsv);
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        throw std::runtime_error (std::string ("Connect to zookeeper failed"));
    }

    void close ()
    {
        auto conn = m_connection.exchange (NULL);
        if (conn)
            zookeeper_close (conn);
    }

    zhandle_t* getConnection ()
    {
        return m_connection;
    }

    ~ZkConn ()
    {
        close ();
    }

private:
    const Setup& m_setup;
    beast::Journal m_journal;
    std::atomic<zhandle_t*> m_connection;
};

class ZkConnFactory
{
public:
    ZkConnFactory (Section const& keyValues, beast::Journal journal)
        : m_journal (journal)
    {
        m_setup.hosts = get<std::string> (keyValues, "hosts");
        if (m_setup.hosts.empty ())
            throw std::runtime_error ("Missing hosts for zookeeper");

        if (keyValues.exists ("recv_timeout"))
            m_setup.recvTimeout = get<int> (keyValues, "recv_timeout");

        if (keyValues.exists ("log_level"))
        {
            ZooLogLevel level = ZOO_LOG_LEVEL_WARN;
            auto levelStr = get<std::string> (keyValues, "log_level");
            if (levelStr == "info")
                level = ZOO_LOG_LEVEL_INFO;
            else if (levelStr == "debug")
                level = ZOO_LOG_LEVEL_DEBUG;
            else if (levelStr == "error")
                level = ZOO_LOG_LEVEL_ERROR;
            zoo_set_debug_level (level);
        }
    }

    zhandle_t* getConnection ()
    {
        auto conn = m_connection.get ();
        if (!conn)
        {
            conn = new ZkConn (m_setup, m_journal);
            m_connection.reset (conn);
        }
        else if (!conn->isOpen ())
        {
            conn->open ();
        }
        return conn->getConnection ();
    }

    void release ()
    {
        m_connection.reset ();
    }

private:
    std::unique_ptr<ZkConn> m_connection;
    ZkConn::Setup m_setup;
    beast::Journal m_journal;
};
};

#endif

#endif
