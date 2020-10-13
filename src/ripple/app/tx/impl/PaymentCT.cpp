//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <boost/algorithm/string.hpp>
#include <ripple/app/tx/impl/PaymentCT.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/protocol/Indexes.h>

#include <secp256k1.h>
#include <secp256k1_rangeproof.h>

namespace ripple {

TER
PaymentCT::preflight (PreflightContext const& ctx)
{
    // check vbc only
    STAmount amount = ctx.tx.getFieldAmount(sfAmount);
    if(!isVBC(amount)){
        return temBAD_AMOUNT;
    }
    return tesSUCCESS;
}

TER
DepositCT::preclaim(PreclaimContext const& ctx){
    return tesSUCCESS;

}

TER
DepositCT::doApply()
{
    auto& app = ctx_.app;
    const uint32_t curLgrSeq = view().info().seq;

    STAmount const saAmount (ctx_.tx.getFieldAmount (sfAmount));
    uint256 const txPubKey(ctx_.tx.getFieldU256(sfTxPublicKey)); // tx_private_key(random)
    STArray const commits (ctx_.tx.getFieldArray (sfCommitments));

    static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);

    for(auto const commit : commits){
        if(commit.getFieldU8(sfCommitType) == CommitType::paymentCommit){
            //paymentCommit
        }
        uint256 blind = commit.getFieldU256(sfMaskCT);
        uint256 commitment = commit.getFieldU256(sfCommitment);
        uint64_t shieldValue = commit.getFieldU64(sfShieldValue);
        Blob proof = commit.getFieldVL(sfProof);

        // verify
        uint64_t minv = 0;
        uint64_t maxv = MAX_UINT64 - 1;
        int len = sizeof(proof);
        if(secp256k1_rangeproof_verify(ctx, &minv, &maxv, &commitment, proof, len, NULL, 0, secp256k1_generator_h) == 0){
            JLOG(j.error) << "Rangeproof verify error";
            return tefNO_AUTH_REQUIRED;
        }
    }

    // set account ctsle to change mask/value

    // create dst sle
    auto k = keylet::ct(commit, curLgrSeq)
    auto dstSle = view()->peek(k);
    if(dstSle){
        JLOG(j.error) << "commitment sle exists";
        return tefINTERNAL;
    }else{
        auto sle = std::make_shared<SLE>(k);
        sle->setFieldU256(sfMaskCT, );
        sle->setFieldU256(sfShieldValue);
    }


    return tesSUCCESS;
} 

}// ripple
