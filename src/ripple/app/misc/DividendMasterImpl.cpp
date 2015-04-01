#if (defined (_WIN32) || defined (_WIN64))
#include <Psapi.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <boost/multiprecision/cpp_int.hpp>


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
    DividendMasterImpl(beast::Journal journal)
        : m_journal(journal),
        m_ready(false),
        m_dividendLedgerSeq(0),
        m_running(false)
    {
    }

    void lock() override
    {
        m_lock.lock();
    }
    void unlock() override
    {
        m_lock.unlock();
    }

    bool tryLock() override
    {
        return m_lock.try_lock();
    }

    void setReady(bool ready) override
    {
        m_ready = ready;
    }

    bool isReady() override
    {
        if (m_ready)
        {
            Ledger::pointer lastClosedLedger = getApp().getOPs().getLedgerByHash(getApp().getOPs().getConsensusLCL());
            if (lastClosedLedger)
            {
                std::uint32_t baseDivLedgerSeq = lastClosedLedger->getDividendBaseLedger();
                if (baseDivLedgerSeq > 0 && baseDivLedgerSeq == m_dividendLedgerSeq)
                {
                    return true;
                }
            }
            m_ready = false;
        }
        return false;
    }
    
    void setRunning(bool running)
    {
        m_running = running;
    }
    
    bool isRunning()
    {
        return m_running;
    }

    AccountsDividend& getDivResult() override
    {
        return m_divResult;
    }

    void setTotalDividendVBC(uint64_t num) override
    {
        m_totalDividendVBC = num;
    }

    uint64_t getTotalDividendVBC() override
    {
        return m_totalDividendVBC;
    }

    void setTotalDividend(uint64_t num) override
    {
        m_totalDividend = num;
    }

    uint64_t getTotalDividend() override
    {
        return m_totalDividend;
    }
    
    void setSumVRank(uint64_t num) override
    {
        m_sumVRank = num;
    }
    
    void setSumVSpd(uint64_t num) override
    {
        m_sumVSpd = num;
    }

    bool calcResultHash() override
    {
        Application& app = getApp();
#ifdef RADAR_ASYNC_DIVIDEND
        SHAMap::pointer txMap = std::make_shared<SHAMap>(
                                        smtTRANSACTION,
                                        app.getFullBelowCache(),
                                        app.getTreeNodeCache());
        
        for (const auto& it : m_divResult)
        {
            SerializedTransaction trans(ttDIVIDEND);
            trans.setFieldU8(sfDividendType, DividendMaster::DivType_Apply);
            trans.setFieldAccount(sfAccount, Account());
            trans.setFieldAccount(sfDestination, std::get<0>(it));
            trans.setFieldU32(sfDividendLedger, m_dividendLedgerSeq);
            trans.setFieldU64(sfDividendCoins, std::get<1>(it));
            trans.setFieldU64(sfDividendCoinsVBC, std::get<2>(it));
            trans.setFieldU64(sfDividendCoinsVBCRank, std::get<3>(it));
            trans.setFieldU64(sfDividendCoinsVBCSprd, std::get<4>(it));
            trans.setFieldU64(sfDividendVRank, std::get<5>(it));
            trans.setFieldU64(sfDividendVSprd, std::get<6>(it));
            trans.setFieldU64(sfDividendTSprd, std::get<7>(it));

            uint256 txID = trans.getTransactionID();
            Serializer s;
            trans.add(s, true);
            
            SHAMapItem::pointer tItem = std::make_shared<SHAMapItem>(txID, s.peekData());
            
            if (!txMap->addGiveItem(tItem, true, false))
            {
                return false;
            }
        }
        m_resultHash = txMap->getHash();
#endif // RADAR_ASYNC_DIVIDEND
        
        return true;
    }
    uint256 getResultHash() override
    {
        return m_resultHash;
    }
    
    void setResultHash(uint256 hash) override
    {
        m_resultHash = hash;
    }

    void fillDivReady(SHAMap::pointer initialPosition) override
    {
        SerializedTransaction trans(ttDIVIDEND);
        trans.setFieldU8(sfDividendType, DividendMaster::DivType_Done);
        trans.setFieldAccount(sfAccount, Account());
        trans.setFieldU32(sfDividendLedger, m_dividendLedgerSeq);
        trans.setFieldU64(sfDividendCoins, m_totalDividend);
        trans.setFieldU64(sfDividendCoinsVBC, m_totalDividendVBC);
        trans.setFieldU64(sfDividendVRank, m_sumVRank);
        trans.setFieldU64(sfDividendVSprd, m_sumVSpd);
        trans.setFieldH256(sfDividendResultHash, m_resultHash);

        uint256 txID = trans.getTransactionID();
        Serializer s;
        trans.add(s, true);

        SHAMapItem::pointer tItem = std::make_shared<SHAMapItem>(txID, s.peekData());

        if (!initialPosition->addGiveItem(tItem, true, false)) {
            if (m_journal.warning)
            {
                m_journal.warning << "Ledger already had dividend ready";
            }
        }
        else
        {
            if (m_journal.debug)
            {
                m_journal.debug << "dividend ready add tx " << txID;
            }
        }
    }

    void fillDivResult(SHAMap::pointer initialPosition) override
    {
        for (const auto& it : m_divResult) {
            SerializedTransaction trans(ttDIVIDEND);
            trans.setFieldU8(sfDividendType, DividendMaster::DivType_Apply);
            trans.setFieldAccount(sfAccount, Account());
            trans.setFieldAccount(sfDestination, std::get<0>(it));
            trans.setFieldU32(sfDividendLedger, m_dividendLedgerSeq);
            trans.setFieldU64(sfDividendCoins, std::get<1>(it));
            trans.setFieldU64(sfDividendCoinsVBC, std::get<2>(it));
            trans.setFieldU64(sfDividendCoinsVBCRank, std::get<3>(it));
            trans.setFieldU64(sfDividendCoinsVBCSprd, std::get<4>(it));
            trans.setFieldU64(sfDividendVRank, std::get<5>(it));
            trans.setFieldU64(sfDividendVSprd, std::get<6>(it));
            trans.setFieldU64(sfDividendTSprd, std::get<7>(it));

            uint256 txID = trans.getTransactionID();
            Serializer s;
            trans.add(s, true);

            SHAMapItem::pointer tItem = std::make_shared<SHAMapItem>(txID, s.peekData());

            if (!initialPosition->addGiveItem(tItem, true, false)) {
                if (m_journal.warning.active())
                    m_journal.warning << "Ledger already had dividend for " << std::get<0>(it);
            }
            else {
                if (m_journal.trace.active())
                    m_journal.trace << "dividend add TX " << txID << " for " << std::get<0>(it);
            }
        }
        if (m_journal.info)
            m_journal.info << "dividend add " << m_divResult.size() << " TXs done. Mem" << memUsed();
    }

    void setLedgerSeq(uint32_t seq) override
    {
        m_dividendLedgerSeq = seq;
    }

    uint32_t getLedgerSeq() override
    {
        return m_dividendLedgerSeq;
    }


private:
    beast::Journal m_journal;
    beast::RecursiveMutex m_lock;
    bool m_ready;
    uint32_t m_dividendLedgerSeq;
    AccountsDividend m_divResult;
    uint64_t m_totalDividend;
    uint64_t m_totalDividendVBC;
    uint64_t m_sumVRank=0;
    uint64_t m_sumVSpd=0;
    uint256 m_resultHash;
    bool m_running;
};

void DividendMaster::calcDividend(Ledger::ref lastClosedLedger)
{
    auto sle = lastClosedLedger->getDividendObject();
    
    if (!sle
        || sle->getFieldIndex(sfDividendLedger) == -1
        || sle->getFieldIndex(sfDividendCoins) == -1
        || sle->getFieldIndex(sfDividendCoinsVBC) == -1 ){
        WriteLog(lsERROR, DividendMaster) << "calcDividend called but info in dividend object missing";
        return;
    }
    
    uint32_t baseLedgerSeq = sle->getFieldU32(sfDividendLedger);
    uint64_t dividendCoins = sle->getFieldU64(sfDividendCoins);
    uint64_t dividendCoinsVBC = sle->getFieldU64(sfDividendCoinsVBC);
    
    Ledger::ref baseLedger = getApp().getOPs().getLedgerBySeq(baseLedgerSeq);
    
    if (!baseLedger) {
        WriteLog(lsWARNING, DividendMaster) << "base ledger not found";
        return;
    }
    
    DividendMaster::pointer dividendMaster = getApp().getOPs().getDividendMaster();
    
    dividendMaster->lock();
    
    dividendMaster->setRunning(true);
    dividendMaster->setReady(false);
    
    dividendMaster->setLedgerSeq(baseLedgerSeq);
    dividendMaster->getDivResult().clear();
    uint64_t actualTotalDividend = 0;
    uint64_t actualTotalDividendVBC = 0, sumVRank=0, sumVSpd=0;
    
    if (!DividendMaster::calcDividendFunc(baseLedger,
                                      dividendCoins,
                                      dividendCoinsVBC,
                                      dividendMaster->getDivResult(),
                                      actualTotalDividend,
                                      actualTotalDividendVBC, sumVRank, sumVSpd))
    {
        WriteLog(lsWARNING, DividendMaster) << "calcDividend does not find any account";
        dividendMaster->setRunning(false);
        dividendMaster->unlock();
        return;
    }
    
    dividendMaster->setTotalDividend(actualTotalDividend);
    dividendMaster->setTotalDividendVBC(actualTotalDividendVBC);
    dividendMaster->setSumVRank(sumVRank);
    dividendMaster->setSumVSpd(sumVSpd);
    
    if (!dividendMaster->calcResultHash())
    {
        WriteLog(lsWARNING, DividendMaster) << "calcDividend fail to get result hash";
        dividendMaster->setRunning(false);
        dividendMaster->unlock();
        return;
    }
    
    dividendMaster->setReady(true);
    dividendMaster->setRunning(false);
    
    dividendMaster->unlock();
}

static inline uint64_t adjust(uint64_t coin)
{
    return coin>=10000000000 ? coin+90000000000 : coin*10;
}

class AccountsByReference_Less {
public:
    template <class T>
    bool operator()(const T &x, const T &y) const
    {
        // Accounts by reference height needs to be descending order
        if (std::get<2>(x) > std::get<2>(y))
            return true;
        else if (std::get<2>(x) == std::get<2>(y))
            return std::get<1>(x) > std::get<1>(y);
        return false;
    }
};

bool DividendMaster::calcDividendFunc(Ledger::ref baseLedger, uint64_t dividendCoins, uint64_t dividendCoinsVBC, AccountsDividend& accountsOut, uint64_t& actualTotalDividend, uint64_t& actualTotalDividendVBC, uint64_t& sumVRank, uint64_t& sumVSpd)
{
    WriteLog(lsINFO, DividendMaster) << "Expected dividend: " << dividendCoins << " " << dividendCoinsVBC << " for ledger " << baseLedger->getLedgerSeq() << " Mem " << memUsed();
    
    // <BalanceVBC, <AccountId, ParentAccoutId, Height>>, accounts sorted by balance
    std::multimap<uint64_t, std::tuple<Account, Account, uint32_t>> accountsByBalance;
    
    // <<AccountId, ParentId, Height>, <BalanceVBC, VRank, VSpd, TSpd>>, accounts sorted by reference height and parent desc
    std::multimap<std::tuple<Account, Account, uint32_t>, std::tuple<uint64_t, uint32_t, uint64_t, uint64_t>, AccountsByReference_Less> accountsByReference;
    
    hash_set<Account> accountsNeedsUpgrade;
    
    // visit account stats to fill accountsByBalance
    baseLedger->visitStateItems([&accountsByBalance, &accountsByReference, &accountsNeedsUpgrade, baseLedger](SLE::ref sle) {
        if (sle->getType() == ltACCOUNT_ROOT) {
            uint64_t bal = sle->getFieldAmount(sfBalanceVBC).getNValue();
            // Only accounts with balance >= SYSTEM_CURRENCY_PARTS_VBC or has child should be calculated.
            if (sle->isFieldPresent(sfReferences)) {
                // refer migrate needed, @todo: simply delete this if after migration.
                accountsNeedsUpgrade.insert(sle->getFieldAccount(sfAccount).getAccountID());
            }
            else if (bal < SYSTEM_CURRENCY_PARTS_VBC
                && !baseLedger->hasRefer(sle->getFieldAccount(sfAccount).getAccountID())) {
                return;
            }
            uint32_t height = 0;
            Account addrParent;
            if (sle->isFieldPresent(sfReferee) && sle->isFieldPresent(sfReferenceHeight)) {
                height = sle->getFieldU32(sfReferenceHeight);
                addrParent = sle->getFieldAccount(sfReferee).getAccountID();
            }
            if (bal < SYSTEM_CURRENCY_PARTS_VBC)
                accountsByReference.emplace(std::piecewise_construct,
                                            std::forward_as_tuple(sle->getFieldAccount(sfAccount).getAccountID(), addrParent, height),
                                            std::forward_as_tuple(bal, 0, 0, 0));
            else
                accountsByBalance.emplace(std::piecewise_construct,
                                          std::forward_as_tuple(bal),
                                          std::forward_as_tuple(sle->getFieldAccount(sfAccount).getAccountID(), addrParent, height));
        }
    });
    WriteLog(lsINFO, DividendMaster) << "calcDividend got " << accountsByBalance.size() << " accounts for ranking " << accountsByReference.size() << " accounts for sprd Mem " << memUsed();
    
    if (accountsByBalance.empty() && accountsByReference.empty())
    {
        accountsOut.clear();
        actualTotalDividend = 0;
        actualTotalDividendVBC = 0;
        sumVRank = 0;
        sumVSpd = 0;
        return true;
    }
    
    // traverse accountsByBalance to caculate V ranking into VRank in accountsByReference
    sumVRank = 0;
    {
        uint64_t lastBalance = 0;
        uint32_t pos = 1, rank = 1;
        for (auto it = accountsByBalance.begin(); it != accountsByBalance.end(); ++pos) {
            if (lastBalance < it->first) {
                rank = pos;
                lastBalance = it->first;
            }
            accountsByReference.emplace(std::piecewise_construct, it->second, std::forward_as_tuple(it->first, rank, 0, 0));
            sumVRank += rank;
            it = accountsByBalance.erase(it);
        }
    }
    WriteLog(lsINFO, DividendMaster) << "calcDividend got v rank total: " << sumVRank << " Mem " << memUsed();
    
    // traverse accountsByReference to caculate V spreading into VSpd
    sumVSpd = 0;
    {
        // <AccountId, <TotalChildrenHolding, TotalChildrenVSpd>>, children holding cache
        hash_map<Account, std::pair<uint64_t, uint64_t>> childrenHoldings;
        Account lastParent;
        uint64_t totalChildrenVSpd = 0, totalChildrenHolding = 0, maxHolding = 0;
        for (auto& it : accountsByReference) {
            const Account& accountParent = std::get<1>(it.first);
            if (lastParent != accountParent) {
                // no more for lastParent, store it
                if (totalChildrenVSpd != 0) {
                    childrenHoldings.emplace(lastParent, std::make_pair(totalChildrenHolding, totalChildrenVSpd - adjust(maxHolding) + (static_cast<uint64_t>(pow(maxHolding/SYSTEM_CURRENCY_PARTS_VBC, 1.0 / 3))*SYSTEM_CURRENCY_PARTS_VBC)));
                }
                totalChildrenVSpd = totalChildrenHolding = maxHolding = 0;
                lastParent = accountParent;
            }
            
            const Account& account = std::get<0>(it.first);
            
            uint64_t t = 0, v = 0;
            
            // pickup children holding.
            auto itHolding = childrenHoldings.find(account);
            if (itHolding != childrenHoldings.end()) {
                t = itHolding->second.first;
                v = itHolding->second.second;
                childrenHoldings.erase(itHolding);
            }
            
            uint64_t balance = std::get<0>(it.second);
            
            // store V spreading
            if (balance >= SYSTEM_CURRENCY_PARTS_VBC) {
                std::get<2>(it.second) = v;
                sumVSpd += v;
            }
            
            t += balance;
            std::get<3>(it.second) = t;
            
            if (accountParent.isZero())
                continue;
            
            totalChildrenHolding += t;
            totalChildrenVSpd += adjust(t);
            
            if (maxHolding < t)
                maxHolding = t;
        }
    }
    WriteLog(lsINFO, DividendMaster) << "calcDividend got v spread total: " << sumVSpd << " Mem " << memUsed();
    
    // traverse accountsByReference to calc dividend
    accountsOut.reserve(accountsByReference.size()+1);
    actualTotalDividend = 0; actualTotalDividendVBC = 0;
    uint64_t totalDivVBCbyRank = dividendCoinsVBC / 2;
    uint64_t totalDivVBCbyPower = dividendCoinsVBC - totalDivVBCbyRank;
    for (const auto& it : accountsByReference) {
        uint64_t divVBC = 0;
        boost::multiprecision::uint128_t divVBCbyRank(0), divVBCbyPower(0);
        if (dividendCoinsVBC > 0 && sumVSpd > 0 && sumVRank > 0) {
            divVBCbyRank = totalDivVBCbyRank;
            divVBCbyRank *= std::get<1>(it.second);
            divVBCbyRank /= sumVRank;
            divVBCbyPower = totalDivVBCbyPower;
            divVBCbyPower *= std::get<2>(it.second);
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
            div = std::get<0>(it.second) * VRP_INCREASE_RATE / VRP_INCREASE_RATE_PARTS;
            actualTotalDividend += div;
        }
        
        if (ShouldLog(lsINFO, DividendMaster)) {
            WriteLog(lsINFO, DividendMaster) << "{\"account\":\"" << RippleAddress::createAccountID(std::get<0>(it.first)).humanAccountID() << "\",\"data\":{\"divVBCByRank\":\"" << divVBCbyRank << "\",\"divVBCByPower\":\"" << divVBCbyPower << "\",\"divVBC\":\"" << divVBC << "\",\"balance\":\"" << std::get<0>(it.second) << "\",\"vrank\":\"" << std::get<1>(it.second) << "\",\"vsprd\":\"" << std::get<2>(it.second) << "\",\"tsprd\":\"" << std::get<3>(it.second) << "\"}}";
        }
        
        if (div !=0 || divVBC !=0) {
            accountsOut.push_back(std::make_tuple(std::get<0>(it.first), div, divVBC, static_cast<uint64_t>(divVBCbyRank), static_cast<uint64_t>(divVBCbyPower), std::get<1>(it.second), std::get<2>(it.second), std::get<3>(it.second)));
            accountsNeedsUpgrade.erase(std::get<0>(it.first)); // refer migrate needed, @todo: simply delete this line after migration.
        }
    }
    accountsByReference.clear();
    
    for (const auto& it : accountsNeedsUpgrade) {
        // refer migrate needed, @todo: simply delete this for loop after migration.
        accountsOut.push_back(std::make_tuple(it, 0, 0, 0, 0, 0, 0, 0));
    }
    
    WriteLog(lsINFO, DividendMaster) << "calcDividend got actualTotalDividend " << actualTotalDividend << " actualTotalDividendVBC " << actualTotalDividendVBC << " Mem " << memUsed();
    
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
        accountsOut.push_back(std::make_tuple(Account("0x56CE5173B6A2CBEDF203BD69159212094C651041"), remainCoins, remainCoinsVBC, 0, 0, 0, 0, 0));
    
    WriteLog(lsINFO, DividendMaster) << "calcDividend done with " << accountsOut.size() << " accounts Mem " << memUsed();
    
    return true;
}

std::unique_ptr<DividendMaster>
make_DividendMaster(beast::Journal journal)
{
    return std::make_unique<DividendMasterImpl>(journal);
}

}
