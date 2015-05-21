#include <BeastConfig.h>
#include <ripple/rpc/impl/Tuning.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/app/paths/RippleState.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

void addLine (Json::Value& jsonLines, RippleState const& line, Ledger::pointer ledger);

// {
//   accounts: [<account>|<account_public_key>]
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value doAccountAsset (RPC::Context& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::accounts))
        return RPC::missing_field_error("account");
    if (!params.isMember(jss::peer))
        return RPC::missing_field_error("peer");
    if (!params.isMember(jss::currency))
        return RPC::missing_field_error("currency");

    Ledger::pointer ledger;
    Json::Value result(RPC::lookupLedger(params, ledger, context.netOps));
    if (!ledger)
        return result;

    std::string strIdent(params[jss::account].asString());
    bool bIndex(params.isMember(jss::account_index));
    int iIndex(bIndex ? params[jss::account_index].asUInt() : 0);
    RippleAddress rippleAddress;

    Json::Value const jv(RPC::accountFromString(ledger, rippleAddress, bIndex,
                                                strIdent, iIndex, false, context.netOps));
    if (!jv.empty()) {
        for (Json::Value::const_iterator it(jv.begin()); it != jv.end(); ++it)
            result[it.memberName()] = it.key();

        return result;
    }

    if (!ledger->hasAccount(rippleAddress))
        return rpcError(rpcACT_NOT_FOUND);

    std::string strPeer(params.isMember(jss::peer) ? params[jss::peer].asString() : "");
    bool bPeerIndex(params.isMember(jss::peer_index));
    int iPeerIndex(bIndex ? params[jss::peer_index].asUInt() : 0);
    RippleAddress rippleAddressPeer;

    Json::Value const jvPeer(RPC::accountFromString(ledger, rippleAddressPeer, bPeerIndex,
                                                    strPeer, iPeerIndex, false, context.netOps));
    if (!jvPeer.empty()) {
        return result;
    }

    if (!ledger->hasAccount(rippleAddressPeer))
        return rpcError(rpcACT_NOT_FOUND);

    Currency uCurrency;
    if (!to_currency(uCurrency, params[jss::currency].asString()))
        return RPC::make_error(rpcSRC_CUR_MALFORMED, "Invalid field 'currency', bad currency.");

    Account const& raAccount(rippleAddress.getAccountID());
    Account const& raPeerAccount(rippleAddressPeer.getAccountID());

    uint256 uNodeIndex = getRippleStateIndex(raAccount, raPeerAccount, uCurrency);
    auto sleNode = context.netOps.getSLEi(ledger, uNodeIndex);
    auto const line(RippleState::makeItem(raAccount, sleNode));

    if (line == nullptr ||
        raAccount != line->getAccountID() ||
        raPeerAccount != line->getAccountIDPeer())
        return result;

    Json::Value jsonLines(Json::arrayValue);
    addLine(jsonLines, *line, ledger);
    result[jss::lines] = jsonLines[0u];
    
    Json::Value& jsonAssetStates(result[jss::states] = Json::arrayValue);
    
    // get asset_states for currency ASSET.
    if (assetCurrency() == line->getBalance().getCurrency()) {
        uint256 baseIndex = getAssetStateIndex(line->getAccountID(), line->getAccountIDPeer(), assetCurrency());
        uint256 assetStateIndex = getQualityIndex(baseIndex);
        uint256 assetStateEnd = getQualityNext(assetStateIndex);

        for (;;) {
            auto const& sle = ledger->getSLEi(assetStateIndex);
            if (sle) {
                Account const& owner = sle->getFieldAccount160(sfAccount);
                STAmount amount = sle->getFieldAmount(sfAmount);
                STAmount delivered = sle->getFieldAmount(sfDeliveredAmount);

                auto const& sleAsset = ledger->getSLEi(getAssetIndex(amount.issue()));

                if (sleAsset) {
                    Json::Value& state(jsonAssetStates.append(Json::objectValue));
                    uint64_t boughtTime = getQuality(assetStateIndex);

                    auto const& releaseSchedule = sleAsset->getFieldArray(sfReleaseSchedule);
                    uint32_t releaseRate = 0;

                    for (auto const& releasePoint : releaseSchedule) {
                        if (boughtTime + releasePoint.getFieldU32(sfExpiration) > ledger->getParentCloseTimeNC())
                            break;

                        releaseRate = releasePoint.getFieldU32(sfReleaseRate);
                    }

                    STAmount released = multiply(amount, amountFromRate(releaseRate), amount.issue());

                    if (owner == line->getAccountIDPeer()) {
                        amount.negate();
                        released.negate();
                    }

                    auto reserved = amount - released;
                    
                    state[jss::date] = static_cast<Json::UInt>(boughtTime);
                    state[jss::amount] = amount.getText();
                    state[jss::reserve] = reserved.getText();
                }
            }

            auto const nextAssetState(
                ledger->getNextLedgerIndex(assetStateIndex, assetStateEnd));

            if (nextAssetState.isZero())
                break;

            assetStateIndex = nextAssetState;
        }
    }

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
