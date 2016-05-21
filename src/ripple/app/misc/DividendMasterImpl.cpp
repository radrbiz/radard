#if (defined (_WIN32) || defined (_WIN64))
#include <Psapi.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/depth_first_search.hpp>

#include <beast/threads/RecursiveMutex.h>

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
//#include <ripple/app/misc/DefaultMissingNodeHandler.h>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/json/to_string.h>
#include <ripple/server/Role.h>
#include <ripple/rpc/impl/TransactionSign.h>

namespace ripple {
        
static inline uint64_t memUsed(void) {
#if (defined (_WIN32) || defined (_WIN64))
    //CARL windows implementation
    HANDLE hProcess = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS pmc;
    BOOL bOk = GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc));
    return pmc.WorkingSetSize / (1024 * 1024);
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss/1024;
#endif
}
    
class DividendMasterImpl : public DividendMaster
{
public:
    DividendMasterImpl (Application& app, beast::Journal journal)
        : app_ (app), m_journal (journal)
    {
    }

    AccountsDividend& getDivResult()
    {
        return m_divResult;
    }

    SHAMapHash getResultHash() override
    {
        return m_resultHash;
    }
    
    void setResultHash(SHAMapHash hash) override
    {
        m_resultHash = hash;
    }
    
    int getDividendState() override
    {
        return m_dividendState;
    }
    
    void setDividendState(int dividendState) override
    {
        m_dividendState = dividendState;
    }
    
    /// calculate coins to be divided.
    /// @return [dropsVRP, dropsVBC] to be divided.
    std::tuple<uint64_t, uint64_t> getDividendCoins(Ledger::pointer ledger)
    {
        // Make decision total coins and coinsVBC
        std::uint64_t dividendCoins = VRP_INCREASE_RATE * ledger->info ().dropsVBC.drops () / VRP_INCREASE_RATE_PARTS;

        if (dividendCoins + ledger->info ().drops > VRP_INCREASE_MAX)
        {
            dividendCoins = 0;
        }

        uint64_t dividendCoinsVBC = 0;
        if (ledger->info ().dropsVBC < VBC_INCREASE_MAX)
        {
            if (ledger->info ().closeTime < VBC_DIVIDEND_PERIOD_1)
            {
                dividendCoinsVBC = VBC_INCREASE_RATE_1 * ledger->info ().dropsVBC.drops () / VBC_INCREASE_RATE_1_PARTS;
            }
            else if (ledger->info ().closeTime < VBC_DIVIDEND_PERIOD_2)
            {
                dividendCoinsVBC = VBC_INCREASE_RATE_2 * ledger->info ().dropsVBC.drops () / VBC_INCREASE_RATE_2_PARTS;
            }
            else if (ledger->info ().closeTime < VBC_DIVIDEND_PERIOD_3)
            {
                dividendCoinsVBC = VBC_INCREASE_RATE_3 * ledger->info ().dropsVBC.drops () / VBC_INCREASE_RATE_3_PARTS;
            }
            else
            {
                dividendCoinsVBC = VBC_INCREASE_RATE_4 * ledger->info ().dropsVBC.drops () / VBC_INCREASE_RATE_4_PARTS;
            }
            if (dividendCoinsVBC + ledger->info ().dropsVBC > VBC_INCREASE_MAX)
                dividendCoinsVBC = 0;
        }
        m_dividendTotalCoins = dividendCoins;
        m_dividendTotalCoinsVBC = dividendCoinsVBC;
        return std::make_tuple (dividendCoins, dividendCoinsVBC);
    }

    /// Prepare #m_accountsByBalance and #m_accountsGraph for dividend.
    void prepareAccounts (Ledger::pointer ledger)
    {
        // Accounts that are under minimal VBC requirement and with no
        // reference yet.
        std::unordered_map<AccountID, std::shared_ptr<AccountData>> accountsUnqualified;

        auto& accounts = m_accounts;
        accounts.clear ();
        auto& accountsByBalance = m_accountsByBalance;
        accountsByBalance.clear ();
        auto& accountsGraph = m_accountsGraph;
        accountsGraph.clear ();

        /// push an AccountData into #accounts and #accountsGraph.
        /// @return std::pair<iterator in accounts, true as succeeded>.
        auto pushAccount = [&](const std::shared_ptr<AccountData>& accountData)
        {
            auto result = accounts.emplace (accountData->account, accountData);
            if (result.second)
                accountData->vertex = boost::add_vertex ({accountData}, accountsGraph);
            return result;
        };

        /// Visit all AccountRoots to fill #accounts, #accountsGraph and #accountsByBalance.
        auto accountVisitor = [&](SLE::ref sle)
        {
            if (sle->getType () != ltACCOUNT_ROOT)
                return;

            uint64_t vbc = sle->getFieldAmount (sfBalanceVBC).mantissa ();

            auto account = sle->getAccountID (sfAccount);
            auto accountParent = sle->getAccountID (sfReferee);
            bool noParent = !accountParent;

            auto iter = accounts.find (account);
            
            // root account stats
            if (noParent)
            {
                JLOG (m_journal.info) << "Root account " << account;
            }
            
            if (iter != accounts.end ())
            {
                // Already qualified as referee.
                iter->second->vbc = vbc;
            }
            else if (vbc < SYSTEM_CURRENCY_PARTS_VBC && noParent)
            {
                // Not qualified, store in accountsUnqualified temporary.
                accountsUnqualified.emplace (account, std::make_shared<AccountData> (account, vbc));
                return;
            }
            else
            {
                // Qualified.
                bool result;
                std::tie (iter, result) = pushAccount (std::make_shared<AccountData> (account, vbc));
                if (!result)
                {
                    // this should not happen.
                    JLOG (m_journal.fatal) << "Can not get AccountData for " << account;
                }
            }

            if (vbc >= SYSTEM_CURRENCY_PARTS_VBC)
            {
                // Qualified for vRank calc, add to accountsByBalance.
                accountsByBalance.emplace (vbc, iter->second);
            }

            // Add an edge [referee->reference] in graph.
            if (!noParent)
            {
                auto iterParent = accounts.find (accountParent);
                if (iterParent == accounts.end ())
                {
                    // Not in accounts yet, try move from accountsUnqualified or construct an later-init one.
                    std::shared_ptr<AccountData> accountData;
                    auto iterUnqualified = accountsUnqualified.find (accountParent);
                    if (iterUnqualified != accountsUnqualified.end ())
                    {
                        accountData = iterUnqualified->second;
                        accountsUnqualified.erase (iterUnqualified);
                    }
                    else
                    {
                        accountData.reset (new AccountData (accountParent, 0));
                    }

                    std::tie (iterParent, std::ignore) = pushAccount (accountData);
                    if (iterParent == accounts.end ())
                    {
                        // this should not happen.
                        JLOG (m_journal.fatal) << "Can not get AccountData for " << sle->getAccountID (sfAccount);
                    }
                }
                auto& parentPtr = iterParent->second;
                add_edge (parentPtr->vertex, iter->second->vertex, accountsGraph);
                iter->second->parent = parentPtr->vertex;
            }
        };

        ledger->visitStateItems (accountVisitor);

        JLOG (m_journal.info) << accountsUnqualified.size () << " unqualified accounts found. Mem " << memUsed ();
    }

    bool calcDividend (const uint32_t ledgerIndex) override
    {
        Ledger::pointer ledger = app_.getLedgerMaster ().getLedgerBySeq (ledgerIndex);

        if (!ledger)
        {
            if (m_journal.error)
                m_journal.error << "Ledger " << ledgerIndex << " not found.";
            return false;
        }

        auto ledgerSeq = ledger->info ().seq;

        if (m_journal.info)
            m_journal.info << "Got ledger " << ledgerSeq << " Mem " << memUsed ();

        std::uint64_t dividendCoins, dividendCoinsVBC;
        std::tie (dividendCoins, dividendCoinsVBC) = getDividendCoins (ledger);
        if (dividendCoins == 0 && dividendCoinsVBC == 0)
        {
            if (m_journal.warning)
                m_journal.warning << "Can not start a dividend because both VRP and VBC will exceed max.";
            return true;
        }

        if (m_journal.info)
            m_journal.info << "Expected dividend: " << dividendCoins << " " << dividendCoinsVBC << " for ledger " << ledgerSeq << ". Mem " << memUsed ();

        prepareAccounts (ledger);

        if (m_journal.info)
            m_journal.info << m_accountsByBalance.size () << " accounts found for ranking, " << m_accounts.size () << " accounts for sprd. Mem " << memUsed ();

        ledger.reset ();

        // Calculate
        uint64_t actualTotalDividend = 0, actualTotalDividendVBC = 0,
                 sumVRank = 0, sumVSpd = 0;

        getDivResult ().clear ();

        calcDividend (dividendCoins, dividendCoinsVBC,
                      actualTotalDividend, actualTotalDividendVBC,
                      sumVRank, sumVSpd);

        return true;
    }
    std::pair<bool, Json::Value> checkDividend (const uint32_t ledgerIndex, const std::string hash) override;
    bool launchDividend (uint32_t const ledgerIndex) override;

    void calcDividend (uint64_t dividendCoins, uint64_t dividendCoinsVBC, uint64_t& actualTotalDividend, uint64_t& actualTotalDividendVBC, uint64_t& sumVRank, uint64_t& sumVSpd);

    bool dumpTransactionMap (const uint32_t ledgerIndex, const std::string& hash) override;
    
    void getMissingTxns() override;

private:
    Application& app_;
    beast::Journal m_journal;

    AccountsDividend m_divResult;
    SHAMapHash m_resultHash;
    uint64_t m_dividendTotalCoins;
    uint64_t m_dividendTotalCoinsVBC;
    uint64_t m_dividendVRank;
    uint64_t m_dividendVSprd;
    int m_dividendState = DivType_Start;
    
    class AccountData
    {
    public:
        typedef std::shared_ptr<AccountData> Pointer;
        struct Property
        {
            Pointer data;
        };
        typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, Property> Graph;
        typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;

        AccountData (const AccountID& accountId, uint64_t balanceVBC)
            : account (accountId), vbc (balanceVBC) {}
        AccountID account;
        uint64_t vbc = 0;
        uint64_t vRank = 0, vSprd = 0, tSprd = 0;
        uint64_t maxChildHolding = 0;
        Vertex vertex = boost::graph_traits<Graph>::null_vertex ();
        Vertex parent = boost::graph_traits<Graph>::null_vertex ();
    };

    class Visitor : public boost::default_dfs_visitor
    {
    public:
        typedef std::function<void(AccountData::Vertex, const AccountData::Graph&)> VertexFunc;

        Visitor (VertexFunc finish_vertex)
            : m_finish_vertex (finish_vertex)
        {
        }

        template <typename Vertex, typename Graph>
        void finish_vertex (Vertex vertex, const Graph& accountsGraph)
        {
            m_finish_vertex (vertex, accountsGraph);
        }

    private:
        VertexFunc m_finish_vertex;
    };

    /// Reference Graph
    AccountData::Graph m_accountsGraph;

    /// Accounts with enough VBC or references.
    std::unordered_map<AccountID, std::shared_ptr<AccountData>> m_accounts;

    /// Accounts to calculate vRank. Sorted by balance.
    std::multimap<uint64_t, std::shared_ptr<AccountData>> m_accountsByBalance;
};

void DividendMasterImpl::getMissingTxns ()
{
    if (app_.getOPs ().isNeedNetworkLedger ())
    {
        return;
    }
    
    beast::Journal journal(app_.journal("DividendMaster"));
    auto const& curLedger = app_.getLedgerMaster ().getCurrentLedger ();
    auto const& dividendObj = curLedger->read (keylet::dividend ());
    if (!dividendObj)
        return;

    SHAMapHash fullHash (dividendObj->getFieldH256 (sfDividendHash));

    std::shared_ptr<SHAMap> fullDivMap = std::make_shared<SHAMap> (
        SHAMapType::TRANSACTION,
        fullHash.as_uint256(),
        app_.family ());
    std::vector<SHAMapNodeID> nodeIDs;
    std::vector<uint256> nodeHashes;
    nodeIDs.reserve (1);
    nodeHashes.reserve (1);
    if (fullDivMap->fetchRoot (fullHash, nullptr))
    {
        fullDivMap->getMissingNodes(nodeIDs, nodeHashes, 1, nullptr);
        if (!nodeIDs.empty())
        {
            journal.fatal << "Dividend job, full dividend map is empty.";
            return;
        }
    }
    else
    {
        journal.fatal << "Dividend job, fetch full root hash failed.";
        return;
    }
    
//    SHAMap const& transMap = curLedger->txMap ();
    std::string secret_key = get<std::string> (app_.config ()[SECTION_DIVIDEND_ACCOUNT], "secret_key");
    RippleAddress secret = RippleAddress::createSeedGeneric (secret_key);
    RippleAddress generator = RippleAddress::createGeneratorPublic (secret);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (generator, secret, 0);
    
    uint32_t dividendLedger = dividendObj->getFieldU32 (sfDividendLedger);
    uint256 prevTxnID = dividendObj->getFieldH256 (sfDividendMarker);
    auto txnIter = fullDivMap->upper_bound (prevTxnID);
    if (txnIter != fullDivMap->end ())
        txnIter++;

    uint256 lastTxn = prevTxnID;
    int shots = 256;
    int passes = 1;

    JLOG(journal.info) << "Dividend job, begin submit, dividend state " << getDividendState();
    while (shots > 0 && getDividendState() == DividendMaster::DivType_Start)
    {
        if (txnIter == fullDivMap->end ())
        {
            txnIter = fullDivMap->begin ();
        }
        auto const& item = *txnIter;
        txnIter++;

        JLOG(journal.trace) << "Dividend job, prev transaction " << prevTxnID;
        JLOG(journal.trace) << "Dividend job, this transaction " << item.key ();
        prevTxnID = item.key ();
        
        auto sitTrans = SerialIter{item.data (), item.size ()};
        std::shared_ptr<STTx> stpTrans = std::make_shared<STTx> (std::ref (sitTrans));
        
        auto accountSLE = curLedger->read (keylet::account (stpTrans->getAccountID (sfDestination)));
        if (accountSLE->getFieldU32 (sfDividendLedger) == dividendLedger)
        {
            if (lastTxn == item.key ())
            {
                setDividendState(DividendMaster::DivType_Done);
                JLOG(journal.debug) << "Dividend job, finish last txn " << lastTxn;
                break;
            }
            JLOG(journal.trace) << "Dividend job, account: "<< stpTrans->getAccountID (sfDestination) << " dividend ledger: " << accountSLE->getFieldU32 (sfDividendLedger);
            JLOG(journal.trace) << "Dividend job, "<< passes << " pass applied transaction " << item.key ();
            passes++;
            continue;
        }
        try
        {
            shots--;
            lastTxn = item.key ();
            stpTrans->sign(naAccountPrivate);
            JLOG(journal.debug) << "Dividend job, submit tx " << lastTxn << " with signed tx id " << stpTrans->getTransactionID();
            app_.getOPs ().submitTransaction (stpTrans);

        }
        catch (std::runtime_error& e)
        {
            JLOG(journal.debug) << "Duplicate transaction applied" << e.what ();
        }
    }
    JLOG(journal.info) << "Dividend job, ends submit, passes " << passes << ", dividend state " << getDividendState();
}

static inline uint64_t adjust (uint64_t coin)
{
    return coin >= 10000000000 ? coin + 90000000000 : coin * 10;
}

void DividendMasterImpl::calcDividend (uint64_t dividendCoins, uint64_t dividendCoinsVBC, uint64_t& actualTotalDividend, uint64_t& actualTotalDividendVBC, uint64_t& sumVRank, uint64_t& sumVSpd)
{
    auto& accountsOut = m_divResult;
    accountsOut.clear ();
    const auto& accountsByBalance = m_accountsByBalance;
    const auto& accountsGraph = m_accountsGraph;

    if (accountsByBalance.empty () && num_edges (accountsGraph) == 0)
    {
        actualTotalDividend = 0;
        actualTotalDividendVBC = 0;
        sumVRank = 0;
        sumVSpd = 0;
        return;
    }

    // traverse accountsByBalance to caculate V ranking into VRank in accountsByReference
    sumVRank = 0;
    {
        uint64_t lastBalance = 0;
        uint32_t pos = 1, rank = 1;
        for (auto it = accountsByBalance.begin (); it != accountsByBalance.end (); ++pos, ++it)
        {
            if (lastBalance < it->first)
            {
                rank = pos;
                lastBalance = it->first;
            }
            it->second->vRank = rank;
            sumVRank += rank;
        }
        m_dividendVRank = sumVRank;
    }
    JLOG (m_journal.info) << "calcDividend got v rank total: " << sumVRank << " Mem " << memUsed ();


    // traverse accountsTree to caculate V spreading into VSpd
    sumVSpd = 0;
    {
        Visitor::VertexFunc finishVertex = [&](AccountData::Vertex vertex, const AccountData::Graph& accountsGraph)
        {
            auto& accountData = *accountsGraph[vertex].data;

            if (accountData.vSprd != 0)
            {
                // Qualified for vSprd calc
                if (accountData.vbc >= SYSTEM_CURRENCY_PARTS_VBC)
                {
                    accountData.vSprd = accountData.vSprd - adjust (accountData.maxChildHolding) + static_cast<uint64_t> (pow (accountData.maxChildHolding / SYSTEM_CURRENCY_PARTS_VBC, 1.0 / 3) * SYSTEM_CURRENCY_PARTS_VBC);
                    sumVSpd += accountData.vSprd;
                }
            }

            auto& t = accountData.tSprd;

            t += accountData.vbc;

            if (accountData.parent == boost::graph_traits<AccountData::Graph>::null_vertex ())
                return;

            auto& parentData = *accountsGraph[accountData.parent].data;

            parentData.tSprd += t;
            if (parentData.vbc >= SYSTEM_CURRENCY_PARTS_VBC)
            {
                parentData.vSprd += adjust (t);

                if (parentData.maxChildHolding < t)
                    parentData.maxChildHolding = t;
            }
        };
        boost::depth_first_search (accountsGraph, boost::visitor (Visitor (finishVertex)));
        m_dividendVSprd = sumVSpd;
    }
    JLOG (m_journal.info) << "calcDividend got v spread total: " << sumVSpd << " Mem " << memUsed ();

    // traverse accountsByReference to calc dividend
    //accountsOut.reserve(m_accounts.size()+1);
    actualTotalDividend = 0; actualTotalDividendVBC = 0;
    uint64_t totalDivVBCbyRank = dividendCoinsVBC / 2;
    uint64_t totalDivVBCbyPower = dividendCoinsVBC - totalDivVBCbyRank;
    for (const auto& it : m_accounts) {
        uint64_t divVBC = 0;
        auto accountPtr = it.second;
        boost::multiprecision::uint128_t divVBCbyRank(0), divVBCbyPower(0);
        if (dividendCoinsVBC > 0 && sumVSpd > 0 && sumVRank > 0) {
            divVBCbyRank = totalDivVBCbyRank;
            divVBCbyRank *= accountPtr->vRank;
            divVBCbyRank /= sumVRank;
            divVBCbyPower = totalDivVBCbyPower;
            divVBCbyPower *= accountPtr->vSprd;
            divVBCbyPower /= sumVSpd;
            divVBC = static_cast<uint64_t>(divVBCbyRank + divVBCbyPower);
            if (divVBC < VBC_DIVIDEND_MIN) {
                divVBC = 0;
                divVBCbyRank = 0;
                divVBCbyPower = 0;
            }
            actualTotalDividendVBC += divVBC;
        }
        uint64_t div = 0;
        if (dividendCoins > 0 && (dividendCoinsVBC == 0 || divVBC >= VBC_DIVIDEND_MIN)) {
            div = accountPtr->vbc * VRP_INCREASE_RATE / VRP_INCREASE_RATE_PARTS;
            actualTotalDividend += div;
        }
        
        JLOG (m_journal.info) << "{\"account\":\"" << accountPtr->account << "\",\"data\":{\"divVBCByRank\":\"" << divVBCbyRank << "\",\"divVBCByPower\":\"" << divVBCbyPower << "\",\"divVBC\":\"" << divVBC << "\",\"divVRP\":\"" << div << "\",\"balance\":\"" << accountPtr->vbc << "\",\"vrank\":\"" << accountPtr->vRank << "\",\"vsprd\":\"" << accountPtr->vSprd << "\",\"tsprd\":\"" << accountPtr->tSprd << "\"}}";
        
        if (div !=0 || divVBC !=0 || accountPtr->vSprd > MIN_VSPD_TO_GET_FEE_SHARE)
        {
            //accountsOut.push_back(std::make_tuple(accountPtr->account, div, divVBC, static_cast<uint64_t>(divVBCbyRank), static_cast<uint64_t>(divVBCbyPower), accountPtr->vRank, accountPtr->vSprd, accountPtr->tSprd));
            
            std::pair<AccountsDividend::iterator, bool> ret = accountsOut.emplace(std::piecewise_construct, 
                        std::forward_as_tuple(accountPtr->account), 
                        std::forward_as_tuple(div, divVBC, static_cast<uint64_t>(divVBCbyRank), 
                            static_cast<uint64_t>(divVBCbyPower), accountPtr->vRank, 
                            accountPtr->vSprd, accountPtr->tSprd));
            if (ret.second == false)
            {
                JLOG (m_journal.warning) << "Insert same account: " << accountPtr->account << "into dividend account map!";
            }
        }
    }
    
    JLOG (m_journal.info) << "calcDividend got actualTotalDividend " << actualTotalDividend << " actualTotalDividendVBC " << actualTotalDividendVBC << " Mem " << memUsed();
    
    // collect remainning
    uint64_t remainCoins = 0, remainCoinsVBC = 0;
    if (dividendCoins > actualTotalDividend) {
        remainCoins = dividendCoins - actualTotalDividend;
        actualTotalDividend = dividendCoins;
    }
    if (dividendCoinsVBC > actualTotalDividendVBC) {
        remainCoinsVBC = dividendCoinsVBC - actualTotalDividendVBC;
        actualTotalDividendVBC = dividendCoinsVBC;
    }
    if (remainCoins > 0 || remainCoinsVBC > 0)
    {
        //accountsOut.push_back (std::make_tuple (from_hex_text<AccountID> ("0x56CE5173B6A2CBEDF203BD69159212094C651041"), remainCoins, remainCoinsVBC, 0, 0, 0, 0, 0));
        AccountsDividend::iterator spec = accountsOut.find (from_hex_text<AccountID> ("0x56CE5173B6A2CBEDF203BD69159212094C651041"));
        if (spec != accountsOut.end())
        {
            std::get<0> (spec->second) += remainCoins;
            std::get<1> (spec->second) += remainCoinsVBC;
        }
        else
        {
            accountsOut.emplace(std::piecewise_construct, 
                std::forward_as_tuple(from_hex_text<AccountID> ("0x56CE5173B6A2CBEDF203BD69159212094C651041")), 
                std::forward_as_tuple(remainCoins, remainCoinsVBC, 0, 0, 0, 0, 0));
        }
    }

    JLOG (m_journal.info) << "calcDividend done with " << accountsOut.size() << " accounts Mem " << memUsed();
}

std::pair<bool, Json::Value> 
DividendMasterImpl::checkDividend (const uint32_t ledgerIndex, const std::string hash)
{
    Json::Value jvResult;
    SHAMapHash fullHash (from_hex_text<uint256>(hash));
    std::shared_ptr<SHAMap> fullDivMap = std::make_shared<SHAMap> (
        SHAMapType::TRANSACTION,
        fullHash.as_uint256(),
        app_.family ());
    std::vector<SHAMapNodeID> nodeIDs;
    std::vector<uint256> nodeHashes;
    nodeIDs.reserve (1);
    nodeHashes.reserve (1);

    if (fullDivMap->fetchRoot (fullHash, nullptr))
    {
        fullDivMap->getMissingNodes(nodeIDs, nodeHashes, 1, nullptr);
        if (!nodeIDs.empty())
        {
            jvResult[jss::error_message] = "can not fetch node in dividend full map.";
            return std::pair<bool, Json::Value> (false, jvResult);
        }
    }
    else
    {
        jvResult[jss::error_message] = "can not fetch dividend full map root hash";
        return std::pair<bool, Json::Value> (false, jvResult);
    }
    JLOG(m_journal.info) << "check dividend start";
    auto ledger = app_.getLedgerMaster ().getCurrentLedger ();
    uint32 txnDone = 0;
    uint32 txnLeft = 0;
    for (auto item = fullDivMap->begin (); item != fullDivMap->end (); ++item)
    {
        auto sitTrans = SerialIter{item->data (), item->size ()};
        std::shared_ptr<STTx> stpTrans = std::make_shared<STTx> (std::ref (sitTrans));
        auto accountSLE = ledger->read (keylet::account (stpTrans->getAccountID (sfDestination)));
        if (accountSLE->getFieldU32 (sfDividendLedger) == ledgerIndex)
        {
            // dividend is in progress
            JLOG(m_journal.trace) << "dividend in progress txn " << stpTrans->getTransactionID();
            txnDone++;
        }
        else
        {
            txnLeft++;
        }
    }
    jvResult ["done"] = txnDone;
    jvResult ["left"] = txnLeft;

    return std::pair<bool, Json::Value> (true, jvResult);
}

bool DividendMasterImpl::launchDividend (const uint32_t ledgerIndex)
{
    std::string secret_key = get<std::string> (app_.config ()[SECTION_DIVIDEND_ACCOUNT], "secret_key");

    RippleAddress secret = RippleAddress::createSeedGeneric (secret_key);
    RippleAddress generator = RippleAddress::createGeneratorPublic (secret);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (generator, secret, 0);
    RippleAddress accountPublic = RippleAddress::createAccountPublic (generator, 0);

    std::shared_ptr<STTx> trans = std::make_shared<STTx> (ttDIVIDEND);
    trans->setFieldU8 (sfDividendType, DividendMaster::DivType_Start);
    trans->setFieldU32 (sfDividendLedger, ledgerIndex);
    trans->setFieldU32 (sfFlags, tfFullyCanonicalSig);
    trans->setAccountID (sfAccount, AccountID());
    trans->setAccountID (sfDestination, AccountID());
    trans->setFieldU64 (sfDividendCoins, m_dividendTotalCoins);
    trans->setFieldU64 (sfDividendCoinsVBC, m_dividendTotalCoinsVBC);
    trans->setFieldU64 (sfDividendVRank, m_dividendVRank);
    trans->setFieldU64 (sfDividendVSprd, m_dividendVSprd);
    trans->setFieldH256 (sfDividendHash, getResultHash().as_uint256());
    trans->setFieldVL (sfSigningPubKey, accountPublic.getAccountPublic ());

    trans->sign (naAccountPrivate);

    app_.getJobQueue ().addJob (
        jtTRANSACTION, "launchDividend",
        std::bind (&NetworkOPs::submitTransaction, &app_.getOPs (),
                   trans));

    JLOG(m_journal.info) << "Launch dividend,dividend state " << getDividendState ();
    return true;
}

bool DividendMasterImpl::dumpTransactionMap(const uint32_t ledgerIndex, const std::string& hash)
{
    bool doSave = !hash.empty ();

    std::string secret_key = get<std::string> (app_.config ()[SECTION_DIVIDEND_ACCOUNT], "secret_key");
    RippleAddress secret = RippleAddress::createSeedGeneric (secret_key);
    RippleAddress generator = RippleAddress::createGeneratorPublic (secret);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (generator, secret, 0);
    RippleAddress accountPublic = RippleAddress::createAccountPublic (generator, 0);

    std::shared_ptr<SHAMap> divUnsignedMap = std::make_shared<SHAMap> (
        SHAMapType::TRANSACTION,
        app_.family ());

    for (auto const& div : m_divResult)
    {
        // make transaction
        STTx trans (ttDIVIDEND);
        trans.setFieldU8 (sfDividendType, DividendMaster::DivType_Apply);
        trans.setFieldU32 (sfDividendLedger, ledgerIndex);
        trans.setFieldU32 (sfFlags, tfFullyCanonicalSig);
        trans.setAccountID (sfAccount, AccountID ());
        
        trans.setAccountID (sfDestination, div.first);
        trans.setFieldU64 (sfDividendCoins, std::get<0> (div.second));
        trans.setFieldU64 (sfDividendCoinsVBC, std::get<1> (div.second));
        trans.setFieldU64 (sfDividendCoinsVBCRank, std::get<2> (div.second));
        trans.setFieldU64 (sfDividendCoinsVBCSprd, std::get<3> (div.second));
        trans.setFieldU64 (sfDividendVRank, std::get<4> (div.second));
        trans.setFieldU64 (sfDividendVSprd, std::get<5> (div.second));
        trans.setFieldU64 (sfDividendTSprd, std::get<6> (div.second));
        trans.setFieldVL (sfSigningPubKey, accountPublic.getAccountPublic ());

        uint256 txID = trans.getHash(HashPrefix::transactionID);
        Serializer s;
        trans.add (s);
        auto tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!divUnsignedMap->addGiveItem (tItem, true, false))
        {
            if (m_journal.fatal)
            {
                m_journal.fatal << "Add transaction hash " << txID
                                << " to transaction unsigned map hash failed.";
            }
            return false;
        }
        else
        {
            if (m_journal.trace)
            {
                m_journal.trace << "Add transaction hash " << txID
                                << " to transaction unsigned map hash.";
                m_journal.trace << trans.STObject::getJson (0);
            }
        }
    }
    
    setResultHash (divUnsignedMap->getHash ());

    JLOG(m_journal.info) << "Transaction full unsigned map hash is " << getResultHash ();

    // check if hash matches before signing phase.
    if (doSave)
    {
        JLOG(m_journal.info) << "Transaction full map hash is " << divUnsignedMap->getHash ();
        
        if (to_string (getResultHash ()) != hash)
            return false;
        
        // flush full hashmap to nodestore
        divUnsignedMap->flushDirty (hotTRANSACTION_NODE, 0);
        setResultHash (divUnsignedMap->getHash ());
    }
    return true;
}

std::unique_ptr<DividendMaster>
make_DividendMaster(Application& app, beast::Journal journal)
{
    return std::make_unique<DividendMasterImpl>(app, journal);
}

}
