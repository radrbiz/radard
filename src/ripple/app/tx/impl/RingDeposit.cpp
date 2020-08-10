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
#include <ripple/app/tx/impl/RingDeposit.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/crypto/AltBn128.h>

namespace ripple {

static
uint256
createRingHash(const STArray pks, const uint64_t value, const uint32_t ringIndex){
    Serializer s;
    for(auto const& pk : pks){
        STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
        s.add256(keyPair[0]);
        s.add256(keyPair[1]);
    }
    s.add32(ringIndex);
    s.add64(value);
    return altbn128::sha256_s(s.getHex());
}

TER
RingDeposit::preflight (PreflightContext const& ctx)
{
    // check stx config
    if(!ctx.app.config ().exists (SECTION_SECRET_TX)){
        return tefINTERNAL;
    }

    // field check
    auto const amount = ctx.tx.getFieldAmount(sfAmount);
    std::string strLegalValues = get<std::string> (ctx.app.config ()[SECTION_SECRET_TX], "legal_values");
    std::vector<std::string> vecLegalValues;
    boost::split (vecLegalValues, strLegalValues, boost::is_any_of (","));
    if(std::find(vecLegalValues.begin(), vecLegalValues.end(), to_string(amount.mantissa())) == vecLegalValues.end()){
        JLOG(ctx.j.info) << "amount value:" << amount << " is illegal";
        return temBAD_AMOUNT;
    }
 
    const STVector256  stealthPk = ctx.tx.getFieldV256(sfPublicKeyPair);

    if(!isVBC(amount)){
        return temBAD_AMOUNT;
    }

    if(stealthPk.size() != 2){
        return telBAD_PUBLIC_KEY;
    }

    // check x/y on curve?
    openssl::bn_ctx bc;
    uint256 pkx = stealthPk[0];
    uint256 pky = stealthPk[1];
    if(!altbn128::onCurve(openssl::bignum(pkx), openssl::bignum(pky), bc)){
        JLOG(ctx.j.info) << "Stealth public key is not on curve.";
        return telBAD_PUBLIC_KEY;
    }

    return tesSUCCESS;
}

TER
RingDeposit::preclaim(PreclaimContext const& ctx){
    auto const id = ctx.tx[sfAccount];

    // check balance
    STAmount const amount (ctx.tx.getFieldAmount(sfAmount));
    auto const sle = ctx.view.read (keylet::account (id));
    if(!sle){
        return terNO_ACCOUNT;
    }
    STAmount balanceVBC = sle->getFieldAmount (sfBalanceVBC);
    if (balanceVBC < amount)
    {
        return tecUNFUNDED_TRANSFER;
    }

    // check request ringIndex
    uint32_t const ringIndexReq = ctx.tx.getFieldU32(sfRingIndex);
    if(ringIndexReq <= 0){
        JLOG(ctx.j.info) << "Ring:" << ringIndexReq << " is not found";
        return tecNO_ENTRY;
    }

    auto ringInfo = ctx.view.read(keylet::ring(amount.mantissa(), amount.issue(), 0));
    if(!ringInfo){  // first one
        if(ringIndexReq != 1){
            JLOG(ctx.j.info) << "First ringIndex:" << ringIndexReq << " starts from 1.";
            return tecNO_ENTRY;
        }
    }else{
        auto const lastRingIndex = ringInfo->getFieldU32(sfRingIndex);
        if(lastRingIndex != ringIndexReq){
            JLOG(ctx.j.info) << "RingIndex:" << ringIndexReq << " is obsolete";
            return tefRING_CLOSED;
        }
    }

    return tesSUCCESS;

}

TER
RingDeposit::doApply()
{
    auto& app = ctx_.app;
    const uint32_t curLgrSeq = view().info().seq;

    auto const aCore = parseBase58<AccountID>(get<std::string> (app.config ()[SECTION_SECRET_TX], "public_key"));
    if(!aCore){
        return tefINTERNAL;
    }
    auto const coreSle = view().read(keylet::account(*aCore));
    if(!coreSle){
        JLOG(j_.error) << "Secret transaction publickey account not found.";
        return tefINTERNAL;
    }

    STAmount const amount (ctx_.tx.getFieldAmount(sfAmount));
    uint32_t partNum = get<uint32_t> (app.config ()[SECTION_SECRET_TX], "participant_number");

    STVector256 const newPk = ctx_.tx.getFieldV256(sfPublicKeyPair);

    auto ringInfo = view().peek(keylet::ring(amount.mantissa(), amount.issue(), 0));
    uint32_t lastRingIndex = 0;
    if (ringInfo){
        // lastest index
        lastRingIndex = ringInfo->getFieldU32(sfRingIndex);
    } else {
        // create first ring
        ringInfo = std::make_shared<SLE>(keylet::ring(amount.mantissa(), amount.issue(), 0));
        lastRingIndex += 1;
        ringInfo->setFieldU32(sfRingIndex, lastRingIndex);
        view().insert(ringInfo);
        JLOG(j_.debug) << "Create index sle,quantity:" << amount.mantissa() << ",issue:" << amount.issue() << ",ringIndex:" << lastRingIndex;
    }

    // make sure index in params eq ringInfo
    uint32_t const ringIndexReq = ctx_.tx.getFieldU32(sfRingIndex);
    if (lastRingIndex != ringIndexReq){
        return tefRING_CLOSED;
    }

    auto ringSle = view().peek(keylet::ring(amount.mantissa(), amount.issue(), lastRingIndex));
    if (ringSle){
        int fastInv = get<int> (app.config ()[SECTION_SECRET_TX], "fast_interval");
        const auto startSeq = ringSle->getFieldU32(sfLedgerSequence);
        bool bFastDelay = fastInv > 0 && curLgrSeq - startSeq < fastInv; // true: close delayed

        const auto depositedNum = ringSle->getFieldU32(sfRingDeposited);
        auto ringHash = ringSle->getFieldH256(sfRingHash);
        const auto ringPartNum = ringSle->getFieldU32(sfParticipantsNum);
        if (ringHash == 0 && (depositedNum < ringPartNum || bFastDelay)){ // ring not closed
            // push account hash if not redundant
            auto acctHash = sha512Half(account_, amount.mantissa(), lastRingIndex);
            auto accounts = ringSle->getFieldV256(sfAccounts);
            if (std::find(accounts.begin(), accounts.end(), acctHash)!=accounts.end())
                return tefRING_REDUNDANT;
            accounts.push_back(acctHash);
            ringSle->setFieldV256(sfAccounts, accounts);
            // update deposited
            ringSle->setFieldU32(sfRingDeposited, depositedNum + 1);
            // push new pk
            STArray ringPks = ringSle->getFieldArray(sfPublicKeys);
            ringPks.push_back(STObject(sfRingHolder));
            STObject& obj = ringPks.back();
            obj.setFieldV256(sfPublicKeyPair, newPk);
            ringSle->setFieldArray(sfPublicKeys, ringPks);

            if(depositedNum + 1 >= ringPartNum && !bFastDelay){
                // become full, close this ring
                ringSle->setFieldH256(sfRingHash, createRingHash(ringPks, amount.mantissa(), lastRingIndex));
                lastRingIndex += 1;
                ringInfo->setFieldU32(sfRingIndex, lastRingIndex);
                view().update(ringInfo);
            }
            if(depositedNum + 1 > ringPartNum){
                ringSle->setFieldU32(sfParticipantsNum, depositedNum+1);
            }
            view().update(ringSle);
            JLOG(j_.info) << "udpate ring:" << lastRingIndex << ",parts:" << ringPartNum 
                << ",deposited:" << depositedNum+1
                << (bFastDelay ? ",close delayed" : ",not delayed");

            return transferXRP(view(), account_, *aCore, amount, j_);
        }else{
            // already full, close
            if (ringHash == 0){
                // set ring hash if needed
                uint256 ringHash = createRingHash(ringSle->getFieldArray(sfPublicKeys), amount.mantissa(), lastRingIndex);
                ringSle->setFieldH256(sfRingHash, ringHash);
                JLOG(j_.info) << "close ringhash:" << ringHash << ",nextRingIndex:" << lastRingIndex;
                view().update(ringSle);
            }
            lastRingIndex += 1;
            ringInfo->setFieldU32(sfRingIndex, lastRingIndex);
            view().update(ringInfo);
        }
    }

    // create new ring
    auto newRingSle = std::make_shared<SLE>(keylet::ring(amount.mantissa(), amount.issue(), lastRingIndex));
    newRingSle->setFieldU32(sfLedgerSequence, curLgrSeq);

    STVector256 accounts(sfAccounts);
    accounts.push_back(sha512Half(account_, amount.mantissa(), lastRingIndex));
    newRingSle->setFieldV256(sfAccounts, accounts);

    STArray ringPks (sfPublicKeys);
    ringPks.push_back(STObject(sfRingHolder));
    STObject& obj = ringPks.back();
    obj.setFieldV256(sfPublicKeyPair, newPk);
    newRingSle->setFieldArray(sfPublicKeys, ringPks);

    newRingSle->setFieldAmount(sfAmount, amount);
    newRingSle->setFieldU32(sfRingDeposited, 1);
    newRingSle->setFieldU32(sfRingWithdrawed, 0);
    newRingSle->setFieldU32(sfRingIndex, lastRingIndex);
    newRingSle->setFieldU32(sfParticipantsNum, partNum);
    view().insert(newRingSle);

    JLOG(j_.info) << "Generate new ring:" << lastRingIndex << ",pk:" << newPk[0] << ",amount:" << amount << ",participantsNumber:" << partNum;
    return transferXRP(view(), account_, *aCore, amount, j_);
} 



}
