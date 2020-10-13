
//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/Context.h>

#include <secp256k1_rangeproof.h>
#include <secp256k1_ecdh.h>

namespace ripple {

// get first 8bytes(uint64) of uint256
uint64_t u256tou64(const unsigned char* in){
    const char* temp = reinterpret_cast<const char*>(in);
    return strtoull(temp, NULL, 0);
}

/** encode mask/value 
 *  mask = hash("commitment_mask" + hash(dh))
 *  shieldvalue = value XOR hash("amount" + hash(dh))
 *  In:
 *       unsigned char* dh
 *       uint64_t value
 *  Out:
 *       uint256 mask
 *       uint64_t shieldValue
 */
void encodeEcdh(const unsigned char* dh, uint64_t value, uint256& mask, uint64_t& shieldValue){
    std::string prefixAmount = "amount";
    std::string prefixMask = "commitment_mask";
    uint256 hDh = sha512Half(&dh);
    uint256 value_ = sha512Half(prefixAmount + to_string(hDh));
    value_ ^= uint256(value);
    mask = sha512Half(prefixMask + to_string(hDh));
    // uint256 to uint64
    shieldValue = u256tou64(value_.data());
}

/** decode
 * de(amount) = 8 byte encrypted amount XOR first 8 bytes of keccak("amount" || Hs(8aR||i))
 *  In:
 *    dh(private_key)
 *    shieldValue
 *  Out:
 *    decode_value
 */
void decodeEcdh(const unsigned char* dh, uint64_t shieldValue, uint64_t& value){
    std::string prefixAmount = "amount";
    uint256 hDh = sha512Half(&dh);
    uint256 temp = sha512Half(prefixAmount + to_string(hDh));
    uint256 value_ = uint256(shieldValue) ^ temp;
    value = u256tou64(value_.data());
}

// input:
//   account
//   account_private_view_key
//   destination_public_view_key
//   value
//   tx_key
// 
// output:
//   commits
//   mask, shieldValue
//   proofs
//   signatures
Json::Value doSignCT (RPC::Context& context)
{
    Json::Value jvResult;
    auto j = context.j;
    auto& params = context.params;
    if (!params.isMember("tx_key")) {
        return RPC::missing_field_error("tx_key");
    }
    std::string txKey(params["tx_key"].asString());

    if (!params.isMember("destination_public_view_key")){
        return RPC::missing_field_error("destination_public_view_key");
    }
    std::string dest(params["destination_public_view_key"].asString());

    if (!params.isMember("account_private_view_key")){
        return RPC::missing_field_error("account_private_view_key");
    }
    std::string actPriViewKey(params["account_private_view_key"].asString());

    uint64_t value = boost::lexical_cast<uint64_t>(params["value"].asString());
    // destination account
    if (!params.isMember(jss::account)) {
        return RPC::missing_field_error(jss::account);
    }
    AccountID srcAccountID;
    std::string strIdent (params[jss::account].asString());
     auto jvAccepted = RPC::accountFromString (srcAccountID, strIdent, false);
    if (jvAccepted){
        return rpcError(rpcACT_NOT_FOUND);
    }

    auto curLedger = context.ledgerMaster.getCurrentLedger();
    auto sleSrc = curLedger->read(keylet::account(srcAccountID));
    if(!sleSrc){
        return RPC::make_error(rpcSRC_ACT_NOT_FOUND, "account ct is not found");
    }

    // user ct commitment as input
    uint256 inputMask = sleSrc->getFieldH256(sfMaskCT);
    uint64_t inputValue = sleSrc->getFieldU64(sfShieldValue);
    secp256k1_pedersen_commitment inputCommit;
    if(secp256k1_pedersen_commit(ctx, &inputCommit, intputMask.data(), inputValue.data(), secp256k1_generator_h) != 1){
        JLOG(j.error) << "Compute input commitment error";
        return make_error(rpcINTERNAL, "Compute input commitment error");
    }

    //// dst commit
    //static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
    //unsigned char txPriKey[32];
    //std::copy(txKey.begin(), txKey.begin()+32, txPriKey);

    //secp256k1_pubkey txPubKey;
    //if(secp256k1_ec_pubkey_create(ctx, &txPubKey, txPriKey) != 1){
    //    JLOG(j.error) << "Compute txPubKey error";
    //    return make_error(rpcINTERNAL, "Compute txPubKey error");
    //};

    //secp256k1_pubkey dstViewPubKey;
    //secp256k1_ec_pubkey_parse(ctx, dstViewPubKey, dest.begin(), dest.length());

    //unsigned char dh[32];
    //if(secp256k1_ecdh(ctx, dh, &dstViewPubKey, txPriKey, NULL, NULL) != 1){
    //    JLOG(j.error) << "Compute ecdh error";
    //    return make_error(rpcINTERNAL, "Compute ecdh error");
    //}
    //uint256 mask1;
    //uint64_t amount1;
    //secp256k1_pedersen_commitment commit1;
    //encodeEcdh(dh, value, mask1, amount1);
    //if(!secp256k1_pedersen_commit(ctx, &commit1, mask1.data(), amount1, secp256k1_generator_h)){
    //    JLOG(j.error) << "Compute commitment error";
    //    return make_error(rpcINTERNAL, "Compute commitment error");
    //}

    //// change commit
    //// use account_private_view_key encode shieldvalue/mask
    //unsigned char dh2[32];
    //std::copy(actPriViewKey.begin(), actPriViewKey.begin()+32, dh2);
    ////if(secp256k1_ecdh(ctx, dh2, &txPubKey, actPriViewKey , NULL, NULL) != 1){
    ////    JLOG(j.error) << "Compute commitment error";
    ////    return make_error(rpcINTERNAL, "Compute commitment error");
    ////}
    
    //uint256 mask2;
    //uint64_t amount2;
    //secp256k1_pedersen_commitment commit2;
    //uint64_t changeValue = balance - value;
    //encodeEcdh(dh2, changeValue, mask2, amount2);
    //if(!secp256k1_pedersen_commit(ctx, &commit2, mask2.data(), amount2, secp256k1_generator_h)){
    //    JLOG(j.error) << "Compute commitment error";
    //    return make_error(rpcINTERNAL, "Compute commitment error");
    //}

    //// range proof
    //input_message = "fee|message";
    //uint64_t vmin = 0;
    //size_t len = 5134;
    //unsigned char inputProof[5134 + 1];
    //unsigned char paymentProof[5134 + 1];
    //unsigned char changeProof[5134 + 1];
    //if(!secp256k1_rangeproof_sign(ctx, inputProof, &len, vmin, &inputcommit, blind.data(), inputcommit.data, 0, 0, inputValue, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h)){
    //    JLOG(j.error) << "compute range proof error";
    //    return make_error(rpcINTERNAL, "compute range proof error");
    //}
    //if(!secp256k1_rangeproof_sign(ctx, paymentProof, &len, vmin, &commit1, mask1.data(), commit1.data, 0, 0, amount1, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h)){
    //    JLOG(j.error) << "compute range proof error";
    //    return make_error(rpcINTERNAL, "compute range proof error");

    //}
    //if(!secp256k1_rangeproof_sign(ctx, changeProof, &len, vmin, &commit2, mask2.data(), commit2.data, 0, 0, amount2, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h)){
    //    JLOG(j.error) << "compute range proof error";
    //    return make_error(rpcINTERNAL, "compute range proof error");
    //}

    //// input:[{"mask":"", "shield_value":"", "commit":"", "proof":""}]
    //// output:[{"mask":"", "shield_value":"", "commit":"", "proof":""}, {...}, {...}]
    //{
    //Json::Value& jvInput = jvResult["input"];
    //Json::Value& inputEntry = jvInput.append(Json::arrayValue); 
    //Json::Value& entry = inputEntry.append(Json::Object);
    //entry["mask"] = to_string(inputMask);
    //entry["shield_value"] = to_string(inputValue);
    //entry["commit"] = std::string(inputCommit.data);
    //entry["proof"] = std::string(inputProof.data);
    //entry["account"] = to_string(account);
    //}
    //{
    //Json::Value& jvInput = jvResult["output"];
    //Json::Value& inputEntry = jvInput.append(Json::arrayValue); 
    //Json::Value& entry = inputEntry.append(Json::Object);
    //entry["mask"] = to_string(mask1);
    //entry["shield_value"] = to_string(amount1);
    //entry["commit"] = std::string(commit1.data);
    //entry["proof"] = std::string(paymentProof.data);
    //entry["account"] = dest;
    //}
    //{
    //Json::Value& jvInput = jvResult["change"];
    //Json::Value& inputEntry = jvInput.append(Json::arrayValue); 
    //Json::Value& entry = inputEntry.append(Json::Object);
    //entry["mask"] = to_string(mask2);
    //entry["shield_value"] = to_string(amount2);
    //entry["commit"] = std::string(commit2.data);
    //entry["proof"] = std::string(changeProof.data);
    //entry["account"] = to_string(account);
    //}
    return jvResult;
}
}// ripple 