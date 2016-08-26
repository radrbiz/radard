//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>

#if RIPPLE_THRIFT_AVAILABLE

#include <ripple/app/main/Application.h>
#include <ripple/core/Config.h> // VFALCO Bad dependency
#include <ripple/nodestore/Factory.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/BatchWriter.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <beast/threads/Thread.h>
#include <atomic>
#include <memory>
#include <boost/format.hpp>

#include <ripple/thrift/HBaseConn.h>

namespace ripple {
namespace NodeStore {

class HbaseBackend
    : public Backend
    , public BatchWriter::Callback
{
private:
    std::atomic <bool> m_deletePath;
    beast::Journal m_journal;
    size_t const m_keyBytes;
    Scheduler& m_scheduler;
    BatchWriter m_batch;

    static constexpr auto s_tableName =     SYSTEM_NAMESPACE ":NodeStore";
    static constexpr auto s_columnFamily =  "d:";
    static constexpr auto s_columnName =    "d:v";
    
    HBaseConnFactory m_hbaseFactory;

public:
    HbaseBackend (int keyBytes, Section const& keyValues,
        Scheduler& scheduler, beast::Journal journal)
        : m_deletePath (false)
        , m_journal (journal)
        , m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_hbaseFactory (keyValues, journal)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;

        // create table if not exists.
        try
        {
            std::string table (s_tableName);
            std::vector<ColumnDescriptor> columns;
            columns.push_back (ColumnDescriptor ());
            columns.back ().name = s_columnFamily;
            columns.back ().maxVersions = 1;
            columns.back ().compression = "SNAPPY";
//            columns.back ().inMemory = true;
            columns.back ().blockCacheEnabled = true;
            columns.back ().bloomFilterType = "ROW";
//            columns.back ().timeToLive = 3 * 24 * 3600;
            getConnection ()->m_client->createTable (table, columns);
        }
        catch (const apache::hadoop::hbase::thrift::AlreadyExists& ae)
        {
            m_journal.info << "Table exists: " << ae.message;
        }
        catch (const TException& te)
        {
            throw std::runtime_error (std::string ("Unable to open/create Hbase: ") + te.what ());
        }
    }

    ~HbaseBackend ()
    {
        close();
    }

    void
    close() override
    {
    }

    std::string
    getName()
    {
        return "hbase";
    }

    HBaseConn* getConnection ()
    {
        return m_hbaseFactory.getConnection ();
    }

    void releaseConnection ()
    {
        m_hbaseFactory.release ();
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, std::shared_ptr<NodeObject>* pObject)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        
        pObject->reset ();

        Status status (ok);
        for (;;)
        {
        try
        {
            std::vector<TRowResult> rowResult;
            std::map<Text, Text> attributes;
            std::string row = to_string (uint256::fromVoid (key));
            getConnection ()->m_client->getRow (rowResult, s_tableName, row, attributes);
            if (rowResult.empty ())
            {
                status = notFound;
            }
            else if (rowResult.size () != 1)
            {
                status = dataCorrupt;
                if (m_journal.error)
                    m_journal.error << rowResult.size () << " objects found for NodeObject #" << row;
            }
            else
            {
                auto& columns = rowResult.front ().columns;
                if (columns.find (s_columnName) == columns.end ())
                {
                    status = notFound;
                    if (m_journal.error)
                        m_journal.error << "row found but column not found for NodeObject #" << row;
                }
                else
                {
                    auto& data = columns[s_columnName].value;
                    DecodedBlob decoded (key, data.data (), data.size ());

                    if (decoded.wasOk ())
                    {
                        *pObject = decoded.createObject ();
                    }
                    else
                    {
                        // Decoding failed, probably corrupted!
                        //
                        status = dataCorrupt;
                    }
                }
            }
            break;
        }
        catch (TApplicationException& tae)
        {
            if (tae.getType () == TApplicationException::MISSING_RESULT)
                status = notFound;
            else
            {
                status = Status (customCode + tae.getType ());
                m_journal.error << tae.what () << "(TApplicationException) getting NodeObject #" << uint256::fromVoid (key);
            }
        }
        catch (const transport::TTransportException& tte)
        {
            status = tte.getType () == transport::TTransportException::CORRUPTED_DATA ? dataCorrupt : Status (customCode + tte.getType ());
            m_journal.error << tte.what () << "(TTransportException) getting NodeObject #" << uint256::fromVoid (key);
            releaseConnection ();
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        catch (const TException& te)
        {
            status = Status (customCode);
            m_journal.error << te.what () << " getting NodeObject #" << uint256::fromVoid (key);
            releaseConnection ();
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        }
        return status;
    }

    bool canFetchBatch () { return true; }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch (std::size_t n, void const* const* keys) override
    {
        throw std::runtime_error("pure virtual called");
        return {};
    }
    
    uint32_t
    fetchBatchLimit ()
    {
        return m_hbaseFactory.getSetup ().fetchBatchLimit;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, std::set<uint256>>
    fetchBatch (const std::set<uint256>& hashes)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        
        std::vector<std::shared_ptr<NodeObject>> objects;
        std::set<uint256> hashesNotFound (hashes);

        std::vector<TRowResult> rowResults;
        std::map<Text, Text> attributes;
        std::vector<std::string> rows;
        for (auto& hash : hashes)
            rows.emplace_back (to_string (hash));

        for (int i = 0; i < 3; ++i)
        {
            try
            {
                getConnection ()->m_client->getRows (rowResults, s_tableName, rows, attributes);
                for (auto& row : rowResults)
                {
                    auto& columns = row.columns;
                    if (columns.find (s_columnName) == columns.end ())
                    {
                        if (m_journal.error)
                            m_journal.error << "row found but column not found for NodeObject #" << row.row;
                    }
                    else
                    {
                        auto& data = columns[s_columnName].value;
                        uint256 key = from_hex_text<uint256> (row.row);
                        DecodedBlob decoded (key.data (), data.data (), data.size ());

                        if (decoded.wasOk ())
                        {
                            objects.emplace_back (decoded.createObject ());
                            hashesNotFound.erase (objects.back ()->getHash ());
                        }
                        else
                        {
                            // Decoding failed, probably corrupted!
                            //
                            if (m_journal.fatal)
                                m_journal.fatal << "Corrupt NodeObject #" << row.row;
                        }
                    }
                }
                break;
            }
            catch (TApplicationException& tae)
            {
                m_journal.error << tae.what () << "(TApplicationException) getting " << hashes.size () << " NodeObjects, code " << tae.getType ();
            }
            catch (const transport::TTransportException& tte)
            {
                m_journal.error << tte.what () << "(TTransportException) getting " << hashes.size () << " NodeObjects, code " << tte.getType ();
                releaseConnection ();
                std::this_thread::sleep_for (std::chrono::seconds (1));
            }
            catch (const TException& te)
            {
                m_journal.error << te.what () << " getting " << hashes.size () << " NodeObjects";
                releaseConnection ();
                std::this_thread::sleep_for (std::chrono::seconds (1));
            }
        }
        return std::make_pair (objects, hashesNotFound);
    }

    void
    store (std::shared_ptr<NodeObject> const& object)
    {
        m_batch.store (object);
    }

    void
    storeBatch (Batch const& batch)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        
        std::vector<BatchMutation> rowBatches;

        EncodedBlob encoded;

        for (auto const& e : batch)
        {
            encoded.prepare (e);
            
            std::vector<Mutation> mutations;
            mutations.clear ();
            mutations.push_back (Mutation ());
            mutations.back ().column = s_columnName;
            mutations.back ().value.assign (static_cast<const char*> (encoded.getData ()), encoded.getSize ());

            rowBatches.push_back (BatchMutation ());
            rowBatches.back ().row = to_string (uint256::fromVoid (encoded.getKey ()));
            rowBatches.back ().mutations = mutations;
        }

        //for (int i = 0; i < 3; i++)
        for (;;)
        {
            try
            {
                std::map<Text, Text> attributes;
                getConnection ()->m_client->mutateRows (s_tableName, rowBatches, attributes);
                return;
            }
            catch (const TException& te)
            {
                m_journal.error << "storeBatch failed: " << te.what ();
                releaseConnection ();
                std::this_thread::sleep_for (std::chrono::seconds (1));
            }
        }
        throw std::runtime_error ("storeBatch failed");
    }

    void
    for_each (std::function <void(std::shared_ptr<NodeObject>)> f)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        
        auto scan = TScan ();
        scan.caching = 1000;

        std::map<Text, Text> attributes;

        auto scanner = getConnection ()->m_client->scannerOpenWithScan(s_tableName, scan, attributes);
        
        std::vector<TRowResult> rowList;

        for (;;)
        {
            getConnection ()->m_client->scannerGetList (rowList, scanner, 100);
            if (rowList.empty ())
                break;
            
            for (auto& row : rowList)
            {
                if (row.row.size () != m_keyBytes * 2)
                {
                    // VFALCO NOTE What does it mean to find an
                    //             incorrectly sized key? Corruption?
                    if (m_journal.fatal)
                        m_journal.fatal << "Bad key size = " << row.row.size ();
                    continue;
                }

                auto& columns = row.columns;
                if (columns.find (s_columnName) == columns.end ())
                {
                    if (m_journal.fatal)
                        m_journal.fatal << "column not found for NodeObject #" << row.row;
                    continue;
                }

                auto& data = columns[s_columnName].value;
                uint256 key = from_hex_text<uint256> (row.row);
                DecodedBlob decoded (key.data (),
                                     data.data (),
                                     data.size ());

                if (decoded.wasOk ())
                {
                    f (decoded.createObject ());
                }
                else
                {
                    // Uh oh, corrupted data!
                    if (m_journal.fatal)
                        m_journal.fatal << "Corrupt NodeObject #" << row.row;
                }
            }
        }
    }

    int
    getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    void
    setDeletePath() override
    {
        m_deletePath = true;
    }

    //--------------------------------------------------------------------------

    void
    writeBatch (Batch const& batch)
    {
        storeBatch (batch);
    }

    void
    verify() override
    {
    }
};

//------------------------------------------------------------------------------

class HbaseFactory : public Factory
{
public:
    HbaseFactory ()
    {
        Manager::instance().insert(*this);
    }

    ~HbaseFactory ()
    {
        Manager::instance().erase(*this);
    }

    std::string
    getName () const
    {
        return "Hbase";
    }

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <HbaseBackend> (
            keyBytes, keyValues, scheduler, journal);
    }
};

static HbaseFactory hbaseFactory;

}
}

#endif
