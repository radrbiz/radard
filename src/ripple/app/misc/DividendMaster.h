#ifndef RIPPLE_APP_DIVIDEND_MASTER_H_INCLUDED
#define RIPPLE_APP_DIVIDEND_MASTER_H_INCLUDED

namespace ripple {

class DividendMaster
{
public:
    typedef const std::shared_ptr<DividendMaster>& ref;
    typedef std::shared_ptr<DividendMaster> pointer;
    typedef enum { DivType_Done = 0, DivType_Start = 1, DivType_Apply = 2 } DivdendType;
    typedef enum { DivState_Done = 0, DivState_Start = 1 } DivdendState;

    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool tryLock() = 0;
    virtual void setReady(bool ready) = 0;
    virtual bool isReady() = 0;
    virtual void setRunning(bool running) = 0;
    virtual bool isRunning() = 0;
    
    // <AccountID, DivCoins, DivCoinsVBC, DivCoinsVBCRank, DivCoinsVBCSpd, VRank, VSpd>
    typedef std::vector<std::tuple<Account, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, uint64_t, uint64_t>> AccountsDividend;
    
    virtual AccountsDividend& getDivResult() = 0;
    virtual void setTotalDividendVBC(uint64_t) = 0;
    virtual uint64_t getTotalDividendVBC() = 0;
    virtual void setTotalDividend(uint64_t) = 0;
    virtual uint64_t getTotalDividend() = 0;
    virtual void setSumVRank(uint64_t) = 0;
    virtual void setSumVSpd(uint64_t) = 0;
    virtual bool calcResultHash() = 0;
    virtual uint256 getResultHash() = 0;
    virtual void setResultHash(uint256) = 0;
    virtual void fillDivReady(SHAMap::pointer preSet) = 0;
    virtual void fillDivResult(SHAMap::pointer preSet) = 0;
    virtual void setLedgerSeq(uint32_t seq) = 0;
    virtual uint32_t getLedgerSeq() = 0;
    
    static void calcDividend(Ledger::ref lastClosedLedger);
    
    
    /// @return true: needs dividend.
    static bool calcDividendFunc(Ledger::ref baseLedger, uint64_t dividendCoins, uint64_t dividendCoinsVBC, AccountsDividend& accountsOut, uint64_t& actualTotalDividend, uint64_t& actualTotalDividendVBC, uint64_t& sumVRank, uint64_t& sumVSpd);
};

std::unique_ptr<DividendMaster>
make_DividendMaster(beast::Journal journal);

}

#endif //RIPPLE_APP_DIVIDEND_MASTER_H_INCLUDED
