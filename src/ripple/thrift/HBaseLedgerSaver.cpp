#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/thrift/HBaseConn.h>

namespace ripple
{
class HBaseLedgerSaver
{
private:
    // Hbase table defines.
    std::string s_tableLocks = "Rd:Locks";  // LedgerData
    std::string s_tableLedgers = "Rd:Ledgers";  // LedgerData
    std::string s_tableTxs = "Rd:Txs";  // Raw & meta data
    std::string s_tableTxIndex = "Rd:TxIdx";    // Indexes for Hash -> Ledger,TxnSeq

    boost::format s_keyTxs = boost::format ("%X%u-%u-%u"); // Row Key format: [Hex(LedgerSeq%16)][LedgerSeq]-[TxnType]-[TxnSeq]

    std::string s_columnFamily = "d:";

    std::string s_columnRaw = "d:r";
    std::string s_columnMeta = "d:m";
    
    std::string s_columnValue = "d:v";

    std::string s_columnHash = "d:h";
    std::string s_columnClosingTime = "d:ct";
    std::string s_columnPrevHash = "d:ph";
    std::string s_columnAccountSetHash = "d:ah";
    std::string s_columnTransSetHash = "d:th";
    std::string s_columnVRP = "d:vrp";
    std::string s_columnVBC = "d:vbc";

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

    static bool setup ()
    {
        auto doSetup = [](Application& app) {
            if (!app.config ().exists (SECTION_TX_DB_HBASE))
                return true;

            try
            {
                typedef boost::signals2::signal<bool(std::shared_ptr<Ledger const> const&)> signal_type;

                // new HBaseLedgerSaver
                boost::shared_ptr<HBaseLedgerSaver> hbaseLedgerSaver (new HBaseLedgerSaver (app));

                // connect it to signal SaveValidated
                LedgerMaster::signals ().SaveValidated.connect (
                    signal_type::slot_type (&HBaseLedgerSaver::onSaveValidatedLedger,
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
        };
        Application::signals ().Setup.connect (doSetup);
        return true;
    }

private:

    class HBaseLock
    {
    private:
        std::string m_rowKey;
        HBaseLedgerSaver* m_ledgerSaver;
        bool m_locked = false;

    public:
        HBaseLock (const std::string& rowKey, HBaseLedgerSaver* ledgerSaver)
            : m_rowKey (rowKey), m_ledgerSaver (ledgerSaver)
        {
        }
        ~HBaseLock ()
        {
            if (m_locked)
                unlock ();
        }

        bool lock ()
        {
            using namespace apache::thrift;
            using namespace apache::hadoop::hbase::thrift;
            try
            {
                Mutation mput;
                mput.column = m_ledgerSaver->s_columnValue;
                mput.value = "1";
                std::map<Text, Text> attributes;
                while (!m_ledgerSaver->getConnection ()->m_client->checkAndPut (
                    m_ledgerSaver->s_tableLocks, m_rowKey, mput.column, "", mput, attributes))
                {
                    JLOG (m_ledgerSaver->m_journal.debug) << "wait for lock";
                    std::this_thread::sleep_for (std::chrono::milliseconds (100));
                }
            }
            catch (const TException& te)
            {
                JLOG (m_ledgerSaver->m_journal.error) << "get lock failed, " << te.what ();
                return false;
            }
            m_locked = true;
            return true;
        }

        bool unlock ()
        {
            using namespace apache::thrift;
            using namespace apache::hadoop::hbase::thrift;
            for (int i = 0; i < 2; i++)
            {
                try
                {
                    std::map<Text, Text> attributes;
                    m_ledgerSaver->getConnection ()->m_client->deleteAllRow (
                        m_ledgerSaver->s_tableLocks, m_rowKey, attributes);
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

        // get a lock to write this ledger
        static boost::format rowKeyFormat ("ls-%u");
        HBaseLock hbaseLock (boost::str (rowKeyFormat % ledgerSeq), this);
        if (!hbaseLock.lock ())
            return false;

        // check if already in hbase
        {
            std::vector<TCell> cells;
            std::map<Text, Text> attributes;
            try
            {
                getConnection ()->m_client->get (
                    cells, s_tableLedgers, ledgerSeqStr, s_columnHash, attributes);
            }
            catch (const TException& te)
            {
                JLOG (m_journal.error) << "ledger check failed, " << te.what ();
                return false;
            }

            if (!cells.empty ())
            {
                if (cells.size () == 1 && cells[0].value == ledgerHash)
                {
                    // already in hbase
                    JLOG (m_journal.info) << "already saved";
                    return true;
                }
                else
                {
                    // mismatch ledger in ledgers, delete it
                    JLOG (m_journal.warning) << "mismatch hash " << cells[0].value << " got for " << ledgerSeq;
                    try
                    {
                        getConnection ()->m_client->deleteAllRow (
                            s_tableLedgers, ledgerSeqStr, attributes);
                    }
                    catch (const TException& te)
                    {
                        JLOG (m_journal.error) << "delete mismatch ledger failed, " << te.what ();
                        return false;
                    }
                }
            }
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

        // delete txs begin with this LedgerSeq if exists
        try
        {
            JLOG (m_journal.debug) << "scanning dirty txs";
            std::map<Text, Text> attributes;
            std::vector<Text> columns;
            static boost::format prefix ("%X%u-");
            auto scanner = getConnection ()->m_client->scannerOpenWithPrefix (
                s_tableTxs, boost::str (prefix % (ledgerSeq % 16) % ledgerSeq), columns, attributes);

            std::vector<TRowResult> rowList;
            for (;;)
            {
                getConnection ()->m_client->scannerGetList (rowList, scanner, 1024);
                if (rowList.empty ())
                    break;
                JLOG (m_journal.debug) << "deleting " << rowList.size () << " dirty txs";
                std::vector<BatchMutation> rowBatches;
                for (auto& row : rowList)
                {
                    rowBatches.push_back (BatchMutation ());
                    rowBatches.back ().row = row.row;
                    auto& mutations = rowBatches.back ().mutations;
                    mutations.push_back (Mutation ());
                    mutations.back ().isDelete = true;
                }
                getConnection ()->m_client->mutateRows (s_tableTxs, rowBatches, attributes);
            }

            getConnection ()->m_client->scannerClose (scanner);
            JLOG (m_journal.debug) << "scanning dirty txs done";
        }
        catch (const TException& te)
        {
            JLOG (m_journal.error) << "clear from hbase failed, " << te.what ();
            return false;
        }

        // write txs
        std::vector<BatchMutation> txsBatches;
        std::vector<BatchMutation> txIndexBatches;
        for (auto const& vt : aLedger->getMap ())
        {
            uint256 transactionID = vt.second->getTransactionID ();

            m_app.getMasterTransaction ().inLedger (
                transactionID, ledgerSeq);

            std::string const rowKey (boost::str (
                s_keyTxs % (ledgerSeq % 16) % ledgerSeq % vt.second->getTxnType () % vt.second->getTxnSeq ()));

            // mutations to table Txs
            {
                txsBatches.push_back (BatchMutation ());
                txsBatches.back ().row = rowKey;

                auto& mutations = txsBatches.back ().mutations;

                mutations.push_back (Mutation ());
                mutations.back ().column = s_columnRaw;
                Serializer s;
                vt.second->getTxn ()->add (s);
                mutations.back ().value.assign (s.getString ());
                
                mutations.push_back (Mutation ());
                mutations.back ().column = s_columnMeta;
                mutations.back ().value.assign (vt.second->getRawMeta ());
            }

            // mutations to table TxIndex
            {
                txIndexBatches.push_back (BatchMutation ());
                txIndexBatches.back ().row = to_string (transactionID);

                auto& mutations = txIndexBatches.back ().mutations;

                mutations.push_back (Mutation ());
                mutations.back ().column = s_columnValue;
                mutations.back ().value.assign (rowKey);
            }
        }
        
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

        for (int i = 0; i < 3; i++)
        {
            try
            {
                std::map<Text, Text> attributes;
                getConnection ()->m_client->mutateRows (
                    s_tableTxs, txsBatches, attributes);
                getConnection ()->m_client->mutateRows (
                    s_tableTxIndex, txIndexBatches, attributes);
                getConnection ()->m_client->mutateRow (
                    s_tableLedgers, ledgerSeqStr, ledgerMutations, attributes);
                JLOG (m_journal.info) << "done";
                return true;
            }
            catch (const TException& te)
            {
                JLOG (m_journal.error) << "save failed, " << te.what ();
            }
        }
        JLOG (m_journal.error) << "fail to save " << ledgerSeq;
        return false;
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
        columns.back ().timeToLive = 3;
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

static auto hbaseLedgerSaverSetup = HBaseLedgerSaver::setup ();
}