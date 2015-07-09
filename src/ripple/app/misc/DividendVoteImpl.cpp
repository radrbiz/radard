#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/DividendVote.h>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/SystemParameters.h>

namespace ripple {

class DividendVoteImpl : public DividendVote
{
public:
    DividendVoteImpl(beast::Journal journal)
        : m_journal(journal)
    {
    }
    
    bool isStartLedger(Ledger::ref ledger) override
    {
        return ((ledger->getLedgerSeq() > 2) &&
                (ledger->getTotalCoins() < VRP_INCREASE_MAX || ledger->getTotalCoinsVBC() < VBC_INCREASE_MAX) &&
                (ledger->getCloseTime().time_of_day().hours() == 23) &&
                ((ledger->getCloseTimeNC() - ledger->getDividendTimeNC()) > 3600)
                );
    }
    
    bool isApplyLedger(Ledger::ref ledger) override
    {
        return (ledger->isDividendStarted() &&
                ((ledger->getCloseTimeNC() - ledger->getDividendTimeNC()) >= 120)
                );
    }

    void doStartValidation(Ledger::ref lastClosedLedger, STObject& baseValidation) override
    {
        // LCL must be validation ledger
        assert (isStartLedger(lastClosedLedger));
        
        std::uint32_t dividendLedger = lastClosedLedger->getLedgerSeq();
        std::uint64_t dividendCoins = VRP_INCREASE_RATE * lastClosedLedger->getTotalCoinsVBC() / VRP_INCREASE_RATE_PARTS;
        if (dividendCoins + lastClosedLedger->getTotalCoins() > VRP_INCREASE_MAX) {
            dividendCoins = 0;
        }
        std::uint64_t dividendCoinsVBC = 0;
        if (lastClosedLedger->getTotalCoinsVBC() < VBC_INCREASE_MAX) {
            if (lastClosedLedger->getCloseTimeNC() < VBC_DIVIDEND_PERIOD_1) {
                dividendCoinsVBC = VBC_INCREASE_RATE_1 * lastClosedLedger->getTotalCoinsVBC() / VBC_INCREASE_RATE_1_PARTS;
            } else if (lastClosedLedger->getCloseTimeNC() < VBC_DIVIDEND_PERIOD_2) {
                dividendCoinsVBC = VBC_INCREASE_RATE_2 * lastClosedLedger->getTotalCoinsVBC() / VBC_INCREASE_RATE_2_PARTS;
            } else if (lastClosedLedger->getCloseTimeNC() < VBC_DIVIDEND_PERIOD_3) {
                dividendCoinsVBC = VBC_INCREASE_RATE_3 * lastClosedLedger->getTotalCoinsVBC() / VBC_INCREASE_RATE_3_PARTS;
            } else {
                dividendCoinsVBC = VBC_INCREASE_RATE_4 * lastClosedLedger->getTotalCoinsVBC() / VBC_INCREASE_RATE_4_PARTS;
            }
            if (dividendCoinsVBC + lastClosedLedger->getTotalCoinsVBC() > VBC_INCREASE_MAX)
                dividendCoinsVBC = 0;
        }
        
        if (dividendCoins==0 && dividendCoinsVBC==0) {
            if (m_journal.warning)
                m_journal.warning << "Not voting for a dividend start because both VRP and VBC will exceed max.";
            return;
        }
        
        baseValidation.setFieldU32 (sfDividendLedger,   dividendLedger);
        baseValidation.setFieldU64 (sfDividendCoins,    dividendCoins);
        baseValidation.setFieldU64 (sfDividendCoinsVBC, dividendCoinsVBC);
        
        if (m_journal.info)
            m_journal.info << "Voting for a dividend start based " << dividendLedger << " With VRP "<< dividendCoins << " VBC " << dividendCoinsVBC << " in " << lastClosedLedger->getHash();
    }

    void doStartVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override
    {
        // LCL must be validation ledger
        assert (isStartLedger(lastClosedLedger));
        
        std::uint32_t dividendLedger = lastClosedLedger->getLedgerSeq();
        std::map<std::pair<uint64_t, uint64_t>, int> voteMap;
        // get validations for validation ledger
        ValidationSet const set = getApp().getValidations ().getValidations (lastClosedLedger->getHash ());
        for (auto const& e : set)
        {
            STValidation const& val = *e.second;
            
            if (val.isTrusted ())
            {
                if (val.isFieldPresent (sfDividendLedger)
                    && val.isFieldPresent (sfDividendCoins)
                    && val.isFieldPresent (sfDividendCoinsVBC))
                {
                    uint32_t ledgerSeq = val.getFieldU32 (sfDividendLedger);
                    if (ledgerSeq != dividendLedger) {
                        m_journal.warning << "Mismatch ledger seq " << ledgerSeq << " from validator " << val.getNodeID() << " ours: " << dividendLedger << " in " << lastClosedLedger->getHash();
                        continue;
                    }
                    ++voteMap[std::make_pair(val.getFieldU64 (sfDividendCoins), val.getFieldU64 (sfDividendCoinsVBC))];
                }
            }
        }
        
        std::pair<uint64_t, uint64_t> ourVote = std::make_pair(0, 0);
        int weight = 0;
        
        for (auto const& e : voteMap)
        {
            // Take most voted value
            if (e.second > weight)
            {
                ourVote = e.first;
                weight = e.second;
            }
        }
        if (weight < getApp().getLedgerMaster().getMinValidations()) {
            m_journal.warning << weight << " votes are not enough to start dividend for " << dividendLedger;
            return;
        }
        
        if (ourVote.first==0 && ourVote.second==0) {
            if (m_journal.warning)
                m_journal.warning << "Not voting for a dividend start because both VRP and VBC voted are 0";
            return;
        }
        
        if (ourVote.first + lastClosedLedger->getTotalCoins() > VRP_INCREASE_MAX &&
            ourVote.second + lastClosedLedger->getTotalCoinsVBC() > VBC_INCREASE_MAX) {
            if (m_journal.error)
                m_journal.error << "Not voting for a dividend start because both VRP and VBC will exceed max.";
            return;
        }
        
        if (m_journal.warning)
            m_journal.warning << "We are voting for a dividend start based " << dividendLedger << " with VRP " << ourVote.first << " VBC " << ourVote.second << " with " << weight << " same votes in " << lastClosedLedger->getHash();

        STTx trans(ttDIVIDEND);
        trans.setFieldU8(sfDividendType, DividendMaster::DivType_Start);
        trans.setFieldAccount(sfAccount, Account());
        trans.setFieldU32(sfDividendLedger, dividendLedger);
        trans.setFieldU64(sfDividendCoins, ourVote.first);
		trans.setFieldU64(sfDividendCoinsVBC, ourVote.second);

        uint256 txID = trans.getTransactionID();

        if (m_journal.warning)
            m_journal.warning << "Vote: " << txID;

        Serializer s;
        trans.add (s, true);

        SHAMapItem::pointer tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (m_journal.warning)
                m_journal.warning << "Ledger already had dividend start";
        }
    }
    
    void doApplyValidation(Ledger::ref lastClosedLedger, STObject& baseValidation) override
    {
        DividendMaster::pointer dividendMaster = getApp().getOPs().getDividendMaster();
        if (dividendMaster->tryLock())
        {
            if (dividendMaster->isReady())
            {
                uint32_t dividendLedger = lastClosedLedger->getDividendBaseLedger();
                if (dividendLedger == dividendMaster->getLedgerSeq())
                {
                    baseValidation.setFieldU32 (sfDividendLedger, dividendLedger);
                    baseValidation.setFieldH256 (sfDividendResultHash, dividendMaster->getResultHash());
                    
                    if (m_journal.info)
                        m_journal.info << "Voting for a dividend apply based " << dividendLedger << " with hash "<< dividendMaster->getResultHash() << " in " << lastClosedLedger->getHash();
                }
                else
                {
                    if (m_journal.warning)
                        m_journal.warning << "Wrong base ledger " << dividendMaster->getLedgerSeq() << " want "<< dividendLedger;
                }
            }
            dividendMaster->unlock();
        }
    }
    
    bool doApplyVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override
    {
        uint32_t dividendLedger = lastClosedLedger->getDividendBaseLedger();
        
        int weight = 0;
        std::map<uint256, int> votes;
        
        // get validations for validation ledger
        ValidationSet const set = getApp().getValidations ().getValidations (lastClosedLedger->getHash ());
        for (auto const& e : set)
        {
            STValidation const& val = *e.second;
            
            if (val.isTrusted ())
            {
                if (val.isFieldPresent (sfDividendLedger) &&
                    val.isFieldPresent (sfDividendResultHash))
                {
                    uint32_t ledgerSeq = val.getFieldU32 (sfDividendLedger);
                    if (ledgerSeq != dividendLedger)
                        continue;
                    const uint256 & dividendHash = val.getFieldH256 (sfDividendResultHash);
                    ++votes[dividendHash];
//                    if (ledgerSeq != dividendLedger || dividendHash != dividendResultHash) {
                    if (m_journal.debug)
                        m_journal.debug << "Recv dividend apply vote based " << ledgerSeq << " hash " << dividendHash << " from validator " << val.getNodeID() << " in " << lastClosedLedger->getHash();
//                        continue;
//                    }
//                    ++weight;
                }
            }
        }
        
        uint256 dividendResultHash;
        for (auto const& v : votes)
        {
            if (v.second > weight)
            {
                dividendResultHash = v.first;
                weight = v.second;
            }
        }
        
        DividendMaster::pointer dividendMaster = getApp().getOPs().getDividendMaster();
        if (!dividendMaster->tryLock())
        {
            if (weight >=getApp().getLedgerMaster().getMinValidations())
                return false;
            else
                return true;
        }
        
        if (!dividendMaster->isReady() ||
            dividendLedger != dividendMaster->getLedgerSeq() ||
            dividendResultHash != dividendMaster->getResultHash())
        {
            if (dividendMaster->isReady())
                m_journal.warning << "We got mismatch dividend apply based " << dividendLedger << " hash " << dividendResultHash << " ours " << dividendMaster->getResultHash() << " based " << dividendMaster->getLedgerSeq() << " in " << lastClosedLedger->getHash();
            dividendMaster->unlock();
            if (weight >=getApp().getLedgerMaster().getMinValidations())
                return false;
            else
                return true;
        }
        
        if (weight >= getApp().getLedgerMaster().getMinValidations())
        {
            m_journal.warning << "We are voting for a dividend apply based " << dividendLedger << " hash " << dividendResultHash << " with " << weight << " same votes in " << lastClosedLedger->getHash();
            dividendMaster->fillDivResult(initialPosition);
            dividendMaster->fillDivReady(initialPosition);
            dividendMaster->setReady(false);
        }
        else
        {
            m_journal.warning << "We are cancelling a dividend apply with only " << weight << " same votes in " << lastClosedLedger->getHash();
            dividendMaster->setTotalDividend(0);
            dividendMaster->setTotalDividendVBC(0);
            dividendMaster->setSumVRank(0);
            dividendMaster->setSumVSpd(0);
            dividendMaster->setResultHash(uint256());
            dividendMaster->fillDivReady(initialPosition);
            dividendMaster->setReady(false);
        }
        
        dividendMaster->unlock();
        
        return true;
    }

private:
    beast::Journal m_journal;
};
    

std::unique_ptr<DividendVote>
make_DividendVote(beast::Journal journal)
{
    return std::make_unique<DividendVoteImpl>(journal);
}

}
