#include <BeastConfig.h>
#include <ripple/app/paths/RippleState.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>

namespace ripple {

void addLine (RPC::Context& context, Json::Value& jsonLines, RippleState const& line, std::shared_ptr<ReadView const> ledger);

// {
//   account: [<account>|<account_public_key>]
//   peer: [<account>|<account_public_key>]
//   currency: <currency>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value doAccountAsset (RPC::Context& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missing_field_error("account");
    if (!params.isMember(jss::peer))
        return RPC::missing_field_error("peer");
    if (!params.isMember(jss::currency))
        return RPC::missing_field_error("currency");

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);
    if (!ledger)
        return result;

    std::string strIdent (params[jss::account].asString ());
    AccountID raAccount;

    if (auto jv = RPC::accountFromString (raAccount, strIdent))
    {
        for (auto it = jv.begin (); it != jv.end (); ++it)
            result[it.memberName ()] = it.key ();

        return result;
    }

    if (! ledger->exists(keylet::account (raAccount)))
        return rpcError (rpcACT_NOT_FOUND);

    std::string strPeer (params[jss::peer].asString ());
    AccountID raPeerAccount;

    if (auto jv = RPC::accountFromString (raPeerAccount, strPeer))
    {
        for (auto it = jv.begin (); it != jv.end (); ++it)
            result[it.memberName ()] = it.key ();

        return result;
    }

    if (! ledger->exists(keylet::account (raPeerAccount)))
        return rpcError (rpcACT_NOT_FOUND);

    Currency uCurrency;
    if (!to_currency(uCurrency, params[jss::currency].asString()))
        return RPC::make_error(rpcSRC_CUR_MALFORMED, "Invalid field 'currency', bad currency.");

    auto sleNode = ledger->read (keylet::line (raAccount, raPeerAccount, uCurrency));
    auto const line = RippleState::makeItem (raAccount, sleNode);

    if (line == nullptr ||
        raAccount != line->getAccountID() ||
        raPeerAccount != line->getAccountIDPeer())
        return result;

    Json::Value jsonLines(Json::arrayValue);
    addLine(context, jsonLines, *line, ledger);
    result[jss::lines] = jsonLines[0u];
    
    Json::Value& jsonAssetStates(result[jss::states] = Json::arrayValue);
    
    // get asset_states for currency ASSET.
    if (assetCurrency() == line->getBalance().getCurrency()) {
        PaymentSandbox les (&*ledger, tapNONE);
        auto sleRippleState = les.peek (keylet::line (raAccount, raPeerAccount, assetCurrency ()));
        assetRelease (les, raAccount, raPeerAccount, assetCurrency (), sleRippleState, context.app.journal ("View"));

        uint256 baseIndex = getAssetStateIndex(line->getAccountID(), line->getAccountIDPeer(), assetCurrency());
        uint256 assetStateIndex = getQualityIndex(baseIndex);
        uint256 assetStateEnd = getQualityNext(assetStateIndex);

        for (;;) {
            auto const& sle = les.read (keylet::asset_state (assetStateIndex));
            if (sle) {
                STAmount amount = sle->getFieldAmount(sfAmount);
                STAmount released = sle->getFieldAmount(sfDeliveredAmount);

                if (sle->getAccountID(sfAccount) == line->getAccountIDPeer()) {
                    amount.negate();
                    released.negate();
                }

                auto reserved = released ? amount - released : amount;

                Json::Value& state(jsonAssetStates.append(Json::objectValue));
                state[jss::date] = static_cast<Json::UInt>(getQuality(assetStateIndex));
                state[jss::amount] = amount.getText();
                state[jss::reserve] = reserved.getText();
            }

            auto const nextAssetState =
                les.succ (assetStateIndex, assetStateEnd);

            if (!nextAssetState)
                break;

            assetStateIndex = *nextAssetState;
        }
    }

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
