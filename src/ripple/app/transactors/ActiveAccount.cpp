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

        //if currency is native(VRP/VBC), charge 1/1000 of transfer amount(1000 at least),
        //otherwise charge a fix amount of fee(0.001)
        STAmount const amount (mTxn.getFieldAmount (sfAmount));
        feeByTrans += amount.isNative() ? std::max(int(amount.getNValue() * d.FEE_DEFAULT_RATE_NATIVE), 1000) : d.FEE_DEFAULT_NONE_NATIVE;

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
        
        if (maxSourceAmount < zero)
        {
            m_journal.trace <<
                "Malformed transaction: bad max amount: " << maxSourceAmount.
                    getFullText();
            return temBAD_AMOUNT;
        }
        else if (saDstAmount < zero)
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
                // getReserve() is the minimum amount that an account can have.
                // Reserve is not scaled by load.
                m_journal.trace <<
                "Delay transaction: Destination account does not exist. " <<
                "Insufficent payment to create account.";
                
                // TODO: dedupe
                // Another transaction could create the account and then this
                // transaction would succeed.
                return tecNO_DST_INSUF_XRP;
            }
            auto const newIndex = getAccountRootIndex (dstAccountID);
            sleDst = mEngine->entryCreate (ltACCOUNT_ROOT, newIndex);
            sleDst->setFieldAccount (sfAccount, dstAccountID);
            sleDst->setFieldU32 (sfSequence, 1);
        }
        else{
            m_journal.trace << "Malformed transaction: reference account already exists.";
            return tefREFERENCE_EXIST;
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

        return mEngine->view ().addRefer(srcAccountID, dstAccountID);
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