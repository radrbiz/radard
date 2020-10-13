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
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <boost/format.hpp>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <boost/optional.hpp>
#include <sstream>
#include <boost/algorithm/string.hpp>

namespace ripple {

Json::Value doProposalInfo (RPC::Context& context) {
    Json::Value ret (Json::objectValue);

    uint32_t proposalIndex = context.params[jss::index].asUInt();
    const auto &currentLedger = context.ledgerMaster.getCurrentLedger();
    if (!currentLedger) {
        return rpcError(rpcLGR_NOT_FOUND);
    }
    const auto proposalSle = currentLedger->read(keylet::proposal(proposalIndex));
    if (!proposalSle) {
        return rpcError(rpcGENERAL);
    }

    uint32_t expireTime = proposalSle->getFieldU32(sfProposalExpire);

    ret["proposer"] = to_string(proposalSle->getAccountID(sfAccount));
    ret["index"] = proposalSle->getFieldU32(sfProposalIndex);
    ret["options"] = proposalSle->getFieldU8(sfProposalOptions);
    ret["expire"] = expireTime;
    if (proposalSle->isFieldPresent(sfProposalCloseLedger)) {
        ret["close_on_ledger"] = proposalSle->getFieldU32(sfProposalCloseLedger);
    } else if (currentLedger->info().closeTime >= expireTime) {
        // thit proposal has been expire
        auto closeLedger = context.app.getLedgerMaster().getLedgerByCloseTime(expireTime);
        if (closeLedger) {
            ret["close_on_ledger"] =closeLedger->info().seq;
        }
    }
    if (proposalSle->isFieldPresent(sfProposalAppendix)) {
        const auto &appendix = proposalSle->getFieldVL(sfProposalAppendix);
        ret["appendix"] = strHex(appendix.data(), appendix.size());
    }

    if (context.app.getVoteCountingDB().getType() == DatabaseCon::Type::None) {
        return ret;
    }

    // checking vote statistic of this proposal
    auto db = context.app.getVoteCountingDB().checkoutDb();
    std::string sql;
    uint32_t lastLedgerSeq;
    std::string statistic;
    std::tm timestamp;

    *db << "SELECT Ledger, Statistic, Timestamp FROM VoteCounting WHERE Proposal=:proposalIndex ORDER BY Ledger DESC LIMIT 1", 
        soci::use(proposalIndex), soci::into(lastLedgerSeq), soci::into(statistic), soci::into(timestamp);
    if (db->got_data()) {
        Json::Value& statisticVal(ret["votes"] = Json::objectValue);
        statisticVal["ledger"] = lastLedgerSeq;
        Json::Value& accountsVote(statisticVal["account"] = Json::arrayValue);
        Json::Value& vbcVote(statisticVal["vbc"] = Json::arrayValue);
        std::vector<std::string> parts;
        boost::split(parts, statistic, boost::is_any_of(";"));
        std::vector<std::string> accounts;
        boost::split(accounts, parts[0], boost::is_any_of("|"));
        std::vector<std::string> vbcs;
        boost::split(vbcs, parts[1], boost::is_any_of("|"));
        for (int i = 0; i < accounts.size(); i++) {
            Json::Value& acc = accountsVote.append(Json::intValue);
            acc = std::stoi(accounts[i]);
            Json::Value& vbc = vbcVote.append(Json::stringValue);
            vbc = vbcs[i];
        }
    }

    return ret;
}

Json::Value doAccountVote (RPC::Context& context) {
    auto &params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger) {
        return result;
    }

    if (!params.isMember(jss::account) && !params.isMember(jss::ident)) {
        return RPC::missing_field_error (jss::account);
    }

    std::string strIdent = params.isMember(jss::account)
            ? params[jss::account].asString() : params[jss::ident].asString();
    bool bStrict = params.isMember(jss::strict) && params[jss::strict].asBool();
    AccountID accountID;

    // Get info on account.
    auto jvAccepted = RPC::accountFromString(accountID, strIdent, bStrict);

    if (jvAccepted) {
        return jvAccepted;
    }

    // read account root
    const auto sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted) {
        Json::Value& jsonVotes(result["votes"] = Json::arrayValue);
        if (sleAccepted->isFieldPresent(sfProposalVotes)) {
            STArray votes = sleAccepted->getFieldArray(sfProposalVotes);
            for (auto &vote : votes) {
                Json::Value& jsonVote(jsonVotes.append(Json::objectValue));
                jsonVote["proposal_index"] = vote.getFieldU32(sfProposalIndex);
                jsonVote["option"] = vote.getFieldU8(sfProposalVote);
            }
        }
    } else {
        result[jss::account] = context.app.accountIDCache().toBase58(accountID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }
    return result;
}

Json::Value doVoteCounting (RPC::Context& context) {
    if (context.app.getVoteCountingDB().getType() == DatabaseCon::Type::None) {
        return rpcError(rpcNOT_SUPPORTED);
    }

    auto &params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);

    if (!ledger) {
        return result;
    }
    // ledger caller want to calc on
    uint32_t ledgerSeq = ledger->info().seq;
    // proposal to calc
    uint32_t proposalIndex = context.params[jss::index].asUInt();
    const auto proposalSle = ledger->read(keylet::proposal(proposalIndex));
    if (!proposalSle) {
        return rpcError(rpcGENERAL);
    }
    // check whether has been closed
    uint32_t closeLedgerSeq = 0;
    uint32_t expireTime = proposalSle->getFieldU32(sfProposalExpire);
    if (proposalSle->isFieldPresent(sfProposalCloseLedger) && proposalSle->getFieldU32(sfProposalCloseLedger) > 0) {
        closeLedgerSeq = proposalSle->getFieldU32(sfProposalCloseLedger);
    } else if (expireTime < ledger->info().closeTime) {
        // proposal has expired but not closed by owner
        auto closeLedger = context.app.getLedgerMaster().getLedgerByCloseTime(expireTime);
        if (!closeLedger) {
            return rpcError(rpcGENERAL);
        }
        closeLedgerSeq = closeLedger->info().seq;
    }

    if (closeLedgerSeq > 0) {
        // if proposal is closed/expired, calc by close ledger seq
        ledgerSeq = closeLedgerSeq;
    }

    auto db = context.app.getVoteCountingDB().checkoutDb();
    std::string sql;
    uint64_t lastLedgerSeq;
    std::string statistic;
    std::tm timestamp;
    // checking ifg we have done this calc
    *db << "SELECT Ledger, Statistic, Timestamp FROM VoteCounting WHERE Proposal=:proposalIndex ORDER BY Ledger DESC LIMIT 1", 
        soci::use(proposalIndex), soci::into(lastLedgerSeq), soci::into(statistic), soci::into(timestamp);
    if (db->got_data()) {
        // already done
        if (ledgerSeq <= lastLedgerSeq) {
            // only calc based on newer ledger
            return result[""];
        }
    }

    uint8_t options = proposalSle->getFieldU8(sfProposalOptions);
    std::vector<uint64> accounts(options + 1, 0); // 0-not vote
    std::vector<uint64> vbcs(options + 1, 0);     // 0-not vote

    auto workingLedger = context.app.getLedgerMaster().getLedgerBySeq(ledgerSeq);
    if (!workingLedger) {
        return rpcError(rpcGENERAL);
    }
    // traverse all accounts
    workingLedger->visitStateItems([&](SLE::ref sle) {
        if (sle->getType () != ltACCOUNT_ROOT) {
            return;
        }
        const STAmount vbc = sle->getFieldAmount(sfBalanceVBC);
        if (!sle->isFieldPresent(sfProposalVotes)) {
            // this account vote nothing
            accounts[0] += 1;
            vbcs[0] += vbc.mantissa();
            return;
        }
        const STArray &votes = sle->getFieldArray(sfProposalVotes);
        bool found = false;
        for (const auto &vote : votes) {
            uint32_t index = vote.getFieldU32(sfProposalIndex);
            uint8_t optionVote = vote.getFieldU8(sfProposalVote);
            if (index == proposalIndex) {
                // this account vote this proposal
                accounts[optionVote] += 1;
                vbcs[optionVote] += vbc.mantissa();
                found = true;
                break;
            }
        }
        if (!found) {
            accounts[0] += 1;
            vbcs[0] += vbc.mantissa();
        }
    });

    // format result into statistic string
    std::stringstream ss;
    for (int i = 0; i < accounts.size(); i++) {
        ss << accounts[i];
        if (i != accounts.size() - 1) {
            ss << "|";
        }
    }
    ss << ";";
    for (int i = 0; i < vbcs.size(); i++) {
        ss << vbcs[i];
        if (i != vbcs.size() - 1) {
            ss << "|";
        }
    }
    
    statistic = ss.str();

    *db << "INSERT INTO VoteCounting (Proposal, Ledger, Statistic) VALUES(:proposalIndex, :ledgerSeq, :statistic)", 
        soci::use(proposalIndex), soci::use(ledgerSeq), soci::use(statistic);


    return result;
}

} // ripple