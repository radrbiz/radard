//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>

#include <ripple/app/main/Application.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/types.h>
#include <ripple/rpc/impl/Utilities.h>

#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/KeypairForSignature.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>

#include <ripple/protocol/Serializer.h>
#include <ripple/crypto/impl/openssl.h>
#include <string>
#include <tuple>  
#include <vector>
#include <ripple/basics/Log.h>
#include <ripple/crypto/AltBn128.h>

namespace ripple {

using bignum = openssl::bignum;
using bn_ctx = openssl::bn_ctx;
using ec_point = openssl::ec_point;

using namespace openssl;
using namespace altbn128;

Json::Value doRingSign (RPC::Context& context)
{
    auto& params = context.params;

    // destination account
    if (!params.isMember(jss::account)) {
        return RPC::missing_field_error(jss::account);
    }
    AccountID destAccountID;
    std::string strIdent (params[jss::account].asString());
    auto jvAccepted = RPC::accountFromString (destAccountID, strIdent, false);
    if (jvAccepted){
        return rpcError(rpcACT_NOT_FOUND);
    }

    // secret key
    if(!params.isMember(jss::secret)){
        return RPC::missing_field_error (jss::secret);
    }
    uint256 privateKey = from_hex_text<uint256>(params[jss::secret].asString());

    uint32_t ring;
    // ring num
    if (!params.isMember(::Json::StaticString("ring"))){
        return RPC::missing_field_error (::Json::StaticString("ring"));       
    }
    ring = params[::Json::StaticString("ring")].asUInt();

    uint32_t index;
    // index in ring
    if (!params.isMember(::Json::StaticString("index"))){
        return RPC::missing_field_error (::Json::StaticString("index"));       
    }
    index = params[::Json::StaticString("index")].asUInt();

    // ring amount    
    if (!params.isMember(jss::Amount)) {
        return RPC::missing_field_error ("tx_json.Amount");
    }
    STAmount amount;
    if (!amountFromJsonNoThrow(amount, params[jss::Amount])) {
        return RPC::invalid_field_error ("tx_json.Amount");
    }

    // get ledger
    std::shared_ptr<ReadView const> ledger;
    RPC::lookupLedger(ledger, context);
    if (!ledger){
        return RPC::make_error(rpcLGR_NOT_FOUND, "ledgerNotFound");
    }

    // get ring sle
    auto ringSle = ledger->read(keylet::ring(amount.mantissa(), amount.issue(), ring));
    if (!ringSle){
        return RPC::make_error(rpcNOT_READY, "ringNotFound");
    }

    uint256 ringHash = ringSle->getFieldH256(sfRingHash);

    std::string message = to_string(ringHash) + toBase58(destAccountID);
    STArray publicKeys = ringSle->getFieldArray(sfPublicKeys);
    if(index >= publicKeys.size()){
        return RPC::invalid_field_error("tx_json.Index");
    }

    auto singRes = altbn128::ringSign(message, publicKeys, index, privateKey);

    Json::Value result;
    //std::tuple<uint256, std::vector<uint256>, STVector256>
    result["Digest"] = to_string(std::get<0>(singRes));

    Json::Value& signatures = (result["Signatures"] = Json::arrayValue);
    std::vector<uint256> sigs = std::get<1>(singRes);
    for(auto const s : sigs){
        signatures.append(to_string(s));
    }

    Json::Value& keyImage = result["KeyImage"];
    Json::Value& entry (keyImage.append(Json::arrayValue));
    auto ki = std::get<2>(singRes);
    if(ki.size() != 2){
        return RPC::make_error(rpcINTERNAL, "keyImage size error");
    }
    entry.append(to_string(ki[0]));
    entry.append(to_string(ki[1]));
 
    return result;
}

} // ripple
