#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/thrift/HBaseConn.h>
#include <boost/make_shared.hpp>

namespace ripple
{
class HBaseLedgerSaver : Application::SetupListener<HBaseLedgerSaver>
{
private:
    // Hbase table defines.
    static constexpr auto s_tableLocks =    SYSTEM_NAMESPACE ":Locks";  // LedgerData
    static constexpr auto s_tableLedgers =  SYSTEM_NAMESPACE ":Ledgers";// LedgerData
    static constexpr auto s_tableTxs =      SYSTEM_NAMESPACE ":Txs";    // Raw & meta data
    static constexpr auto s_tableTxIndex =  SYSTEM_NAMESPACE ":TxIdx";  // Indexes for Hash -> Ledger,TxnSeq

    boost::format s_keyTxs = boost::format ("%X%u-%u-%u"); // Row Key format: [Hex(LedgerSeq%16)][LedgerSeq]-[TxnType]-[TxnSeq]

    static constexpr auto s_columnFamily =          "d:";

    static constexpr auto s_columnRaw =             "d:r";
    static constexpr auto s_columnMeta =            "d:m";
    
    static constexpr auto s_columnValue =           "d:v";

    static constexpr auto s_columnHash =            "d:h";
    static constexpr auto s_columnClosingTime =     "d:ct";
    static constexpr auto s_columnPrevHash =        "d:ph";
    static constexpr auto s_columnAccountSetHash =  "d:ah";
    static constexpr auto s_columnTransSetHash =    "d:th";
    static constexpr auto s_columnVRP =             "d:vrp";
    static constexpr auto s_columnVBC =             "d:vbc";

public:
    HBaseLedgerSaver (Application& app)
        : m_app (app),
          m_journal (app.journal ("HBaseLedgerSaver")),
          m_hbaseFactory(app.config ().section (SECTION_TX_DB_HBASE), m_journal)
    {
        initTables ();
    }

    ~HBaseLedgerSaver ()
    {
    }

    static bool onSetup (Application& app)
    {
        if (!app.config ().exists (SECTION_TX_DB_HBASE))
            return true;

        try
        {
            // new HBaseLedgerSaver
            static boost::shared_ptr<HBaseLedgerSaver> hbaseLedgerSaver = boost::make_shared<HBaseLedgerSaver> (app);

            // connect it to signal SaveValidated
            typedef decltype(LedgerMaster::Signals::SaveValidated)::slot_type slot_type;
            LedgerMaster::signals ().SaveValidated.connect (
                slot_type (&HBaseLedgerSaver::onSaveValidatedLedger,
                           hbaseLedgerSaver.get (), _1)
                    .track (hbaseLedgerSaver));

            JLOG (app.journal ("HBaseLedgerSaver").info) << "done";
        }
        catch (const std::exception& e)
        {
            JLOG (app.journal ("HBaseLedgerSaver").error) << e.what ();
            return false;
        }

        return true;
    }

private:

    class HBaseLock
    {
    private:
        std::string m_rowKey;
        HBaseLedgerSaver* m_ledgerSaver;
        std::atomic<bool> m_locked;

    public:
        HBaseLock (const std::string& rowKey, HBaseLedgerSaver* ledgerSaver)
            : m_rowKey (rowKey), m_ledgerSaver (ledgerSaver), m_locked (false)
        {
        }
        ~HBaseLock ()
        {
            if (m_locked)
                unlock ();
        }

        void lock () noexcept
        {
            using namespace apache::thrift;
            using namespace apache::hadoop::hbase::thrift;
            Mutation mput;
            mput.column = s_columnValue;
            mput.value = "1";
            std::map<Text, Text> attributes;
            for (;;)
            {
                try
                {
                    if (m_ledgerSaver->getConnection ()->m_client->checkAndPut (
                            s_tableLocks, m_rowKey, mput.column, "", mput, attributes))
                    {
                        m_locked = true;
                        return;
                    }
                }
                catch (const TException& te)
                {
                    JLOG (m_ledgerSaver->m_journal.error) << "get lock failed, " << te.what ();
                }
                JLOG (m_ledgerSaver->m_journal.debug) << "wait for lock";
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
        }

        bool unlock () noexcept
        {
            using namespace apache::thrift;
            using namespace apache::hadoop::hbase::thrift;
            for (int i = 0; i < 2; i++)
            {
                try
                {
                    std::map<Text, Text> attributes;
                    m_ledgerSaver->getConnection ()->m_client->deleteAllRow (
                        s_tableLocks, m_rowKey, attributes);
                    m_locked = false;
                    return true;
                }
                catch (const TException& te)
                {
                    JLOG (m_ledgerSaver->m_journal.error) << "release lock failed, " << te.what ();
                }
            }
            return false;
        }
    };

    bool onSaveValidatedLedger (std::shared_ptr<Ledger const> const& ledger)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;

        auto const& ledgerSeq = ledger->info ().seq;
        auto const ledgerSeqStr = to_string (ledgerSeq);
        auto const ledgerHash = to_string (ledger->info ().hash);

        JLOG (m_journal.info) << "saving ledger " << ledgerSeq;

        // Return if already in hbase. Will check again later if someone wrote it
        // during we are proparing writing. So retry on exception loop is not needed.
        try
        {
            if (isSaved (ledgerSeqStr, ledgerHash))
                return true;
        }
        catch (const TException& te)
        {
            JLOG (m_journal.fatal) << ledgerSeq << " isSaved checking failed, " << te.what ();
        }

        // get AcceptedLedger
        AcceptedLedger::pointer aLedger;
        try
        {
            aLedger = m_app.getAcceptedLedgerCache ().fetch (ledger->info ().hash);
            if (!aLedger)
            {
                aLedger = std::make_shared<AcceptedLedger> (ledger, m_app.accountIDCache (), m_app.logs ());
                m_app.getAcceptedLedgerCache ().canonicalize (ledger->info ().hash, aLedger);
            }
        }
        catch (std::exception const&)
        {
            JLOG (m_journal.warning) << "An accepted ledger was missing nodes";
            return false;
        }

        std::unordered_map<std::string, std::vector<BatchMutation>> mutations;

        prepareTxs (mutations, aLedger, ledgerSeq);

        // mutations to table Ledgers
        std::vector<Mutation> ledgerMutations;
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnHash;
        ledgerMutations.back ().value.assign (ledgerHash);
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnPrevHash;
        ledgerMutations.back ().value.assign (to_string (ledger->info ().parentHash));
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnAccountSetHash;
        ledgerMutations.back ().value.assign (to_string (ledger->info().accountHash));
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnTransSetHash;
        ledgerMutations.back ().value.assign (to_string (ledger->info().txHash));
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnClosingTime;
        ledgerMutations.back ().value.assign (to_string (ledger->info ().closeTime));
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnVRP;
        ledgerMutations.back ().value.assign (to_string (ledger->info ().drops));
        ledgerMutations.push_back (Mutation ());
        ledgerMutations.back ().column = s_columnVBC;
        ledgerMutations.back ().value.assign (to_string (ledger->info ().dropsVBC));

        for (;;)
        {
            // get a lock to write this ledger
            static boost::format rowKeyFormat ("ls-%u");
            try
            {
                HBaseLock hbaseLock (boost::str (boost::format (rowKeyFormat) % ledgerSeq), this);
                hbaseLock.lock ();

                // check again if already in hbase
                if (isSaved (ledgerSeqStr, ledgerHash))
                    return true;

                for (auto const& mutation : mutations)
                {
                    std::map<Text, Text> attributes;
                    getConnection ()->m_client->mutateRows (
                        mutation.first, mutation.second, attributes);
                }

                std::map<Text, Text> attributes;
                getConnection ()->m_client->mutateRow (
                    s_tableLedgers, ledgerSeqStr, ledgerMutations, attributes);
                JLOG (m_journal.info) << "done";
                return true;
            }
            catch (const TException& te)
            {
                JLOG (m_journal.fatal) << ledgerSeq << " save failed, " << te.what ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        JLOG (m_journal.error) << "fail to save " << ledgerSeq;
        return false;
    }

    bool isSaved (const std::string& ledgerSeqStr, const std::string& ledgerHash) noexcept(false)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;

        std::vector<TCell> cells;
        std::map<Text, Text> attributes;

        getConnection ()->m_client->get (
            cells, s_tableLedgers, ledgerSeqStr, s_columnHash, attributes);

        if (cells.empty ())
            return false;

        if (cells.size () == 1 && cells[0].value == ledgerHash)
        {
            // already in hbase
            JLOG (m_journal.info) << "already saved";
            return true;
        }
        else
        {
            JLOG (m_journal.fatal) << "mismatch hash " << cells[0].value << " got for " << ledgerSeqStr;
            return false;
        }
    }

    void prepareTxs (std::unordered_map<std::string, std::vector<apache::hadoop::hbase::thrift::BatchMutation>>& mutations,
                     AcceptedLedger::pointer aLedger,
                     const LedgerIndex& ledgerSeq) noexcept
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        // write txs
        std::vector<BatchMutation>& txsBatches = mutations[s_tableTxs];
        std::vector<BatchMutation>& txIndexBatches = mutations[s_tableTxIndex];
        for (auto const& vt : aLedger->getMap ())
        {
            uint256 transactionID = vt.second->getTransactionID ();

            m_app.getMasterTransaction ().inLedger (
                transactionID, ledgerSeq);

            std::string const rowKey (boost::str (
                boost::format (s_keyTxs) % (ledgerSeq % 16) % ledgerSeq % vt.second->getTxnType () % vt.second->getTxnSeq ()));

            // mutations to table Txs
            mutationTxs (txsBatches, rowKey, vt.second);

            // mutations to table TxIndex
            mutationTxIndex (txIndexBatches, rowKey, transactionID);
        }
    }

    void mutationTxs (std::vector<apache::hadoop::hbase::thrift::BatchMutation>& txsBatches,
                      std::string const& rowKey,
                      AcceptedLedgerTx::pointer vt)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        txsBatches.push_back (BatchMutation ());
        txsBatches.back ().row = rowKey;

        auto& mutations = txsBatches.back ().mutations;

        mutations.push_back (Mutation ());
        mutations.back ().column = s_columnRaw;
        Serializer s;
        vt->getTxn ()->add (s);
        mutations.back ().value.assign (s.getString ());

        mutations.push_back (Mutation ());
        mutations.back ().column = s_columnMeta;
        mutations.back ().value.assign (vt->getRawMeta ());
    }

    void mutationTxIndex (std::vector<apache::hadoop::hbase::thrift::BatchMutation>& txIndexBatches,
                          std::string const& rowKey,
                          uint256 const& transactionID)
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;
        txIndexBatches.push_back (BatchMutation ());
        txIndexBatches.back ().row = to_string (transactionID);

        auto& mutations = txIndexBatches.back ().mutations;

        mutations.push_back (Mutation ());
        mutations.back ().column = s_columnValue;
        mutations.back ().value.assign (rowKey);
    }

private:
    Application& m_app;
    beast::Journal m_journal;
    HBaseConnFactory m_hbaseFactory;

private:
    HBaseConn* getConnection ()
    {
        return m_hbaseFactory.getConnection ();
    }

    void initTables ()
    {
        using namespace apache::thrift;
        using namespace apache::hadoop::hbase::thrift;

        std::vector<ColumnDescriptor> columns;
        columns.push_back (ColumnDescriptor ());
        columns.back ().name = s_columnFamily;
        columns.back ().maxVersions = 1;
        columns.back ().compression = "SNAPPY";
        columns.back ().blockCacheEnabled = true;
        columns.back ().bloomFilterType = "ROW";

        // create table if not exists.
        for (auto& tableName : {s_tableTxs, s_tableTxIndex, s_tableLedgers})
        {
            try
            {
                getConnection ()->m_client->createTable (tableName, columns);
            }
            catch (const AlreadyExists& ae)
            {
                JLOG (m_journal.debug) << "Table " << tableName << " exists, " << ae.message;
            }
            catch (const TException& te)
            {
                JLOG (m_journal.error) << "Create table " << tableName << " failed, " << te.what ();
                throw std::runtime_error (te.what ());
            }
        }

        columns.clear ();
        columns.push_back (ColumnDescriptor ());
        columns.back ().name = s_columnFamily;
        columns.back ().maxVersions = 1;
        columns.back ().inMemory = true;
        columns.back ().blockCacheEnabled = true;
        columns.back ().timeToLive = 30;
        try
        {
            getConnection ()->m_client->createTable (s_tableLocks, columns);
        }
        catch (const AlreadyExists& ae)
        {
            JLOG (m_journal.debug) << "Table " << s_tableLocks << " exists, " << ae.message;
        }
        catch (const TException& te)
        {
            JLOG (m_journal.error) << "Create table " << s_tableLocks << " failed, " << te.what ();
            throw std::runtime_error (te.what ());
        }
    }
};

}
