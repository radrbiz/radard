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

#ifndef RIPPLE_PROTOCOL_INDEXES_H_INCLUDED
#define RIPPLE_PROTOCOL_INDEXES_H_INCLUDED

#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Book.h>
#include <cstdint>

namespace ripple {

// get the index of the node that holds the last 256 ledgers
uint256
getLedgerHashIndex ();

// Get the index of the node that holds the set of 256 ledgers that includes
// this ledger's hash (or the first ledger after it if it's not a multiple
// of 256).
uint256
getLedgerHashIndex (std::uint32_t desiredLedgerIndex);

// get the index of the node that holds the enabled amendments
uint256
getLedgerAmendmentIndex ();

// get the index of the node that holds the fee schedule
uint256
getLedgerFeeIndex ();

uint256
getAccountRootIndex (AccountID const& account);

uint256
getGeneratorIndex (AccountID const& uGeneratorID);

uint256
getBookBase (Book const& book);

uint256
getOfferIndex (AccountID const& account, std::uint32_t uSequence);

uint256
getOwnerDirIndex (AccountID const& account);

uint256
getDirNodeIndex (uint256 const& uDirRoot, const std::uint64_t uNodeIndex);

uint256
getQualityIndex (uint256 const& uBase, const std::uint64_t uNodeDir = 0);

uint256
getQualityNext (uint256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality (uint256 const& uBase);

uint256
getTicketIndex (AccountID const& account, std::uint32_t uSequence);

uint256
getRippleStateIndex (AccountID const& a, AccountID const& b, Currency const& currency);

uint256
getRippleStateIndex (AccountID const& a, Issue const& issue);

uint256
getSignerListIndex (AccountID const& account);

//------------------------------------------------------------------------------

/* VFALCO TODO
    For each of these operators that take just the uin256 and
    only attach the LedgerEntryType, we can comment out that
    operator to see what breaks, and those call sites are
    candidates for having the Keylet either passed in a a
    parameter, or having a data member that stores the keylet.
*/

/** Keylet computation funclets. */
namespace keylet {

/** AccountID root */
struct account_t
{
    Keylet operator()(AccountID const& id) const;
};
static account_t const account {};

/** The amendment table */
struct amendments_t
{
    Keylet operator()() const;
};
static amendments_t const amendments {};

/** Any item that can be in an owner dir. */
Keylet child (uint256 const& key);

/** Skip list */
struct skip_t
{
    Keylet operator()() const;

    Keylet operator()(LedgerIndex ledger) const;
};
static skip_t const skip {};

/** The ledger fees */
struct fees_t
{
    // VFALCO This could maybe be constexpr
    Keylet operator()() const;
};
static fees_t const fees {};

/** The beginning of an order book */
struct book_t
{
    Keylet operator()(Book const& b) const;
};
static book_t const book {};

/** A trust line */
struct line_t
{
    Keylet operator()(AccountID const& id0,
        AccountID const& id1, Currency const& currency) const;

    Keylet operator()(AccountID const& id,
        Issue const& issue) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltRIPPLE_STATE, key };
    }
};
static line_t const line {};

/** An offer from an account */
struct offer_t
{
    Keylet operator()(AccountID const& id,
        std::uint32_t seq) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltOFFER, key };
    }
};
static offer_t const offer {};

/** The initial directory page for a specific quality */
struct quality_t
{
    Keylet operator()(Keylet const& k,
        std::uint64_t q) const;
};
static quality_t const quality {};

/** The directry for the next lower quality */
struct next_t
{
    Keylet operator()(Keylet const& k) const;
};
static next_t const next {};

/** A ticket belonging to an account */
struct ticket_t
{
    Keylet operator()(AccountID const& id,
        std::uint32_t seq) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltTICKET, key };
    }
};
static ticket_t const ticket {};

/** A SignerList */
struct signers_t
{
    Keylet operator()(AccountID const& id) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltSIGNER_LIST, key };
    }
};
static signers_t const signers {};

/** A Dividend object */
struct dividend_t
{
    Keylet operator()() const;
};
static dividend_t const dividend {};

/** A reference list belonging to an account */
struct refer_t
{
    Keylet operator()(AccountID const& id) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltREFER, key };
    }
};
static refer_t const refer {};

/** An asset belonging to an account */
struct asset_t
{
    Keylet operator()(AccountID const& id, Currency const& currency) const;

    Keylet operator()(Issue const& issue) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltASSET, key };
    }
};
static asset_t const asset {};

/** An asset state belonging to an account */
struct asset_state_t
{
    Keylet operator()(AccountID const& id0,
        AccountID const& id1, Currency const& currency) const;

    Keylet operator()(AccountID const& id,
        Issue const& issue) const;

    Keylet operator()(uint256 const& key) const
    {
        return { ltASSET_STATE, key };
    }
};
static asset_state_t const asset_state {};

//------------------------------------------------------------------------------

/** Any ledger entry */
Keylet unchecked(uint256 const& key);

/** The root page of an account's directory */
Keylet ownerDir (AccountID const& id);

/** A page in a directory */
/** @{ */
Keylet page (uint256 const& root, std::uint64_t index);
Keylet page (Keylet const& root, std::uint64_t index);
/** @} */

// DEPRECATED
inline
Keylet page (uint256 const& key)
{
    return { ltDIR_NODE, key };
}

/** A SuspendedPayment */
Keylet
susPay (AccountID const& source, std::uint32_t seq);

/** Ring object */
struct ring_t
{
    Keylet operator()(uint64 const& quantity, Issue const& issue, uint32_t index) const;
};
static ring_t const ring {};

/** proposal object */
struct proposal_t
{
    Keylet operator()(uint32_t const& index) const;
};
static proposal_t const proposal {};

/* ct index */
struct ct_t
{
    Keylet operator()(const unsigned char* commit, uint32_t ledger_index) const;
};
static ct_t const ct {};

} // keylet

uint256
getAccountReferIndex (AccountID const& account);

uint256
getLedgerDividendIndex ();

uint256
getAssetIndex (AccountID const& a, Currency const& currency);

uint256
getAssetIndex (Issue const& issue);

uint256
getAssetStateIndex (AccountID const& a, AccountID const& b, Currency const& currency);

uint256
getAssetStateIndex (AccountID const& a, Issue const& issue);

uint256
getRingIndex (uint64 const& quantity, Issue const& issue, uint32_t index);

uint256
getProposalIndex (uint32_t const& index);

uint256
getCTIndex(const unsigned char* commit, uint32_t ledger_index);

}

#endif
