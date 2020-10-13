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
#include <ripple/app/tx/impl/DepositCT.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/protocol/Indexes.h>

#include <secp256k1.h>
#include <secp256k1_rangeproof.h>

namespace ripple {

TER
DepositCT::preflight (PreflightContext const& ctx)
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
    STBlob const commit (ctx_.tx.getFieldAmount (sfCommit));

    uint64_t v = saAmount.mantissa();
    //secp256k1_pedersen_commitment commit;
    //auto result = secp256k1_pedersen_commit(secp256k1Context(), &commit, blind.data(), v, secp256k1_generator_h);
    //if(result != 1){
    //    return tefINTERNAL;
    //}

    auto const sle = view()->peek(keylet::ct(account_, curLgrSeq));
    if(sle){
        sle->setFieldSTObject(sfCommitment, commit.data);
        view().update(sle)
    }else{
        auto newSle = std::make_shared<STLedger>(keylet::ct(account_, curLgrSeq));
        newSle->setFieldSTObject(sfCommitment, commit.data);
        view().insert(newSle);
    }

    // update account_info
    auto actSLE = view()->peek(keylet:account(account_));
    actSLE->setFieldAmount(sfBalanceVBC, actSLE->getFiledAmount(sfBalanceVBC)-saAmount);
    view()->update(actSLE);

    return tesSUCCESS;
} 
}
