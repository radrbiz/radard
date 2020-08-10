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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>

namespace ripple {

Json::Value doRingInfo (RPC::Context& context)
{
    Json::Value ret (Json::objectValue);

    uint32_t ringIndex = context.params[jss::index].asUInt();

    uint32_t const quantity = context.params["quantity"].asInt();

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger ();
    JLOG(context.j.info) << "query quantity:" << quantity << ",issue:" << vbcIssue() << ",ringIndex:" << ringIndex;
    auto const ringSle = currentLedger->read(keylet::ring(quantity, vbcIssue(), ringIndex));
    if (!ringSle){
        ret[jss::error_message] = "ring not found.";
        return ret;
    } else {
        if(ringSle->isFieldPresent(sfRingHash)){
            ret["ring_hash"] = to_string(ringSle->getFieldH256(sfRingHash));
        }
        if(ringSle->isFieldPresent(sfRingDeposited)){
            ret["deposited"] = to_string(ringSle->getFieldU32(sfRingDeposited));
        }
        if(ringSle->isFieldPresent(sfRingWithdrawed)){
            ret["withdrawed"] = ringSle->getFieldU32(sfRingWithdrawed);
        }
        if(ringSle->isFieldPresent(sfPublicKeys)){
            STArray pks = ringSle->getFieldArray(sfPublicKeys);
            Json::Value& jvpks = ret["public_keys"];
            for(auto const pk : pks){
                STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
                Json::Value& entry = jvpks.append(Json::arrayValue);
                entry.append(to_string(keyPair[0]));
                entry.append(to_string(keyPair[1]));
            }
        }
        if(ringSle->isFieldPresent(sfKeyImages)){
            STArray images = ringSle->getFieldArray(sfKeyImages);
            Json::Value& jvpks = ret["key_images"];
            for(auto const img : images){
                STVector256 pair = img.getFieldV256(sfKeyImage);
                Json::Value& entry = jvpks.append(Json::arrayValue);
                entry.append(to_string(pair[0]));
                entry.append(to_string(pair[1]));
            }
        }
        if(ringSle->isFieldPresent(sfRingIndex)){
            ret["ring_index"] = ringSle->getFieldU32(sfRingIndex);
        }
        if(ringSle->isFieldPresent(sfLedgerSequence)){
            ret["ledger_seq"] = ringSle->getFieldU32(sfLedgerSequence);
        }
        if(ringSle->isFieldPresent(sfAmount)){
            ret["amount"] = ringSle->getFieldAmount(sfAmount).getJson(0);
        }
        if(ringSle->isFieldPresent(sfParticipantsNum)){
            ret["participants_num"] = ringSle->getFieldU32(sfParticipantsNum);
        }
    }

    return ret;
}

} // ripple