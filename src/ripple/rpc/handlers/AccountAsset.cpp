#include <BeastConfig.h>
#include <ripple/rpc/impl/Tuning.h>
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
        return RPC::missing_field_error("accounts");
    if (!params.isMember(jss::currency))
        return RPC::missing_field_error("currency");

    Ledger::pointer ledger;
    Json::Value result(RPC::lookupLedger(params, ledger, context.netOps));
    if (!ledger)
        return result;

    RippleAddress naA;
    RippleAddress naB;
    Currency uCurrency;
    if (!params[jss::accounts].isArray() ||
        2 != params[jss::accounts].size() ||
        !params[jss::accounts][0u].isString() ||
        !params[jss::accounts][1u].isString() ||
        (params[jss::accounts][0u].asString() == params[jss::accounts][1u].asString())) {
        return RPC::make_error(rpcSRC_ACT_MISSING, "Invalid field 'accounts', two accounts needed.");
    } else if (!naA.setAccountID(params[jss::accounts][0u].asString()) ||
               !naB.setAccountID(params[jss::accounts][1u].asString())) {
        return RPC::make_error(rpcSRC_ACT_MALFORMED, "Invalid field 'accounts', bad account.");
    } else if (!to_currency(uCurrency, params[jss::currency].asString())) {
        return RPC::make_error(rpcSRC_CUR_MALFORMED, "Invalid field 'currency', bad currency.");
    }

    uint256 uNodeIndex = getRippleStateIndex(naA.getAccountID(), naB.getAccountID(), uCurrency);
    auto sleNode = context.netOps.getSLEi(ledger, uNodeIndex);
    auto const line(RippleState::makeItem(naA.getAccountID(), sleNode));

    if (line == nullptr ||
        naA.getAccountID() != line->getAccountID() ||
        naB.getAccountID() != line->getAccountIDPeer())
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
                    state[jss::date] = static_cast<Json::UInt>(boughtTime);

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
