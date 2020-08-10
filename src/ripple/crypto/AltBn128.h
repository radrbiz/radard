
#ifndef RIPPLE_CRYPTO_ALTBN128_H_INCLUDED 
#define RIPPLE_CRYPTO_ALTBN128_H_INCLUDED

#include <beast/utility/Journal.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/Log.h>
#include <ripple/crypto/impl/openssl.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STVector256.h>

namespace ripple
{
namespace altbn128
{
EC_GROUP const* group();

openssl::bignum const a(uint256(0));
openssl::bignum const b(uint256(3));
openssl::bignum const Gx(uint256(1));
openssl::bignum const Gy(uint256(2));
// Number of elements in the field (often called `q`)
// n = n(u) = 36u^4 + 36u^3 + 18u^2 + 6u + 1
openssl::bignum const N(from_hex_text<uint256>("0x30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001"));
// p = p(u) = 36u^4 + 36u^3 + 24u^2 + 6u + 1
// Field Order
openssl::bignum const P(from_hex_text<uint256>("0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47"));
// (p+1) / 4
openssl::bignum const A (from_hex_text<uint256> ("0xc19139cb84c680a6e14116da060561765e05aa45a1c72a34f082305b61f3f52"));
// Convinience Numbers
openssl::bignum const bnZero(uint256(0));
openssl::bignum const bnOne(uint256(1));
openssl::bignum const bnTwo(uint256(2));
openssl::bignum const bnThree(uint256(3));

// Adapted from boost::multiprecision::powm at boost/multiprecision/integer.hpp:83
// @sa https://en.wikipedia.org/wiki/Modular_exponentiation#Right-to-left_binary_method
openssl::bignum powmod(openssl::bignum const& a, openssl::bignum const& e, openssl::bignum const& m, openssl::bn_ctx& ctx);

/*
   Checks if the points x, y exists on alt_bn_128 curve
*/
bool onCurve(openssl::bignum const& x, openssl::bignum const& y, openssl::bn_ctx& ctx);

bool onCurveBeta(openssl::bignum const& beta, openssl::bignum const& y, openssl::bn_ctx& ctx);

void evalCurve(openssl::bignum const& x, openssl::bignum& y, openssl::bignum& beta, openssl::bn_ctx& ctx);

openssl::ec_point scalarToPoint(uint256 x_);

uint256
sha256_s(std::string s);

bool
ringVerify(std::string msg,
    uint256 c0, STVector256 keyImage,
    STVector256 sig, STArray publicKeys, beast::Journal j);

} // altbn128
} // ripple

#endif
