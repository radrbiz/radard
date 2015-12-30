#include <BeastConfig.h>
#include <ripple/app/tx/impl/IssueAsset.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple
{

TER IssueAsset::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;
    
    AccountID const uDstAccountID (tx.getAccountID (sfDestination));
    if (!uDstAccountID)
    {
        JLOG(j.trace) << "Malformed transaction: Issue destination account not specified.";
        return temDST_NEEDED;
    }
    else if (tx[sfAccount] == uDstAccountID)
    {
        JLOG(j.trace) << "Malformed transaction: Can not issue asset to self.";
        return temDST_IS_SRC;
    }

    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));
    if (saDstAmount <= zero)
    {
        JLOG(j.trace) << "Malformed transaction: bad amount: " << saDstAmount.getFullText ();
        return temBAD_AMOUNT;
    }
    else if (saDstAmount.getIssuer () != tx[sfAccount])
    {
        JLOG(j.trace) << "Malformed transaction: bad issuer: " << saDstAmount.getFullText ();
        return temBAD_ISSUER;
    }

    Currency const currency (saDstAmount.getCurrency ());
    if (currency != assetCurrency ())
    {
        JLOG(j.trace) << "Malformed transaction: bad currency: " << saDstAmount.getFullText ();
        return temBAD_CURRENCY;
    }

    STArray const releaseSchedule (tx.getFieldArray (sfReleaseSchedule));
    int64_t lastExpiration = -1, lastReleaseRate = -1;
    for (const auto& releasePoint : releaseSchedule)
    {
        const auto& rate = releasePoint.getFieldU32 (sfReleaseRate);
        if (rate <= lastReleaseRate || rate > QUALITY_ONE)
            return temBAD_RELEASE_SCHEDULE;

        const auto& expire = releasePoint.getFieldU32 (sfExpiration);

        if (rate == 0 && expire != 0)
            return temBAD_RELEASE_SCHEDULE;

        if (expire <= lastExpiration || expire % ctx.app.config ().ASSET_INTERVAL_MIN != 0)
            return temBAD_RELEASE_SCHEDULE;

        lastExpiration = expire;
        lastReleaseRate = rate;
    }
    
    return preflight2(ctx);
}

TER IssueAsset::preclaim (PreclaimContext const& ctx)
{
    AccountID const uDstAccountID (ctx.tx.getAccountID (sfDestination));

    if (!ctx.view.exists (keylet::account (uDstAccountID)))
    {
        JLOG(ctx.j.trace) << "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }
    return tesSUCCESS;
}

TER IssueAsset::doApply ()
{
    auto& tx=ctx_.tx;
    
    AccountID const uDstAccountID (tx.getAccountID (sfDestination));
    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));
    Currency const currency (saDstAmount.getCurrency ());
    STArray const releaseSchedule (tx.getFieldArray (sfReleaseSchedule));

    auto const k = keylet::asset (account_, currency);
    auto sleAsset = view ().peek (k);
    if (sleAsset)
    {
        JLOG(j_.trace) << "Asset already issued.";
        return tefCREATED;
    }

    sleAsset = std::make_shared<SLE> (k);
    sleAsset->setFieldAmount (sfAmount, saDstAmount);
    sleAsset->setAccountID (sfRegularKey, uDstAccountID);
    sleAsset->setFieldArray (sfReleaseSchedule, releaseSchedule);
    view ().insert (sleAsset);

    auto viewJ = ctx_.app.journal("View");
    return rippleCredit (view (), account_, uDstAccountID, saDstAmount, false, viewJ);
}
} // ripple
