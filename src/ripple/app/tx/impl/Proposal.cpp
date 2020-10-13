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
#include <ripple/app/tx/impl/Proposal.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/app/ledger/LedgerMaster.h>


namespace ripple {

TER CreateProposal::preflight (PreflightContext const& ctx) {
    JLOG(ctx.j.info) << "CreateProposal::preflight";
    // check proposal config
    if(!ctx.app.config ().exists (SECTION_PROPOSAL_VOTE)){
        return tefINTERNAL;
    }
    // check vote admin config
    std::string strVoteAdmin = get<std::string> (ctx.app.config ()[SECTION_PROPOSAL_VOTE], "vote_admin");
    AccountID adminAccount;
    auto jvRet = RPC::accountFromString (adminAccount, strVoteAdmin, true);
    if (jvRet) {
        // no vote admin account
        return tefINTERNAL;
    }

    const auto &tx = ctx.tx;
    // sfProposalOptions
    if (!tx.isFieldPresent(sfProposalOptions) || ctx.tx.getFieldU8(sfProposalOptions) <= 0) {
        return temINVALID;
    }
    // sfProposalExpire
    if (!tx.isFieldPresent(sfProposalExpire) || ctx.tx.getFieldU32(sfProposalExpire) <= 0) {
        return temBAD_EXPIRATION;
    }

    return tesSUCCESS;
}

TER CreateProposal::preclaim(PreclaimContext const& ctx) {
    JLOG(ctx.j.info) << "CreateProposal::preclaim";
    const auto &tx = ctx.tx;
    const auto expire = ctx.tx.getFieldU32(sfProposalExpire);
    if (ctx.view.parentCloseTime() >= expire) {
        return temBAD_EXPIRATION;
    }
    const auto proposer = tx[sfAccount];
    const auto sle = ctx.view.read (keylet::account (proposer));
    if (!sle) {
        // proposal account must exist
        return terNO_ACCOUNT;
    }
    // check vote admin config
    std::string strVoteAdmin = get<std::string> (ctx.app.config ()[SECTION_PROPOSAL_VOTE], "vote_admin");
    AccountID adminAccount;
    RPC::accountFromString (adminAccount, strVoteAdmin, true);
    if (adminAccount != proposer) {
        return temINVALID;
    }

    return tesSUCCESS;
}


TER CreateProposal::doApply() {
    JLOG(j_.info) << "CreateProposal::doApply";
    // use keylet::proposal(0) to store general propoerties
    auto proposalInfo = view().peek(keylet::proposal(0));
    uint32_t lastProposalIndex = 0;
    if (proposalInfo) {
        lastProposalIndex = proposalInfo->getFieldU32(sfProposalIndex);
    } else {
        proposalInfo = std::make_shared<SLE>(keylet::proposal(0));
        proposalInfo->setFieldU32(sfProposalIndex, 0);
        view().insert(proposalInfo);
    }

    // new proposal entry
    auto newProposal = std::make_shared<SLE>(keylet::proposal(lastProposalIndex + 1));
    newProposal->setFieldU32(sfProposalIndex, lastProposalIndex + 1);
    newProposal->setAccountID(sfAccount, account_);
    newProposal->setFieldU8(sfProposalOptions, ctx_.tx.getFieldU8(sfProposalOptions));
    newProposal->setFieldU32(sfProposalExpire, ctx_.tx.getFieldU32(sfProposalExpire));
    // proposalInfo->setFieldU32(sfProposalCloseLedger, 0);
    if (ctx_.tx.isFieldPresent(sfProposalAppendix)) {
        newProposal->setFieldVL(sfProposalAppendix, ctx_.tx.getFieldVL(sfProposalAppendix));
    }
    view().insert(newProposal);

    // set largest proposal index
    proposalInfo->setFieldU32(sfProposalIndex, lastProposalIndex + 1);
    view().update(proposalInfo);

    return tesSUCCESS;
}

TER CloseProposal::preflight (PreflightContext const& ctx) {
    const auto &tx = ctx.tx;
    // sfProposalIndex
    if (!tx.isFieldPresent(sfProposalIndex) || ctx.tx.getFieldU32(sfProposalIndex) <= 0) {
        return temINVALID;
    }

    return tesSUCCESS;
}

TER CloseProposal::preclaim(PreclaimContext const& ctx) {
    const auto &tx = ctx.tx;
    uint32_t proposalIndex = tx.getFieldU32(sfProposalIndex);
    auto const k = keylet::proposal(proposalIndex);
    auto const proposalSle = ctx.view.read(k);
    // no proposal entry
    if (!proposalSle) {
        return tecNO_ENTRY;
    }
    // proposal has already been closed
    if (proposalSle->isFieldPresent(sfProposalCloseLedger) && proposalSle->getFieldU32(sfProposalCloseLedger) > 0) {
        return temINVALID;
    }
    // only proposer can close proposal
    const auto proposer = tx[sfAccount];
    if (proposalSle->isFieldPresent(sfAccount) && proposer != proposalSle->getAccountID(sfAccount)) {
        return temINVALID;
    }

    return tesSUCCESS;
}


TER CloseProposal::doApply() {
    uint32_t proposalIndex = ctx_.tx.getFieldU32(sfProposalIndex);
    auto proposalInfo = view().peek(keylet::proposal(proposalIndex));

    uint32_t expireTime = proposalInfo->getFieldU32(sfProposalExpire);
    LedgerIndex currentSeq = view().info().seq;

    if (view().info().closeTime <= expireTime) {
        // if not expired, set close ledger as current
        proposalInfo->setFieldU32(sfProposalCloseLedger, currentSeq);
        view().update(proposalInfo);
    }
    // else {
    //     // if already expired, find first ledger whoes close time is larger than expire time
    //     LedgerMaster& ledgerMaster = ctx_.app.getLedgerMaster();
    //     auto ledger = ledgerMaster.getLedgerByCloseTime(expireTime);
    //     if (!ledger) {
    //         return tecNO_ENTRY;
    //     }
    //     proposalInfo->setFieldU32(sfProposalCloseLedger, ledger->seq());
    // }
    
    // view().update(proposalInfo);

    return tesSUCCESS;
}

TER VoteProposal::preflight (PreflightContext const& ctx) {
    const auto &tx = ctx.tx;
    // sfProposalIndex
    if (!tx.isFieldPresent(sfProposalIndex) || ctx.tx.getFieldU32(sfProposalIndex) <= 0) {
        return temINVALID;
    }
    // sfProposalVote
    if (!tx.isFieldPresent(sfProposalVote)) {
        return temINVALID;
    }

    return tesSUCCESS;
}

TER VoteProposal::preclaim(PreclaimContext const& ctx) {
    const auto &tx = ctx.tx;
    uint32_t proposalIndex = tx.getFieldU32(sfProposalIndex);
    auto const k = keylet::proposal(proposalIndex);
    auto const proposalSle = ctx.view.read(k);
    // no proposal entry
    if (!proposalSle) {
        return tecNO_ENTRY;
    }
    // proposal has already been closed
    if (proposalSle->isFieldPresent(sfProposalCloseLedger) && proposalSle->getFieldU32(sfProposalCloseLedger) > 0) {
        return temINVALID;
    }
    // proposal has expired (the proposer has not closed yet)
    uint32_t expireTime = proposalSle->getFieldU32(sfProposalExpire);
    if (ctx.view.info().closeTime > expireTime) {
        return temINVALID;
    }
    // vote option should smaller than proposal option count
    if (tx.getFieldU8(sfProposalVote) > proposalSle->getFieldU8(sfProposalOptions)) {
        return temINVALID;
    }
    return tesSUCCESS;
}

TER VoteProposal::doApply() {
    // proposal to vote
    uint32_t proposalIndex = ctx_.tx.getFieldU32(sfProposalIndex);
    // option we vote for (0 for no vote)
    uint8_t proposalVote = ctx_.tx.getFieldU8(sfProposalVote);
    auto accountSle = view().peek(keylet::account(account_));
    // get votes we already have
    STArray originVotes;
    if (accountSle->isFieldPresent(sfProposalVotes)) {
        originVotes = accountSle->getFieldArray(sfProposalVotes);
    }
    STArray votes;
    bool found = false; // whether we already vote for this poposal
    // check all votes of this user
    for (auto &vote : originVotes) {
        uint32_t index = vote.getFieldU32(sfProposalIndex);
        auto proposalSle = view().peek(keylet::proposal(index));
        // found a closed ledger, clear this vote record
        if (proposalSle->isFieldPresent(sfProposalCloseLedger)) {
            uint32_t closeLedgerSeq = proposalSle->getFieldU32(sfProposalCloseLedger);
            if (closeLedgerSeq > 0) {
                // filter out votes whose proposal is closed
                continue;
            }
        }
        uint32_t expireTime = proposalSle->getFieldU32(sfProposalExpire);
        // found an expired ledger, clear this vote record
        if (expireTime < view().info().closeTime) {
            continue;
        }
        uint8_t voteOption = vote.getFieldU8(sfProposalVote);
        if (index == proposalIndex) {
            found = true;
            // dealing with proposal we are voting
            vote.setFieldU8(sfProposalVote, proposalVote);
            voteOption = proposalVote;
        }
        if (voteOption != 0) {
            // filter out abstained votes
            votes.push_back(vote);
        }
    }
    if (!found && proposalVote != 0) {
        // never vote this proposal before
        votes.push_back(STObject(sfVoteRecords));
        votes.back().setFieldU32(sfProposalIndex, proposalIndex);
        votes.back().setFieldU8(sfProposalVote, proposalVote);
    }
    // it seems delField has a bug when setField again after del, so just don't del when empty
    // if (votes.size() <= 0 && accountSle->isFieldPresent(sfProposalVotes)) {
    //     // no more active votes
    //     accountSle->delField(sfProposalVotes);
    // } else {
        accountSle->setFieldArray(sfProposalVotes, votes);
    // }
    view().update(accountSle);
    return tesSUCCESS;
}

}
