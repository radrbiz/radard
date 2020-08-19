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
#include <ripple/app/tx/impl/RingCancel.h>
#include <ripple/app/tx/impl/RingDeposit.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/crypto/AltBn128.h>


namespace ripple {

TER
RingCancel::preflight (PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    // check middle account in config
    if (! parseBase58<AccountID>(get<std::string> (ctx.app.config ()[SECTION_SECRET_TX], "public_key"))){
        return tefINTERNAL;
    }
 
    // check public key pair
    STVector256 const keyPair = tx.getFieldV256(sfPublicKeyPair);
    if(keyPair.size() != 2){
        return telBAD_PUBLIC_KEY;
    }

    // check pk on curve
    openssl::bn_ctx bc;
    uint256 pkx = keyPair[0];
    uint256 pky = keyPair[1];
    if(!altbn128::onCurve(openssl::bignum(pkx), openssl::bignum(pky), bc)){
        JLOG(j.info) << "Stealth public key is not on curve.";
        return telBAD_PUBLIC_KEY;
    }

    // only VBC
    STAmount const amount = tx.getFieldAmount (sfAmount);
    if (!isVBC(amount)){
        JLOG(j.info) << "Amount is not VBC.";
        return temBAD_AMOUNT;
    }

    // image elements == 2
    STVector256 keyImage = tx.getFieldV256(sfKeyImage);
    if (keyImage.size() != 2){
        JLOG(j.info) << "Image is malformed";
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}


TER
RingCancel::preclaim(PreclaimContext const& ctx){
    STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
    uint32_t const ringIndex = ctx.tx.getFieldU32(sfRingIndex);

    auto const k = keylet::ring(amount.mantissa(), amount.issue(), ringIndex);
    auto const ringSle = ctx.view.read(k);

    if(!ringSle)
        return tecNO_ENTRY;
    
    uint256 ringHash = ringSle->getFieldH256(sfRingHash);
    if(!ringHash.isZero()){
        JLOG(ctx.j.info) << "Ring is completed.";
        return tefRING_CLOSED;
    }

    uint32_t withdrawedNum = ringSle->getFieldU32(sfRingWithdrawed);
    uint32_t depositedNum = ringSle->getFieldU32(sfRingDeposited);
    if(withdrawedNum > 0){
        // this should not happen
        JLOG(ctx.j.fatal) << "Ring open but withdrawedNum:" << withdrawedNum;
        return tefBAD_LEDGER;
    }
    if(depositedNum == 0){
        JLOG(ctx.j.info) << "Ring depositedNum:" << depositedNum;
        return tecNO_TARGET;
    }

    STVector256 sigs = ctx.tx.getFieldV256(sfSignatures);
    STArray pks = ringSle->getFieldArray(sfPublicKeys);
    if(sigs.size() != pks.size()){
        JLOG(ctx.j.info) << "Ring signatures number:" << sigs.size() << "!eq publickey number:" << pks.size();
        return temBAD_SIGNATURE;
    }

    // check deposit/cancel from same account
    STVector256 pk_ = ctx.tx.getFieldV256(sfPublicKeyPair);
    AccountID account = ctx.tx.getAccountID(sfAccount);
    STVector256 accounts = ringSle->getFieldV256(sfAccounts);
    int pos = 0;
    for(auto const pk : pks){
        STVector256 const keyPair = pk.getFieldV256(sfPublicKeyPair);
        if(keyPair.empty()){
            continue;
        }
        if(keyPair[0] == pk_[0] && keyPair[1] == pk_[1]){
            break;
        }
        pos++;
    }
    if(accounts.size() < pos+1){
        // should not happen
        JLOG(ctx.j.fatal) << "accounts' length not match publickeys'.";
        return tecNO_TARGET;
    }
    uint256 depositHash = accounts[pos];
    uint256 cancelHash = sha512Half(std::string("D"), account, amount.mantissa(), ringIndex);
    JLOG(ctx.j.info) << "deposit account:" << depositHash
        << ",cancelHash:" << cancelHash << ",cancel account:" << account 
        << ",ring:" << ringIndex << ",amount:" << amount.getText();
    if(depositHash != cancelHash){
        JLOG(ctx.j.info) << "Deposit account doesn't match.";
        return tecNO_TARGET;
    }

    return tesSUCCESS;
}

TER
RingCancel::doApply()
{
    AccountID dest = account_;
    STAmount const amount = ctx_.tx.getFieldAmount(sfAmount);
    uint32_t const ringIndex = ctx_.tx.getFieldU32(sfRingIndex);
    uint256 c0 = ctx_.tx.getFieldH256(sfDigest);
    STVector256 keyImage = ctx_.tx.getFieldV256(sfKeyImage);
    STVector256 signatures = ctx_.tx.getFieldV256(sfSignatures);
    STVector256 pk_ = ctx_.tx.getFieldV256(sfPublicKeyPair);

    auto ringSle = view().peek(keylet::ring(amount.mantissa(), amount.issue(), ringIndex));

    if (!ringSle){
        JLOG(j_.info) << "Ring cannot be found.";
        return tecNO_ENTRY;
    }
    
    uint256 ringHash = ringSle->getFieldH256(sfRingHash);
    uint32_t depositedNum = ringSle->getFieldU32(sfRingDeposited);

    STArray publicKeys = ringSle->getFieldArray(sfPublicKeys);
    std::string msg = toBase58(dest);
    JLOG(j_.info) << "Message:" << msg;
    if(!altbn128::ringVerify(msg, c0, keyImage, signatures, publicKeys, j_)){
        JLOG(j_.info) << "Signatures verify unsuccessful.";
        return tefBAD_SIGNATURE;
    }
    
    // recall account
    auto acctHash = sha512Half(std::string("D"), dest, amount.mantissa(), ringIndex);
    auto accounts = ringSle->getFieldV256(sfAccounts);
    auto acctIter = std::find(accounts.begin(), accounts.end(), acctHash);
    if (acctIter==accounts.end())
        return tecNO_TARGET;
    accounts.erase(acctIter);
    ringSle->setFieldV256(sfAccounts, accounts);

    // recall publickey
    STArray newPks;
    bool bRemoved=false;
    for(auto const pk : publicKeys){
        STVector256 const keyPair = pk.getFieldV256(sfPublicKeyPair);
        if(keyPair.empty()){
            continue;
        }
        /// keyPair[0] == pk_[0] && keyPair[1] == pk_[1] or keyPair[0] == pk_[0] || keyPair[1] == pk_[0]???
        if(keyPair[0] == pk_[0] && keyPair[1] == pk_[1]){
            bRemoved = true;
            continue;
        }
        newPks.push_back(STObject(sfRingHolder));
        STObject& obj = newPks.back();
        obj.setFieldV256(sfPublicKeyPair, keyPair);

    }
    if (!bRemoved)
        return tecNO_TARGET;
    if(newPks.empty()){
        auto ringInfo = view().peek(keylet::ring(amount.mantissa(), amount.issue(), 0));
        if(!ringInfo){ // never happen
            return tefINTERNAL;
        }
        ringInfo->setFieldU32(sfRingIndex, ringInfo->getFieldU32(sfRingIndex) + 1);
        view().update(ringInfo);
        view().erase(ringSle);
    }else{
        ringSle->setFieldArray(sfPublicKeys, newPks);
    
        ringSle->setFieldU32(sfRingDeposited, depositedNum-1);
        view().update(ringSle);
    }

    // transfer
    auto const midAccount = parseBase58<AccountID>(get<std::string> (ctx_.app.config ()[SECTION_SECRET_TX], "public_key"));
    return transferXRP(view(), *midAccount, dest, amount, j_);
}
}
