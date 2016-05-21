#include <fstream>
#include <iostream>
#include <stdio.h>
#include <boost/algorithm/string.hpp>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

Json::Value doLoadDividend (RPC::Context &context)
{
    Json::Value jvResult;
    bool doSave = false;
    uint32_t ledgerIndex = context.params[jss::ledger_index].asUInt ();
    std::string hash = context.params[jss::hash].asString ();

    if (ledgerIndex == 0 && hash.empty ())
    {
        Ledger::pointer ledger = context.ledgerMaster.getValidatedLedger ();
        auto dividendSLE = ledger->read (keylet::dividend ());
        if (!dividendSLE)
            return RPC::make_error (rpcNO_CURRENT, "dividend object not found");
        ledgerIndex = (*dividendSLE)[sfDividendLedger];
        hash = to_string ((*dividendSLE)[sfDividendHash]);
    }
    else
    {
        doSave = true;
    }

    auto& dm = context.app.getDividendMaster ();
    
    if (!hash.empty())
    {
        std::pair<bool, Json::Value> ret;
        ret = dm.checkDividend (ledgerIndex, hash);
        if (ret.first)
        {
            // check dividend status,return if inProgress/done
            int state = dm.getDividendState ();
            jvResult = ret.second;
            switch (state)
            {
            case DividendMaster::DivType_Start:
                jvResult["dividend_state"] = to_string ("progress");
                break;
            case DividendMaster::DivType_Done:
                jvResult["dividend_state"] = to_string ("done");
                break;
            default:
                jvResult["dividend_state"] = to_string ("unknown");
            }
            jvResult [jss::ledger_index] = ledgerIndex;
            jvResult [jss::hash] = hash;
            if (!doSave)
                return jvResult;
        }
        else if (!doSave)
            return RPC::make_error (rpcINTERNAL, ret.second[jss::error_message].asString ());
    }

    // public key is needed to accept dividend transactions.
    std::string public_key = get<std::string> (context.app.config ()[SECTION_DIVIDEND_ACCOUNT], "public_key");
    if (public_key.empty ())
        return RPC::make_error (rpcNOT_ENABLED, "public_key missing in cfg.");
    auto accountPublic = parseBase58<AccountID> (public_key);
    if (!accountPublic)
        return RPC::make_error (rpcPUBLIC_MALFORMED, "invalid public_key in cfg.");

    // private key is needed to launch dividend.
    std::string secret_key = get<std::string> (context.app.config ()[SECTION_DIVIDEND_ACCOUNT], "secret_key");
    if (secret_key.empty ())
        return RPC::make_error (rpcNOT_ENABLED, "secret_key missing in cfg.");
    RippleAddress secret = RippleAddress::createSeedGeneric (secret_key);
    RippleAddress generator = RippleAddress::createGeneratorPublic (secret);
    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (generator, secret, 0);
    if (calcAccountID (RippleAddress::createAccountPublic (generator, 0)) != accountPublic)
        return RPC::make_error (rpcBAD_SECRET, "secret_key does not match public_key in cfg.");

    if (!dm.calcDividend (ledgerIndex))
        return RPC::make_error (rpcINTERNAL, "Failed to calculate dividend.");

    if (!dm.dumpTransactionMap (ledgerIndex, hash))
        return RPC::make_error (rpcINTERNAL, "Failed to store dividend.");

    jvResult[jss::hash] = to_string (dm.getResultHash ());

    // launch a new dividend
    if (!hash.empty() && !dm.launchDividend(ledgerIndex))
    {
        return RPC::make_error (rpcINTERNAL, "Failed to launch dividend");
    }

    return jvResult;
}

}// ripple