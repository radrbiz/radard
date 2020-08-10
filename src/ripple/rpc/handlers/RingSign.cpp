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

std::tuple<uint256, std::vector<uint256>, STVector256> RingSign(
    const std::string &message, 
    const STArray &publicKeys, 
    int index,
    uint256 privateKey)
{
    auto pt2uin256pair = [](const ec_point &pt)->std::pair<uint256, uint256>{
        bn_ctx ctx;
        bignum xBN, yBN;
        EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pt.get(), xBN.get(), yBN.get(), ctx.get());
        return std::make_pair(uint256_from_bignum_clear(xBN), uint256_from_bignum_clear(yBN));
    };

    auto publicKeysToHexString = [](const STArray &publicKeys)->std::string  {
        Serializer ser;
        for(auto const pk : publicKeys){
            STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
            ser.add256(keyPair[0]);
            ser.add256(keyPair[1]);
        }
        std::string serHex = "0x" + ser.getHex();         
        return serHex;
    };

    // auto debugPointOutput = [](const ec_point &pt, std::string prefix)->std::string {
    //     bn_ctx ctx;
    //     bignum xBN, yBN;
    //     EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pt.get(), xBN.get(), yBN.get(), ctx.get());
    //     return std::string(prefix 
    //         + "\n\tx=0x" 
    //         + std::string(BN_bn2hex(xBN.get())) 
    //         + ",\n\ty=0x" 
    //         + std::string(BN_bn2hex(yBN.get()))
    //     );
    // };

    auto sha256hash = [](const std::string &data)->uint256{
        sha256_hasher hasher;
        hasher(data.data(), data.size());
        std::array<std::uint8_t, 32> hRes = static_cast<typename sha256_hasher::result_type>(hasher);
        uint256 h;
        std::memcpy(h.data(), hRes.data(), hRes.size());
        return h;       
    };

    // auto uint256tostring = [](const uint256& n)->std::string {
    //     bignum N(n);
    //     return std::string(BN_bn2hex(N.get()));
    // };

    auto h1hash = [sha256hash](const std::string &d)->uint256 {
        // prefix 0x
        std::string data = "0x" + d;
        // to lower case
        std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { 
            return std::tolower(c); 
        });
        uint256 h1 = sha256hash(data);
        bignum h1BN = bignum(h1);
        bn_ctx ctx;
        BN_mod(h1BN.get(), h1BN.get(), N.get(), ctx.get());
        return uint256_from_bignum_clear(h1BN);
    };

    int keyCount = publicKeys.size();
    std::vector<uint256> c(keyCount);
    std::vector<uint256> s(keyCount);
    bn_ctx ctx;

    // STEP 1
    std::string pksHex = publicKeysToHexString(publicKeys);
    // log << "step1 data: " << pksHex;
    uint256 hBin = sha256hash(pksHex);

    ec_point h = scalarToPoint(hBin);
    // log << debugPointOutput(h, "h:");

    ec_point yTildePt = multiply2(altbn128::group(), h, bignum(privateKey), ctx); // h * privateKey
    // log << debugPointOutput(yTildePt, "yTildePt:");

    // STEP 2
        // yTilde
    // bignum yTildeBN = point2bn(altbn128::group(), yTildePt);
    // uint256 yTilde = uint256_from_bignum_clear(yTildeBN);
        // Gu
    bignum u = bignum::rand(256);
    // bignum u(from_hex_text<uint256>("1172a7084c95e4c3655602cc810042b28cb968f5dbe7577357990ef0fbf4735"));
    ec_point GuPt = multiply(altbn128::group(), u, ctx);        // bn128.ecMul(G, u)
    // log << debugPointOutput(GuPt, "GuPt:");
    
        // hu
    ec_point huPt = multiply2(altbn128::group(), h, u, ctx);    // bn128.ecMul(h, u)
    // log << debugPointOutput(huPt, "huPt:");
        // c[idx+1] = h1(...)
    Serializer ser;
    for(auto const pk : publicKeys){
        STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
        ser.add256(keyPair[0]);
        ser.add256(keyPair[1]);
    }
    auto yTildePtPair = pt2uin256pair(yTildePt);
    ser.add256(yTildePtPair.first);
    ser.add256(yTildePtPair.second);
    ser.addRaw(message.data(), message.size());
    auto GuPtPair = pt2uin256pair(GuPt);
    ser.add256(GuPtPair.first);
    ser.add256(GuPtPair.second);
    auto huPtPair = pt2uin256pair(huPt);
    ser.add256(huPtPair.first);
    ser.add256(huPtPair.second);
    uint256 hStep2 = h1hash(ser.getHex());

    // log << "c[(index+1) % keyCount]=" << uint256tostring(hStep2);

    c[(index+1) % keyCount] = hStep2;

    
    // STEP 3
    std::deque<int> indice;
    for (int i = index+1; i < keyCount; i++) {
        indice.push_back(i);
    }
    for (int i = 0; i < index; i++) {
        indice.push_back(i);
    }
    {
            std::string idxStr = "";
            for (int i : indice) {
                idxStr += to_string(i);
                idxStr += ", ";
            }
            // log << idxStr;
    }
    
    for (int i : indice) {
        // s[i]
        bignum sBN = bignum::rand(256);
        // bignum sBN(from_hex_text<uint256>("4d08ba2afba7642aa4a40e1df7860063c02aedba83934e8809ab37c3d59daf0"));
        // s[i] = uint256_from_bignum_clear(sBN);
        
        bignum cBN(c[i]);

        // log << "sBN: " << uint256tostring(s[i]);
        // z1
            // -- bn128.ecMul(G, s[i])
        ec_point GsPt = multiply(altbn128::group(), sBN, ctx);
        // log << debugPointOutput(GsPt, "GsPt:");
            // -- bn128.ecMul(publicKeys[i], c[i])
        uint256 pkx = publicKeys[i].getFieldV256(sfPublicKeyPair)[0];
        uint256 pky = publicKeys[i].getFieldV256(sfPublicKeyPair)[1];
        ec_point pkPt = set_coordinates(altbn128::group(), bignum(pkx), bignum(pky));
        ec_point pkcPt = multiply2(altbn128::group(), pkPt, cBN, ctx);
        // log << uint256tostring(c[i]);
        // log << debugPointOutput(pkPt, "pkPt:");
        // log << debugPointOutput(pkcPt, "pkcPt:");
            // -- z1 = bn128.ecAdd(bn128.ecMul(G, s[i]), bn128.ecMul(publicKeys[i], c[i]))
        ec_point z1Pt = add(altbn128::group(), GsPt, pkcPt, ctx);
        // log << debugPointOutput(z1Pt, "z1Pt:");


        // z2
            // -- bn128.ecMul(h, s[i])
        ec_point hsPt = multiply2(altbn128::group(), h, sBN, ctx);
            // -- bn128.ecMul(yTilde, c[i])
        ec_point hTcPt = multiply2(altbn128::group(), yTildePt, cBN, ctx);
            // z2 = bn128.ecAdd(bn128.ecMul(h, s[i]), bn128.ecMul(yTilde, c[i]))
        ec_point z2Pt = add(altbn128::group(), hsPt, hTcPt, ctx);

        // log << debugPointOutput(z2Pt, "z2Pt:");

        // // c[(i + 1) % keyCount] = h1(...)
        // c[(i + 1) % keyCount] = sha256(
        //     publicKeys,
        //     yTilde,
        //     message,
        //     z1,
        //     z2
        // );
        Serializer s3;
        for(auto const pk : publicKeys){
            STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
            s3.add256(keyPair[0]);
            s3.add256(keyPair[1]);
        }
        auto yTildePtPair = pt2uin256pair(yTildePt);
        s3.add256(yTildePtPair.first);
        s3.add256(yTildePtPair.second);
        s3.addRaw(message.data(), message.size());
        
        auto z1PtPair = pt2uin256pair(z1Pt);
        s3.add256(z1PtPair.first);
        s3.add256(z1PtPair.second);
        auto z2PtPair = pt2uin256pair(z2Pt);
        s3.add256(z2PtPair.first);
        s3.add256(z2PtPair.second);

        uint256 hStep3 = h1hash(s3.getHex());
        // log << "c[(i + 1) % keyCount]=" << uint256tostring(hStep3);
        c[(i + 1) % keyCount] = hStep3;

        s[i] = uint256_from_bignum_clear(sBN);
    }

    // log << "======";

    //uint256 zero(from_hex_text<uint256>("0"));

    // STEP 4
    bignum privateKeyBN(privateKey);
    // log << "privateKey: " << uint256tostring(privateKey);
    bignum cBN(c[index]);
    // log << "c[index]: " << uint256tostring(c[index]);
    bignum pkc;
    BN_mul(pkc.get(), privateKeyBN.get(), cBN.get(), ctx.get());
    // log << "pkc: ";
    bignum sci;
    BN_mod(sci.get(), pkc.get(), N.get(), ctx.get());
    // log << "sci: " << BN_bn2hex(sci.get());

    bignum usci;
    BN_sub(usci.get(), u.get(), sci.get());
    // log << "usci: " << BN_bn2hex(usci.get());

    bignum usci1;
    bignum zero(from_hex_text<uint256>("0"));
    if (BN_cmp(usci.get(), zero.get()) < 0) {
        BN_add(usci1.get(), usci.get(), N.get());
    } else {
        BN_copy(usci1.get(), usci.get());
    }
    // log << "usci1: " << BN_bn2hex(usci1.get());

    bignum sBN;
    BN_mod(sBN.get(), usci1.get(), N.get(), ctx.get());
    // log << "sBN: " << BN_bn2hex(sBN.get());

    s[index] = uint256_from_bignum_clear(sBN);
    // log << "s[index]: " << uint256tostring(s[index]);

    bignum c0BN(c[0]);
    uint256 c0 = uint256_from_bignum_clear(c0BN);

    // auto yTildePtPair = pt2uin256pair(yTildePt);
    STVector256 yTilde;
    yTilde.push_back(yTildePtPair.first);
    yTilde.push_back(yTildePtPair.second);
    return std::move(std::make_tuple(c0, s, yTilde));
}

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
    Json::Value jvResult;
    KeyPair keypair = RPC::keypairForSignature(params, jvResult);
    if (RPC::contains_error (jvResult)){
        return std::move (jvResult);
    }

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
    uint256 privateKey = keypair.secretKey.getAccountPrivate();

    auto singRes = RingSign(message, publicKeys, index, privateKey);

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
