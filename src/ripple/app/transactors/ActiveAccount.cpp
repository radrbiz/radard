#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
class ActiveAccount
    : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const MaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const MaxPathLength = 8;

public:
    ActiveAccount(
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("ActiveAccount"))
    {
    
    }
    
    void calculateFee () override
    {
        mFeeDue = STAmount (mEngine->getLedger ()->scaleFeeLoad (
                        calculateBaseFee (), mParams & tapADMIN));

        Config d;
        std::uint64_t feeByTrans = 0;
        
        Account const uDstAccountID (mTxn.getFieldAccount160 (sfReference));
        auto const index = getAccountRootIndex (uDstAccountID);
        //dst account not exist yet, charge a fix amount of fee(0.01) for creating
        if (!mEngine->entryCache (ltACCOUNT_ROOT, index))
        {
            feeByTrans = d.FEE_DEFAULT_CREATE;
        }

        //if currency is native(VRP/VBC), charge 1/1000 of transfer amount,
        //otherwise charge a fix amount of fee(0.001)
        STAmount const amount (mTxn.getFieldAmount (sfAmount));
        feeByTrans += amount.isNative() ? amount.getNValue() * d.FEE_DEFAULT_RATE_NATIVE : d.FEE_DEFAULT_NONE_NATIVE;

        mFeeDue = std::max(mFeeDue, STAmount(feeByTrans, false));
    }
    
    TER doApply() override
    {
        // referee
        Account const srcAccountID (mTxn.getFieldAccount160 (sfReferee));
        // reference
        Account const dstAccountID (mTxn.getFieldAccount160(sfReference));
        Account const midAccountID (mTxn.getFieldAccount160(sfAccount));        
        
        STAmount const saDstAmount (mTxn.getFieldAmount(sfAmount));
        STAmount maxSourceAmount;

        if (saDstAmount.isNative())
            maxSourceAmount = saDstAmount;
        else
            maxSourceAmount = STAmount(
                {saDstAmount.getCurrency(), mTxnAccountID},
                saDstAmount.mantissa(), saDstAmount.exponent(),
                saDstAmount < zero);

        auto const& uSrcCurrency = maxSourceAmount.getCurrency ();
        auto const& uDstCurrency = saDstAmount.getCurrency ();
        
        bool const bXRPDirect = (uSrcCurrency.isZero() && uDstCurrency.isZero()) ||
            (isVBC(uSrcCurrency) && isVBC(uDstCurrency));
            
        m_journal.trace <<
            "maxSourceAmount=" << maxSourceAmount.getFullText () <<
            " saDstAmount=" << saDstAmount.getFullText ();
            
        if (!isLegalNet(saDstAmount) || !isLegalNet (maxSourceAmount))
            return temBAD_AMOUNT;
        
        if (!uDstCurrency)
        {
            m_journal.trace <<
                "Malformed transaction: Payment destination account not specified.";
            return  temDST_NEEDED;
        }
        else if (maxSourceAmount <= zero)
        {
            m_journal.trace <<
                "Malformed transaction: bad max amount: " << maxSourceAmount.
                    getFullText();
            return temBAD_AMOUNT;
        }
        else if (saDstAmount <= zero)
        {
            m_journal.trace <<
                "Malformed transaction: bad dst amount: " << saDstAmount.getFullText ();

            return temBAD_AMOUNT;
        }
        else if (badCurrency() == uSrcCurrency || badCurrency() == uDstCurrency)
        {
            m_journal.trace <<
                "Malformed transaction: Bad currency.";

            return temBAD_CURRENCY;
        }
        else if (mTxnAccountID == dstAccountID && uSrcCurrency == uDstCurrency)
        {
            // You're signing yourself a payment.
            // If bPaths is true, you might be trying some arbitrage.
            m_journal.trace <<
                "Malformed transaction: Redundant transaction:" <<
                " src=" << to_string (mTxnAccountID) <<
                " dst=" << to_string (dstAccountID) <<
                " src_cur=" << to_string (uSrcCurrency) <<
                " dst_cur=" << to_string (uDstCurrency);

            return temREDUNDANT;
        }
        
        //Open a ledger for editing.
        auto const index = getAccountRootIndex(dstAccountID);
        SLE::pointer sleDst (mEngine->entryCache(ltACCOUNT_ROOT, index));
        
        if (!sleDst)
        {
            // Destination account dose not exist.
            if (!saDstAmount.isNative())
            {
                m_journal.trace <<
                    "Delay transaction: Destination account does not exist.";

                // Another transaction could create the account and then this
                // transaction would succeed.
                return tecNO_DST;
            }
            else if (saDstAmount.getNValue() < mEngine->getLedger()->getReserve(0))
            {
            }
            auto const newIndex = getAccountRootIndex (dstAccountID);
            sleDst = mEngine->entryCreate (ltACCOUNT_ROOT, newIndex);
            sleDst->setFieldAccount (sfAccount, dstAccountID);
            sleDst->setFieldU32 (sfSequence, 1);
        }
        else if ((sleDst->getFlags() & lsfRequireDestTag) &&
                 !mTxn.isFieldPresent(sfDestinationTag))
        {
            // The tag is basically account-specific information we don't
            // understand, but we can require someone to fill it in.

            // We didn't make this test for a newly-formed account because there's
            // no way for this field to be set.
            m_journal.trace << "Malformed transaction: DestinationTag required.";
            return tefDST_TAG_NEEDED;
        }
        else
        {
            // Tell the engine that we are intending to change the the destination
            // account.  The source account gets always charged a fee so it's always
            // marked as modified.
            mEngine->entryModify(sleDst);
        }
        
        TER terResult;
        
        // Direct XRP payment.

        // uOwnerCount is the number of entries in this legder for this account
        // that require a reserve.

        std::uint32_t const uOwnerCount (mTxnAccount->getFieldU32 (sfOwnerCount));

        // This is the total reserve in drops.
        // TODO(tom): there should be a class for this.
        std::uint64_t const uReserve (mEngine->getLedger ()->getReserve (uOwnerCount));

        // mPriorBalance is the balance on the sending account BEFORE the fees were charged.
        //
        // Make sure have enough reserve to send. Allow final spend to use
        // reserve for fee.
        auto const mmm = std::max(uReserve, mTxn.getTransactionFee ().getNValue ());
        bool isVBCTransaction = isVBC(saDstAmount);
        if (mPriorBalance < (isVBCTransaction?0:saDstAmount) + mmm
            || (isVBCTransaction && mTxnAccount->getFieldAmount (sfBalanceVBC) < saDstAmount) )
        {
            // Vote no.
            // However, transaction might succeed, if applied in a different order.
            m_journal.trace << "Delay transaction: Insufficient funds: " <<
                " " << mPriorBalance.getText () <<
                " / " << (saDstAmount + uReserve).getText () <<
                " (" << uReserve << ")";

            terResult = tecUNFUNDED_PAYMENT;
        }
        else
        {
            // The source account does have enough money, so do the arithmetic
            // for the transfer and make the ledger change.
            m_journal.info << "radar: Deduct coin "
                << isVBCTransaction << mSourceBalance << saDstAmount;

            if (isVBCTransaction)
            {
                mTxnAccount->setFieldAmount(sfBalanceVBC, mTxnAccount->getFieldAmount (sfBalanceVBC) - saDstAmount);
                sleDst->setFieldAmount(sfBalanceVBC, sleDst->getFieldAmount(sfBalanceVBC) + saDstAmount);
            }
            else
            {
                mTxnAccount->setFieldAmount(sfBalance, mSourceBalance - saDstAmount);
                sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + saDstAmount);
            }



            // Re-arm the password change fee if we can and need to.
            if ((sleDst->getFlags () & lsfPasswordSpent))
                sleDst->clearFlag (lsfPasswordSpent);

            terResult = tesSUCCESS;
        }

        std::string strToken;
        std::string strHuman;

        if (transResultInfo (terResult, strToken, strHuman))
        {
            m_journal.trace <<
                strToken << ": " << strHuman;
        }
        else
        {
            assert (false);
        }

        
        //set referee
        // Open a ledger entry for editing.
        SLE::pointer sleReferee(mEngine->entryCache(ltACCOUNT_ROOT, getAccountRootIndex(srcAccountID)));
        SLE::pointer sleReference(mEngine->entryCache(ltACCOUNT_ROOT, getAccountRootIndex(dstAccountID)));
        
        auto const referenceReferIndex = getAccountReferIndex(dstAccountID);
        SLE::pointer sleReferenceRefer(mEngine->entryCache (ltREFER, 
            referenceReferIndex));
        auto const referIndex = getAccountReferIndex (srcAccountID);
        SLE::pointer sleRefer(mEngine->entryCache (ltREFER, referIndex));
        
        if (!sleReferee) {
            // Referee account does not exist.
            m_journal.trace <<  "Referee account does not exist.";

            return tecNO_DST;
        } else if ((sleReferenceRefer && !sleReferenceRefer->getFieldArray(sfReferences).empty())) {
            m_journal.trace << "Reference has been set.";
            return tefREFERENCE_EXIST;
        } else {
            STArray references(sfReferences);
            if (sleRefer && sleRefer->isFieldPresent(sfReferences)) {
                references = sleRefer->getFieldArray(sfReferences);
            }

            for (auto it = references.begin(); it != references.end(); ++it) {
                Account id = it->getFieldAccount(sfReference).getAccountID();
                if (id == dstAccountID) {
                    m_journal.trace << "Malformed transaction: Reference has been set.";
                    return tefREFERENCE_EXIST;
                }
            }

            int referenceHeight=0;
            if (sleReferee->isFieldPresent(sfReferenceHeight))
                referenceHeight=sleReferee->getFieldU32(sfReferenceHeight);
            
            mEngine->entryModify(sleReference);
            sleReference->setFieldAccount(sfReferee, srcAccountID);
            sleReference->setFieldU32(sfReferenceHeight, referenceHeight+1);
            references.push_back(STObject(sfReferenceHolder));
            references.back().setFieldAccount(sfReference, dstAccountID);
            if (!sleRefer) {
                sleRefer = mEngine->entryCreate(ltREFER, referIndex);
            } else {
                mEngine->entryModify(sleRefer);
            }
            sleRefer->setFieldArray(sfReferences, references);
        }
        return tesSUCCESS;
    }
};

TER
transact_ActiveAccount (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return ActiveAccount(txn, params, engine).apply();
};
}//ripple