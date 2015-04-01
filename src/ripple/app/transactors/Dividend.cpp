#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

    class Dividend
        : public Transactor
    {
    public:
        Dividend(
            STTx const& txn,
            TransactionEngineParams params,
            TransactionEngine* engine)
            : Transactor(
            txn,
            params,
            engine,
            deprecatedLogs().journal("Dividend"))
        {
        }

        TER checkSig() override
        {
            //No signature, always return true
            return tesSUCCESS;
        }

        TER checkSeq() override
        {
            if ((mTxn.getSequence() != 0) || mTxn.isFieldPresent(sfPreviousTxnID))
            {
                m_journal.warning << "Bad sequence";
                return temBAD_SEQUENCE;
            }

            return tesSUCCESS;
        }

        TER payFee() override
        {
            if (mTxn.getTransactionFee() != STAmount())
            {
                m_journal.warning << "Non-zero fee";
                return temBAD_FEE;
            }

            return tesSUCCESS;
        }

        TER preCheck() override
        {
            if (mParams & tapOPEN_LEDGER)
            {
                m_journal.warning << "Dividend transaction against open ledger";
                return temINVALID;
            }
            if (!mTxn.isFieldPresent(sfDividendType))
            {
                m_journal.warning << "No dividend type";
                return temBAD_DIV_TYPE;
            }
            if (!mTxn.isFieldPresent(sfDividendLedger))
            {
                m_journal.warning << "No dividend ledger";
                return temINVALID;
            }
            if (!mTxn.isFieldPresent(sfDividendCoins))
            {
                m_journal.warning << "No dividend coins";
                return temINVALID;
            }
            if (!mTxn.isFieldPresent(sfDividendCoinsVBC))
            {
                m_journal.warning << "No dividend coins vbc";
                return temINVALID;
            }
            
            uint8_t divType = mTxn.getFieldU8(sfDividendType);
            
            if (divType == DividendMaster::DivType_Start) {
                SLE::pointer sle=mEngine->getLedger()->getDividendObject();
                if (sle && sle->getFieldIndex(sfDividendState)!=-1
                    && sle->getFieldU8(sfDividendState)!=DividendMaster::DivState_Done) {
                    m_journal.warning << "Dividend in progress";
                    return tefBAD_LEDGER;
                }
            } else {
                SLE::pointer sle=mEngine->getLedger()->getDividendObject();
                if (!sle || sle->getFieldIndex(sfDividendLedger)==-1) {
                    m_journal.warning << "No dividend ledger";
                    return tefBAD_LEDGER;
                }
                if (mTxn.getFieldU32(sfDividendLedger) != sle->getFieldU32(sfDividendLedger)) {
                    m_journal.warning << "Dividend ledger mismatch";
                    return tefBAD_LEDGER;
                }
                if (!mTxn.isFieldPresent(sfDividendVRank))
                {
                    m_journal.warning << "No dividend v rank";
                    return temINVALID;
                }
                if (!mTxn.isFieldPresent(sfDividendVSprd))
                {
                    m_journal.warning << "No dividend v spread";
                    return temINVALID;
                }
                switch (divType) {
                    case DividendMaster::DivType_Apply:
                    {
                        if (!mTxn.isFieldPresent(sfDestination))
                        {
                            m_journal.warning << "No dividend destination";
                            return temDST_NEEDED;
                        }
                        if (!mTxn.isFieldPresent(sfDividendCoinsVBCRank))
                        {
                            m_journal.warning << "No dividend coins by rank";
                            return temINVALID;
                        }
                        if (!mTxn.isFieldPresent(sfDividendCoinsVBCSprd))
                        {
                            m_journal.warning << "No dividend coins by spread";
                            return temINVALID;
                        }
                        if (!mTxn.isFieldPresent(sfDividendTSprd))
                        {
                            m_journal.warning << "No dividend T spread";
                            return temINVALID;
                        }
                        break;
                    }
                    case DividendMaster::DivType_Done:
                    {
                        if (!mTxn.isFieldPresent(sfDividendResultHash))
                        {
                            m_journal.warning << "No dividend result hash";
                            return temINVALID;
                        }
                        break;
                    }
                    default:
                        return temBAD_DIV_TYPE;
                }
            }
            return tesSUCCESS;
        }

        bool mustHaveValidAccount() override
        {
            return false;
        }

        //achieve consensus on which ledger to start dividend
        TER startCalc()
        {
            SLE::pointer dividendObject = mEngine->entryCache(ltDIVIDEND, Ledger::getLedgerDividendIndex());

            if (!dividendObject)
            {
                dividendObject = mEngine->entryCreate(ltDIVIDEND, Ledger::getLedgerDividendIndex());
            }

            m_journal.info << "Previous dividend object: " << dividendObject->getText();

            uint32_t dividendLedger = mTxn.getFieldU32(sfDividendLedger);
            uint64_t dividendCoins = mTxn.getFieldU64(sfDividendCoins);
            uint64_t dividendCoinsVBC = mTxn.getFieldU64(sfDividendCoinsVBC);

            dividendObject->setFieldU8(sfDividendState, DividendMaster::DivState_Start);
            dividendObject->setFieldU32(sfDividendLedger, dividendLedger);
            dividendObject->setFieldU64(sfDividendCoins, dividendCoins);
            dividendObject->setFieldU64(sfDividendCoinsVBC, dividendCoinsVBC);

            mEngine->entryModify(dividendObject);

            m_journal.info << "Current dividend object: " << dividendObject->getText();

            return tesSUCCESS;
        }

        //apply dividend result here
        TER applyTx()
        {
            if (m_journal.debug.active())
                m_journal.debug << "radar: apply dividend.";

            auto ledgerSeq = mTxn.getFieldU32(sfDividendLedger);
            const Account& account = mTxn.getFieldAccount160(sfDestination);

            if (m_journal.trace.active()) {
                m_journal.trace << "des account " << RippleAddress::createAccountID(account).humanAccountID();
            }
            
            uint64_t divCoinsVBC = mTxn.getFieldU64(sfDividendCoinsVBC);
            uint64_t divCoins = mTxn.getFieldU64(sfDividendCoins);
            
            if (divCoinsVBC == 0 && divCoins ==0)
                return tesSUCCESS;
            
            SLE::pointer sleAccoutModified = mEngine->entryCache(
                ltACCOUNT_ROOT, getAccountRootIndex(account));

            if (sleAccoutModified)
            {
                mEngine->entryModify(sleAccoutModified);
                if (divCoinsVBC > 0)
                {
                    uint64_t prevBalanceVBC = sleAccoutModified->getFieldAmount(sfBalanceVBC).getNValue();
                    sleAccoutModified->setFieldAmount(sfBalanceVBC, prevBalanceVBC + divCoinsVBC);
                    mEngine->getLedger()->createCoinsVBC(divCoinsVBC);
                }
                if (divCoins > 0)
                {
                    uint64_t prevBalance = sleAccoutModified->getFieldAmount(sfBalance).getNValue();
                    sleAccoutModified->setFieldAmount(sfBalance, prevBalance + divCoins);
                    mEngine->getLedger()->createCoins(divCoins);
                }
                

                
                //Record VSpd, TSpd, DividendLedgerSeq
                if (mTxn.isFieldPresent(sfDividendLedger))
                {
                    std::uint32_t divLedgerSeq = mTxn.getFieldU32(sfDividendLedger);
                    sleAccoutModified->setFieldU32(sfDividendLedger, divLedgerSeq);
                    
                    if (mTxn.isFieldPresent(sfDividendVRank))
                    {
                        std::uint64_t divVRank = mTxn.getFieldU64(sfDividendVRank);
                        sleAccoutModified->setFieldU64(sfDividendVRank, divCoins);
                    }
                    
                    if (mTxn.isFieldPresent(sfDividendVSprd))
                    {
                        std::uint64_t divVSpd = mTxn.getFieldU64(sfDividendVSprd);
                        sleAccoutModified->setFieldU64(sfDividendVSprd, divVSpd);
                    }
                    
                    if (mTxn.isFieldPresent(sfDividendTSprd))
                    {
                        std::uint64_t divTSpd = mTxn.getFieldU64(sfDividendTSprd);
                        sleAccoutModified->setFieldU64(sfDividendTSprd, divTSpd);
                    }
                }
                
                if (m_journal.trace.active()) {
                    m_journal.trace << "Dividend Applied:" << sleAccoutModified->getText();
                }
                
                // convert refereces storage mothod
                if (sleAccoutModified->isFieldPresent(sfReferences))
                {
                    RippleAddress address = sleAccoutModified->getFieldAccount(sfAccount);
                    const STArray& references = sleAccoutModified->getFieldArray(sfReferences);
                    auto const referObjIndex = getAccountReferIndex (address.getAccountID());
                    SLE::pointer sleReferObj(mEngine->entryCache(ltREFER, referObjIndex));
                    if (sleReferObj)
                    {
                        m_journal.warning << "Has both sfReferences and ReferObj at the same time, this should not happen.";
                    }
                    else
                    {
                        sleReferObj = mEngine->entryCreate(ltREFER, referObjIndex);
                        sleReferObj->setFieldArray(sfReferences, references);
                        sleAccoutModified->delField(sfReferences);
                        m_journal.warning << address.getAccountID() << " references storage convert done.";
                    }
                }
            }
            else {
                if (m_journal.warning.active()) {
                    m_journal.warning << "Dividend account not found :" << RippleAddress::createAccountID(account).humanAccountID();
                }
            }
            
            return tesSUCCESS;
        }

        //mark as we have done dividend apply
        TER doneApply()
        {
            uint256 dividendResultHash = mTxn.getFieldH256(sfDividendResultHash);

            SLE::pointer dividendObject = mEngine->entryCache(ltDIVIDEND, Ledger::getLedgerDividendIndex());

            if (!dividendObject)
            {
                dividendObject = mEngine->entryCreate(ltDIVIDEND, Ledger::getLedgerDividendIndex());
            }

            m_journal.info << "Previous dividend object: " << dividendObject->getText();

            uint32_t dividendLedger = mTxn.getFieldU32(sfDividendLedger);
            uint64_t dividendCoins = mTxn.getFieldU64(sfDividendCoins);
            uint64_t dividendCoinsVBC = mTxn.getFieldU64(sfDividendCoinsVBC);

            dividendObject->setFieldU8(sfDividendState, DividendMaster::DivState_Done);
            dividendObject->setFieldU32(sfDividendLedger, dividendLedger);
            dividendObject->setFieldU64(sfDividendCoins, dividendCoins);
            dividendObject->setFieldU64(sfDividendCoinsVBC, dividendCoinsVBC);
            dividendObject->setFieldU64(sfDividendVRank, mTxn.getFieldU64(sfDividendVRank));
            dividendObject->setFieldU64(sfDividendVSprd, mTxn.getFieldU64(sfDividendVSprd));
            dividendObject->setFieldH256(sfDividendResultHash, dividendResultHash);

            mEngine->entryModify(dividendObject);

            m_journal.info << "Current dividend object: " << dividendObject->getText();

            return tesSUCCESS;
        }

        TER doApply () override
        {
            if (mTxn.getTxnType() == ttDIVIDEND)
            {
                uint8_t divOpType = mTxn.isFieldPresent(sfDividendType) ? mTxn.getFieldU8(sfDividendType) : DividendMaster::DivType_Start;
                switch (divOpType)
                {
                    case DividendMaster::DivType_Start:
                    {
                        return startCalc();
                    }
                    case DividendMaster::DivType_Apply:
                    {
                        return applyTx();
                    }
                    case DividendMaster::DivType_Done:
                    {
                        return doneApply();
                    }
                }
            }
            return temUNKNOWN;
        }
    };

    TER
    transact_Dividend(
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
    {
	    return Dividend (txn, params, engine).apply();
    }
}
