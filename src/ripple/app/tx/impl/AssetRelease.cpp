#include <BeastConfig.h>
#include <ripple/app/tx/impl/AssetRelease.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple
{

TER AssetRelease::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;
    
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
    return preflight2(ctx);
}

TER AssetRelease::preclaim (PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER AssetRelease::doApply ()
{
    auto& tx=ctx_.tx;
    
    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));
    Currency const currency (saDstAmount.getCurrency ());

    auto const k = keylet::asset (account_, currency);
    auto sleAsset = view ().peek (k);
    if (sleAsset)
    {
        // clear release schedule
        STArray releaseSchedule;
        sleAsset->setFieldArray(sfReleaseSchedule, releaseSchedule);
        view ().update (sleAsset);
    }
    else
    {
        return tefFAILURE;
    }

    return tesSUCCESS;
}
} // ripple
