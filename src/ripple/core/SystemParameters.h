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

#ifndef RIPPLE_CORE_SYSTEMPARAMETERS_H_INCLUDED
#define RIPPLE_CORE_SYSTEMPARAMETERS_H_INCLUDED

namespace ripple {

/** Protocol specific constant globals. */
// VFALCO NOTE use these from now on instead of the macros!
class RippleSystem
{
public:
    static inline char const* getSystemName ()
    {
        return "radar";
    }

    static char const* getCurrencyCode ()
    {
        return "VRP";
    }

    static char const* getCurrencyCodeRipple ()
    {
        return "VRR";
    }
	
	static char const* getCurrencyCodeVBC ()
    {
        return "VBC";
    }

    static char const* getCurrencyCodeRippleVBC ()
    {
        return "VBB";
    }
	

    static int getCurrencyPrecision ()
    {
        return 6;
    }
};

// VFALCO TODO I would love to replace these macros with the language
//         constructs above. The problem is the way they are used at
//         the point of call, i.e. "User-agent:" SYSTEM_NAME
//         It will be necessary to rewrite some of them to use string streams.
//
#define SYSTEM_NAME                 "radar"
#define SYSTEM_CURRENCY_PRECISION   6

// VFALCO TODO Replace with C++11 long long constants
// VFALCO NOTE Apparently these are used elsewhere. Make them constants in the config
//             or in the Application
//
#define SYSTEM_CURRENCY_GIFT        2ull
#define SYSTEM_CURRENCY_USERS       1000000ull
#define SYSTEM_CURRENCY_PARTS       1000000ull      // 10^SYSTEM_CURRENCY_PRECISION
#define SYSTEM_CURRENCY_START       (SYSTEM_CURRENCY_GIFT*SYSTEM_CURRENCY_USERS*SYSTEM_CURRENCY_PARTS)
#define SYSTEM_CURRENCY_GIFT_VBC        10ull
#define SYSTEM_CURRENCY_USERS_VBC       1000000ull
#define SYSTEM_CURRENCY_PARTS_VBC       1000000ull      // 10^SYSTEM_CURRENCY_PRECISION
#define SYSTEM_CURRENCY_START_VBC       (SYSTEM_CURRENCY_GIFT_VBC*SYSTEM_CURRENCY_USERS_VBC*SYSTEM_CURRENCY_PARTS_VBC)

#define VBC_DIVIDEND_MIN            1000ull
#define VBC_DIVIDEND_PERIOD_1       473904000ull // 2015-1-7
#define VBC_DIVIDEND_PERIOD_2       568598400ull // 2018-1-7
#define VBC_DIVIDEND_PERIOD_3       663292800ull // 2021-1-7
#define VBC_INCREASE_RATE_1         3
#define VBC_INCREASE_RATE_1_PARTS   1000
#define VBC_INCREASE_RATE_2         15
#define VBC_INCREASE_RATE_2_PARTS   10000
#define VBC_INCREASE_RATE_3         1
#define VBC_INCREASE_RATE_3_PARTS   1000
#define VBC_INCREASE_RATE_4         3
#define VBC_INCREASE_RATE_4_PARTS   10000
#define VBC_INCREASE_MAX            1000000000000000ull
#define VRP_INCREASE_RATE           1
#define VRP_INCREASE_RATE_PARTS     1000
#define VRP_INCREASE_MAX            10000000000000ull

} // ripple

#endif
