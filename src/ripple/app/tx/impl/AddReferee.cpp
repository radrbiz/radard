#include <BeastConfig.h>
#include <ripple/app/tx/impl/AddReferee.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TER
AddReferee::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;
    
    AccountID const refereeID (ctx.tx.getAccountID (sfDestination));
    AccountID const referenceID (ctx.tx.getAccountID (sfAccount));

    if (!refereeID)
    {
        JLOG(ctx.j.warning) << "Malformed transaction: Referee account not specified.";

        return temDST_NEEDED;
    }
    else if (referenceID == refereeID)
    {
        // You're referring yourself.
        JLOG(ctx.j.trace) << "Malformed transaction: Redundant transaction:"
                        << " reference=" << to_string (referenceID) << " referee=" << to_string (refereeID);

        return temDST_IS_SRC;
    }
    
    return preflight2 (ctx);
}

TER
AddReferee::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
AddReferee::doApply ()
{
    AccountID const refereeID (ctx_.tx.getAccountID (sfDestination));
    AccountID const referenceID (account_);

    return addRefer (view (), refereeID, referenceID, ctx_.app.journal ("View"));
}

}  // ripple
