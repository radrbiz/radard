#include <BeastConfig.h>
#include <ripple/app/tx/impl/ActiveAccount.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TER
ActiveAccount::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    bool const bPaths = tx.isFieldPresent (sfPaths);
    bool const bMax = tx.isFieldPresent (sfSendMax);
    
    if (bPaths)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Path not allowed.";
        return temBAD_SEND_XRP_PATHS;
    }
    if (bMax)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "SendMax not allowed.";
        return temBAD_SEND_XRP_MAX;
    }
    
    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));

    STAmount maxSourceAmount;
    auto const account = tx.getAccountID(sfAccount);

    if (saDstAmount.native ())
        maxSourceAmount = saDstAmount;
    else
        maxSourceAmount = STAmount (
            {saDstAmount.getCurrency (), account},
            saDstAmount.mantissa (), saDstAmount.exponent (),
            saDstAmount < zero);

    auto const& uSrcCurrency = maxSourceAmount.getCurrency ();
    auto const& uDstCurrency = saDstAmount.getCurrency ();

    if (!isLegalNet (saDstAmount) || !isLegalNet (maxSourceAmount))
        return temBAD_AMOUNT;

    auto const uDstAccountID = tx.getAccountID (sfReference);
    auto const uReferee = tx.getAccountID (sfReferee);

    if (!uDstAccountID)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Reference account not specified.";
        return temDST_NEEDED;
    }
    if (!uReferee)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Referee account not specified.";
        return temDST_NEEDED;
    }
    if (uDstAccountID == uReferee)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Referee should not be same with reference.";
        return temDST_IS_SRC;
    }
    if (maxSourceAmount < zero)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "bad max amount: " << maxSourceAmount.getFullText ();
        return temBAD_AMOUNT;
    }
    if (saDstAmount < zero)
    {
        JLOG(j.trace) << "Malformed transaction: "<<
            "bad dst amount: " << saDstAmount.getFullText ();
        return temBAD_AMOUNT;
    }
    else if (badCurrency () == uSrcCurrency || badCurrency () == uDstCurrency)
    {
        JLOG(j.trace) <<"Malformed transaction: " <<
            "Bad currency.";
        return temBAD_CURRENCY;
    }
    else if (account == uDstAccountID && uSrcCurrency == uDstCurrency)
    {
        // You're signing yourself a payment.
        // If bPaths is true, you might be trying some arbitrage.
        JLOG(j.trace) << "Malformed transaction: " <<
            "Redundant payment from " << to_string (account) <<
            " to self without path for " << to_string (uDstCurrency);
        return temREDUNDANT;
    }
    // additional checking for currency ASSET.
    if (assetCurrency () == uDstCurrency || assetCurrency () == uSrcCurrency)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Sending asset not allowed.";
        return temBAD_CURRENCY;
    }

    return preflight2 (ctx);
}

TER
ActiveAccount::preclaim (PreclaimContext const& ctx)
{
    AccountID const uDstAccountID(ctx.tx[sfReference]);
    STAmount const saDstAmount(ctx.tx[sfAmount]);

    auto const k = keylet::account(uDstAccountID);
    if (ctx.view.exists (k))
    {
        JLOG(ctx.j.trace) <<
             "account already created";
        return tefCREATED;
    }

    // Destination account dose not exist.
    if (!saDstAmount.native())
    {
        JLOG(ctx.j.trace) <<
            "Delay transaction: Destination account does not exist.";

        // Another transaction could create the account and then this
        // transaction would succeed.
        return tecNO_DST;
    }
    else if (saDstAmount < STAmount(ctx.view.fees().accountReserve(0)))
    {
        // accountReserve is the minimum amount that an account can have.
        // Reserve is not scaled by load.
        JLOG(ctx.j.trace) <<
            "Delay transaction: Destination account does not exist. " <<
            "Insufficent payment to create account.";

        // TODO: dedupe
        // Another transaction could create the account and then this
        // transaction would succeed.
        return tecNO_DST_INSUF_XRP;
    }

    return tesSUCCESS;
}

TER
ActiveAccount::doApply ()
{
    auto& tx = ctx_.tx;
    
    // referee
    AccountID const srcAccountID (tx.getAccountID (sfReferee));
    // reference
    AccountID const dstAccountID (tx.getAccountID (sfReference));
    AccountID const midAccountID (tx.getAccountID (sfAccount));

    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));
    STAmount maxSourceAmount;

    if (saDstAmount.native ())
        maxSourceAmount = saDstAmount;
    else
        maxSourceAmount = STAmount (
            {saDstAmount.getCurrency (), account_},
            saDstAmount.mantissa (), saDstAmount.exponent (),
            saDstAmount < zero);

    auto const& uSrcCurrency = maxSourceAmount.getCurrency ();
    auto const& uDstCurrency = saDstAmount.getCurrency ();

    JLOG(j_.trace) <<
        "maxSourceAmount=" << maxSourceAmount.getFullText () <<
        " saDstAmount=" << saDstAmount.getFullText ();

    // Open a ledger for editing.
    auto const k = keylet::account(dstAccountID);
    SLE::pointer sleDst = view().peek (k);

    if (!sleDst)
    {
        // Create the account.
        sleDst = std::make_shared<SLE>(k);
        sleDst->setAccountID (sfAccount, dstAccountID);
        sleDst->setFieldU32 (sfSequence, 1);
        view().insert(sleDst);
    }

    TER terResult;

    assert (saDstAmount.native ());

    auto const sle = view ().read (keylet::account (account_));
    // Direct XRP payment.

    // uOwnerCount is the number of entries in this legder for this
    // account that require a reserve.
    auto const uOwnerCount = sle->getFieldU32 (sfOwnerCount);

    // This is the total reserve in drops.
    auto const reserve = view().fees().accountReserve(uOwnerCount);

    // mPriorBalance is the balance on the sending account BEFORE the
    // fees were charged. We want to make sure we have enough reserve
    // to send. Allow final spend to use reserve for fee.
    auto const mmm = std::max(reserve,
        ctx_.tx.getFieldAmount (sfFee).xrp ());

    auto transfer = [&](STAmount const& saDstAmount)
    {
        bool isVBCTransaction = isVBC (saDstAmount);
        if (mPriorBalance < (isVBCTransaction ? 0 : saDstAmount.xrp ()) + mmm ||
            (isVBCTransaction && sle->getFieldAmount (sfBalanceVBC) < saDstAmount))
        {
            // Vote no. However the transaction might succeed, if applied in
            // a different order.
            JLOG(j_.trace) << "Delay transaction: Insufficient funds: " <<
                " " << to_string (mPriorBalance) <<
                " / " << to_string (saDstAmount.xrp () + mmm) <<
                " (" << to_string (reserve) << ")";

            terResult = tecUNFUNDED_PAYMENT;
        }
        else
        {
            // The source account does have enough money, so do the
            // arithmetic for the transfer and make the ledger change.
            if (!isVBCTransaction)
            {
                view ().peek (keylet::account (account_))->setFieldAmount (sfBalance,
                    mSourceBalance - saDstAmount);
                sleDst->setFieldAmount (sfBalance,
                    sleDst->getFieldAmount (sfBalance) + saDstAmount);
            }
            else
            {
                view ().peek (keylet::account (account_))->setFieldAmount (sfBalanceVBC,
                    sle->getFieldAmount (sfBalanceVBC) - saDstAmount);
                sleDst->setFieldAmount (sfBalanceVBC,
                    sleDst->getFieldAmount (sfBalanceVBC) + saDstAmount);
            }

            // Re-arm the password change fee if we can and need to.
            if ((sleDst->getFlags () & lsfPasswordSpent))
                sleDst->clearFlag (lsfPasswordSpent);

            terResult = tesSUCCESS;
        }
    };

    transfer (saDstAmount);

    if (terResult == tesSUCCESS && tx.isFieldPresent (sfAmounts))
    {
        auto const saAmounts (tx.getFieldArray (sfAmounts));
        for (auto const& saEntry : saAmounts)
        {
            auto const& saAmount (saEntry.getFieldAmount (sfAmount));
            if (!saAmount.native ())
            {
                terResult = temBAD_CURRENCY;
                break;
            }
            transfer (saAmount);
        }
    }

    if (terResult == tesSUCCESS && tx.isFieldPresent (sfLimits))
    {
        auto const saLimits (tx.getFieldArray (sfLimits));
        for (auto const& saEntry : saLimits)
        {
            auto const& saLimitAmount (saEntry.getFieldAmount (sfLimitAmount));
            bool const bQualityIn (saEntry.isFieldPresent (sfQualityIn));
            bool const bQualityOut (saEntry.isFieldPresent (sfQualityOut));

            Currency const currency (saLimitAmount.getCurrency ());
            AccountID uDstAccountID (saLimitAmount.getIssuer ());

            uint256 index (getRippleStateIndex (
                dstAccountID, uDstAccountID, currency));
            if (view ().exists (keylet::line (index)))
            {
                j_.error << "Exception trust line to " << uDstAccountID;
                terResult = tefEXCEPTION;
                break;
            }

            // true, iff current is high account.
            bool const bHigh = dstAccountID > uDstAccountID;

            std::uint32_t uQualityIn (bQualityIn ? saEntry.getFieldU32 (sfQualityIn) : 0);
            std::uint32_t uQualityOut (bQualityOut ? saEntry.getFieldU32 (sfQualityOut) : 0);

            if (bQualityOut && QUALITY_ONE == uQualityOut)
                uQualityOut = 0;

            std::uint32_t const uTxFlags = saEntry.getFlags ();

            bool const bSetAuth = (uTxFlags & tfSetfAuth);
            bool const bClearNoRipple = (uTxFlags & tfClearNoRipple);
            bool const bSetNoRipple = (uTxFlags & tfSetNoRipple);
            bool const bSetFreeze = (uTxFlags & tfSetFreeze);
            bool const bClearFreeze = (uTxFlags & tfClearFreeze);

            if (assetCurrency () == currency && bClearNoRipple)
            {
                terResult = temDISABLED;
            }

            STAmount saLimitAllow = saLimitAmount;
            saLimitAllow.setIssuer (dstAccountID);

            // Zero balance in currency.
            STAmount saBalance ({currency, noAccount ()});

            j_.trace <<
                "doTrustSet: Creating ripple line: " <<
                to_string (index);

            // Create a new ripple line.
            terResult = trustCreate (view (),
                bHigh,
                dstAccountID,
                uDstAccountID,
                index,
                sleDst,
                bSetAuth,
                bSetNoRipple && !bClearNoRipple,
                bSetFreeze && !bClearFreeze,
                saBalance,
                saLimitAllow, // Limit for who is being charged.
                uQualityIn,
                uQualityOut, ctx_.app.journal ("View"));

            if (terResult != tesSUCCESS)
                break;
        }
    }

    if (terResult == tesSUCCESS)
        terResult = addRefer (view (), srcAccountID, dstAccountID, ctx_.app.journal ("View"));

    std::string strToken;
    std::string strHuman;

    if (transResultInfo (terResult, strToken, strHuman))
    {
        JLOG(j_.trace) <<
            strToken << ": " << strHuman;
    }
    else
    {
        assert (false);
    }

    return terResult;
}
}//ripple
