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
#include <ripple/app/tx/impl/RingWithdraw.h>
#include <ripple/app/tx/impl/RingDeposit.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/crypto/AltBn128.h>


namespace ripple {

TER
RingWithdraw::preflight (PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

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
RingWithdraw::doApply()
{
    AccountID dest = account_;
    STAmount amount = ctx_.tx.getFieldAmount(sfAmount);
    uint32_t ringIndex = ctx_.tx.getFieldU32(sfRingIndex);
    uint256 c0 = ctx_.tx.getFieldH256(sfDigest);
    STVector256 keyImage = ctx_.tx.getFieldV256(sfKeyImage);
    STVector256 signatures = ctx_.tx.getFieldV256(sfSignatures);

    auto ringSle = view().peek(keylet::ring(amount.mantissa(), amount.issue(), ringIndex));

    if (!ringSle)
    {
        JLOG(j_.info) << "Ring cannot be found.";
        return tecNO_ENTRY;
    }

    uint32_t depositedNum = ringSle->getFieldU32(sfRingDeposited);
    uint32_t participantsNum = ringSle->getFieldU32(sfParticipantsNum);
    if(depositedNum != participantsNum){
        JLOG(j_.info) << "Ring depositedNum:" << depositedNum << " != participantsNum:" << participantsNum;
        return tefRING_NOT_CLOSED;
    }
    uint32_t withdrawedNum = ringSle->getFieldU32(sfRingWithdrawed);
    if(withdrawedNum >= depositedNum){
        // ring dry, which should not happen
        JLOG(j_.fatal) << "Ring should be deleted. withdrawedNum:" << withdrawedNum << " >= depositedNum:" << depositedNum;
        return tefBAD_LEDGER;
    }
    uint256 ringHash = ringSle->getFieldH256(sfRingHash);
    if(ringHash.isZero()){
        JLOG(j_.info) << "Ring is still open";
        return tefRING_NOT_CLOSED;
    }
    
    // account redundancy check
    auto acctHash = sha512Half(std::string("W"), dest, amount.mantissa(), ringIndex);
    auto accounts = ringSle->getFieldV256(sfAccounts);
    if (std::find(accounts.begin(), accounts.end(), acctHash)!=accounts.end())
        return tefRING_REDUNDANT;
    
    STArray publicKeys = ringSle->getFieldArray(sfPublicKeys);
    std::string msg = to_string(ringHash) + toBase58(dest);
    JLOG(j_.info) << "Message:" << msg;
    if(!altbn128::ringVerify(msg, c0, keyImage, signatures, publicKeys, j_))
    {
        JLOG(j_.info) << "Signatures verify unsuccessful.";
        return tefBAD_SIGNATURE;
    }
    
    // check double spend
    STArray images = ringSle->getFieldArray(sfKeyImages);
    for(auto const image : images){
        STVector256 imagePoint = image.getFieldV256(sfKeyImage);
        uint256 imageX = keyImage[0];
        if(imageX == imagePoint[0] || imageX == imagePoint[1]){
            // has spent image
            JLOG(j_.info) << "Spent keyimage.";
            return tefBAD_SIGNATURE;
        }
    }
    if(withdrawedNum + 1 == depositedNum ){
        // delete ringsle if withdrawed == deposited
        view().erase(ringSle);
        // update ringInfo
        auto ringInfo = view().peek(keylet::ring(amount.mantissa(), amount.issue(), 0));
        if(ringInfo->getFieldU32(sfRingIndex) == ringIndex){
            ringInfo->setFieldU32(sfRingIndex, ringIndex+1);
            view().update(ringInfo);
        }
    }else{
        // add account hash
        accounts.push_back(acctHash);
        ringSle->setFieldV256(sfAccounts, accounts);
        // add keyImages
        images.push_back(STObject(sfRingHolder));
        STObject& obj = images.back();
        obj.setFieldV256(sfKeyImage, keyImage);
        ringSle->setFieldArray(sfKeyImages, images);
        ringSle->setFieldU32(sfRingWithdrawed, withdrawedNum+1);
        view().update(ringSle);
    }

    // transfer
    auto const midAccount = parseBase58<AccountID>(get<std::string> (ctx_.app.config ()[SECTION_SECRET_TX], "public_key"));
    if(!midAccount){
        return tefINTERNAL;
    }
    return transferXRP(view(), *midAccount, dest, amount, j_);
}


}

