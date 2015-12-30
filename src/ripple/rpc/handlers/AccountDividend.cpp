#include <BeastConfig.h>

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/types.h>
#include <ripple/rpc/impl/Utilities.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>

namespace ripple {

// account_dividend [account]
Json::Value doAccountDividend (RPC::Context& context)
{
    auto const& params (context.params);
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);
    if (! ledger)
        return result;

    std::string strIdent (params[jss::account].asString ());
    AccountID accountID;

    if (auto jv = RPC::accountFromString (accountID, strIdent))
    {
        for (auto it = jv.begin (); it != jv.end (); ++it)
            result[it.memberName ()] = it.key ();

        return result;
    }

    auto accountSLE = ledger->read (keylet::account (accountID));

    if (!accountSLE)
        return rpcError (rpcACT_NOT_FOUND);

    result[jss::account] = context.app.accountIDCache ().toBase58 (accountID);

    std::uint32_t baseLedgerSeq = 0;
    auto dividendSLE = ledger->read (keylet::dividend ());
    if (dividendSLE)
    {
        if (dividendSLE->getFieldU8 (sfDividendState) != DividendMaster::DivState_Done)
        {
            return RPC::make_error (rpcNOT_READY, "Dividend in progress");
        }
        Json::Value resumeToken;
        baseLedgerSeq = dividendSLE->getFieldU32 (sfDividendLedger);
        auto txns = context.netOps.getTxsAccount (
            accountID, baseLedgerSeq, ledger->info ().seq, false, resumeToken, 1,
            context.role == Role::ADMIN, "Dividend");
        if (!txns.empty ())
        {
            auto& txn = txns.begin ()->first->getSTransaction ();
            result["DividendCoins"] = to_string (txn->getFieldU64 (sfDividendCoins));
            result["DividendCoinsVBC"] = to_string (txn->getFieldU64 (sfDividendCoinsVBC));
            result["DividendCoinsVBCRank"] = to_string (txn->getFieldU64 (sfDividendCoinsVBCRank));
            result["DividendCoinsVBCSprd"] = to_string (txn->getFieldU64 (sfDividendCoinsVBCSprd));
            result["DividendTSprd"] = to_string (txn->getFieldU64 (sfDividendTSprd));
            result["DividendVRank"] = to_string (txn->getFieldU64 (sfDividendVRank));
            result["DividendVSprd"] = to_string (txn->getFieldU64 (sfDividendVSprd));
            result["DividendLedger"] = to_string (txn->getFieldU32 (sfDividendLedger));
            return result;
        }
    }

    result["DividendCoins"] = "0";
    result["DividendCoinsVBC"] = "0";
    result["DividendCoinsVBCRank"] = "0";
    result["DividendCoinsVBCSprd"] = "0";
    result["DividendTSprd"] = "0";
    result["DividendVRank"] = "0";
    result["DividendVSprd"] = "0";
    result["DividendLedger"] = to_string (baseLedgerSeq);
    
    return result;
}

} // ripple
