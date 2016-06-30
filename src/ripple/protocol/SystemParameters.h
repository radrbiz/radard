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

#ifndef RIPPLE_PROTOCOL_SYSTEMPARAMETERS_H_INCLUDED
#define RIPPLE_PROTOCOL_SYSTEMPARAMETERS_H_INCLUDED

#include <cstdint>
#include <string>

namespace ripple {

// Various protocol and system specific constant globals.

/* The name of the system. */
static inline
std::string const&
systemName ()
{
    static std::string const name = "radar";
    return name;
}

#define SYSTEM_NAMESPACE "Rd"

static constexpr auto systemNamespace = SYSTEM_NAMESPACE;

static
std::uint64_t const
SYSTEM_CURRENCY_GIFT = 2;

static
std::uint64_t const
SYSTEM_CURRENCY_USERS = 1000000;

/** Number of drops per 1 XRP */
static
std::uint64_t const
SYSTEM_CURRENCY_PARTS = 1000000;      // 10^SYSTEM_CURRENCY_PRECISION

static
std::uint64_t const
SYSTEM_CURRENCY_START = SYSTEM_CURRENCY_GIFT*SYSTEM_CURRENCY_USERS*SYSTEM_CURRENCY_PARTS;

static
std::uint64_t const
SYSTEM_CURRENCY_GIFT_VBC = 10;

static
std::uint64_t const
SYSTEM_CURRENCY_USERS_VBC = 1000000;

static
std::uint64_t const
SYSTEM_CURRENCY_PARTS_VBC = 1000000;      // 10^SYSTEM_CURRENCY_PRECISION

static
std::uint64_t const
MIN_VSPD_TO_GET_FEE_SHARE = 10000000000;

/** Number of drops in the genesis account. */
static
std::uint64_t const
SYSTEM_CURRENCY_START_VBC = SYSTEM_CURRENCY_GIFT_VBC*SYSTEM_CURRENCY_USERS_VBC*SYSTEM_CURRENCY_PARTS_VBC;

static
std::uint64_t const
VBC_DIVIDEND_MIN = 1000;

static
std::uint64_t const
VBC_DIVIDEND_PERIOD_1 = 473904000ull; // 2015-1-7

static
std::uint64_t const
VBC_DIVIDEND_PERIOD_2 = 568598400ull; // 2018-1-7

static
std::uint64_t const
VBC_DIVIDEND_PERIOD_3 = 663292800ull; // 2021-1-7

static
std::uint64_t const
VBC_INCREASE_RATE_1 = 3;

static
std::uint64_t const
VBC_INCREASE_RATE_1_PARTS= 1000;

static
std::uint64_t const
VBC_INCREASE_RATE_2 = 15;

static
std::uint64_t const
VBC_INCREASE_RATE_2_PARTS = 10000;

static
std::uint64_t const
VBC_INCREASE_RATE_3 = 1;

static
std::uint64_t const
VBC_INCREASE_RATE_3_PARTS = 1000;

static
std::uint64_t const
VBC_INCREASE_RATE_4 = 3;

static
std::uint64_t const
VBC_INCREASE_RATE_4_PARTS = 10000;

static
std::uint64_t const
VBC_INCREASE_MAX = 1000000000000000ull;

static
std::uint64_t const
VRP_INCREASE_RATE = 1;

static
std::uint64_t const
VRP_INCREASE_RATE_PARTS = 1000;

static
std::uint64_t const
VRP_INCREASE_MAX = 10000000000000ull;

/* The currency code for the native currency. */
static inline
std::string const&
systemCurrencyCode ()
{
    static std::string const code = "VRP";
    return code;
}
    
/* The currency code for the native currency. */
static inline
std::string const&
systemCurrencyCodeVBC ()
{
    static std::string const code = "VBC";
    return code;
}
} // ripple

#endif
