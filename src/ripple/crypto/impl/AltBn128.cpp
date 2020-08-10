#include <ripple/crypto/AltBn128.h>
#include <ripple/protocol/Serializer.h>

namespace ripple{
namespace altbn128 {

using namespace openssl;

class Curve
{
public:
    EC_GROUP * group;
    Curve(){
        bn_ctx ctx;
        group = EC_GROUP_new_curve_GFp(P.get(), a.get(), b.get(), ctx.get());
        if (!group){
            Throw<std::runtime_error> ("The OpenSSL library new curve error.");
        }
        /*The affine co-ordinates for a point describe a point in terms of its x and y position.
        The functions EC_POINT_set_affine_coordinates_GFp and EC_POINT_set_affine_coordinates_GF2m set the x and y co-ordinates for the point p defined over the curve given in group.*/
        ec_point gen(group);
        if (!EC_POINT_set_affine_coordinates_GFp(group, gen.get(), Gx.get(), Gy.get(), ctx.get()))
            Throw<std::runtime_error> ("ec_group_set_generator() failed");

        if (!EC_POINT_is_on_curve(group, gen.get(), ctx.get())){
            Throw<std::runtime_error> ("ec generator is not on curve");
        }
        /*
        EC_GROUP_set_generator sets curve paramaters that must be agreed by all participants using the curve.
        These paramaters include the generator, the order and the cofactor.
        The generator is a well defined point on the curve chosen for cryptographic operations.
        Integers used for point multiplications will be between 0 and n-1 where n is the order.
        The order multipied by the cofactor gives the number of points on the curve.
        */
        if (!EC_GROUP_set_generator(group, gen.get(), N.get(), BN_value_one()))
            Throw<std::runtime_error> ("ec_group_set_generator() failed");
    }
    ~Curve(){
        EC_GROUP_free(group);
    }
};

EC_GROUP const* group(){
    static Curve curve;
    return curve.group;
}

bignum powmod(bignum const& a, bignum const& e, bignum const& m, bn_ctx& ctx){
    bignum result, y, b;
    BN_copy(result.get(), bnOne.get());
    BN_copy(y.get(), a.get());
    BN_copy(b.get(), e.get());
    while (bnZero < b)
    {
        if (BN_is_odd(b.get()))
            BN_mod_mul(result.get(), result.get(), y.get(), m.get(), ctx.get());
        BN_mod_sqr(y.get(), y.get(), m.get(), ctx.get());
        BN_rshift1(b.get(), b.get());
    }
    BN_mod(result.get(), result.get(), m.get(), ctx.get());
    return result;
}

bool onCurve(bignum const& x, bignum const& y, bn_ctx& ctx) 
{
    bignum beta;
    BN_mod_sqr(beta.get(), x.get(), P.get(), ctx.get());
    BN_mod_mul(beta.get(), beta.get(), x.get(), P.get(), ctx.get());
    BN_mod_add(beta.get(), beta.get(), bnThree.get(), P.get(), ctx.get());

    return onCurveBeta(beta, y, ctx);
}

bool onCurveBeta(bignum const& beta, bignum const& y, bn_ctx& ctx)
{
    bignum result;
    BN_mod_sqr(result.get(), y.get(), P.get(), ctx.get());
    return BN_cmp(beta.get(), result.get()) == 0;
}

void evalCurve(bignum const& x, bignum& y, bignum& beta, bn_ctx& ctx){
    BN_mod_sqr(beta.get(), x.get(), P.get(), ctx.get());
    BN_mod_mul(beta.get(), beta.get(), x.get(), P.get(), ctx.get());
    BN_mod_add(beta.get(), beta.get(), bnThree.get(), P.get(), ctx.get());

    y = powmod(beta, A, P, ctx);
    // require(beta == mulmod(y, y, P), "Invalid x for evalCurve");
}

ec_point scalarToPoint(uint256 x_){
    bignum x(x_), y, beta;
    bn_ctx ctx;
    BN_mod(x.get(), x.get(), N.get(), ctx.get());

    while (true) {
        evalCurve(x, y, beta, ctx);

        if (onCurveBeta(beta, y, ctx)) {
            bignum tmp;
            BN_sqr(tmp.get(), x.get(), ctx.get());
            BN_mod_add(tmp.get(), tmp.get(), bnZero.get(), P.get(), ctx.get());
            BN_mul(tmp.get(), tmp.get(), x.get(), ctx.get());
            BN_mod_add(tmp.get(), tmp.get(), bnThree.get(), P.get(), ctx.get());
            return set_coordinates(altbn128::group(), x, y);
        }

        BN_mod_add(x.get(), x.get(), bnOne.get(), N.get(), ctx.get());
    }
}

uint256
sha256_s(std::string s){
    sha256_hasher h;
    h(s.data(), s.size());
    std::array<std::uint8_t, 32> arr = static_cast<typename sha256_hasher::result_type>(h);
    uint256 ret;
    std::memcpy(ret.data(), arr.data(), arr.size());
    return ret;
}

bool
ringVerify(std::string msg,
    uint256 c0, STVector256 keyImage,
    STVector256 sig, STArray publicKeys, beast::Journal j)
{

    using namespace openssl;
    bignum bn_c0(c0);
    bignum bn_c(c0);
    std::string hexPrefix = "0x";
    //step 1
    Serializer s;
    for(auto const pk : publicKeys){
        STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
        s.add256(keyPair[0]);
        s.add256(keyPair[1]);
    }
    std::string str_pks = hexPrefix + s.getHex();
    uint256 h = sha256_s(str_pks);
    JLOG(j.debug) << "str_pks:" << str_pks;
    JLOG(j.debug) << "sha256(pks):" << h;
    
    //mod n
    ec_point pt_h = scalarToPoint(h);
    bignum hx,hy;
    bn_ctx bc;
    EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pt_h.get(), hx.get(), hy.get(), bc.get());

    char *ch1 = BN_bn2hex(hx.get());
    char *ch2 = BN_bn2hex(hy.get());
    JLOG(j.debug) << "hx:" << ch1 << ",hy:" << ch2;
    OPENSSL_free(ch1);
    OPENSSL_free(ch2);
    // step 2
    char* z1, *z2;
    Serializer s2;
    for(auto const pk : publicKeys){
        STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
        s2.add256(keyPair[0]);
        s2.add256(keyPair[1]);
    }
    s2.add256(keyImage[0]);
    s2.add256(keyImage[1]);
    s2.addRaw(msg.data(), msg.size());
    for(int i=0; i<publicKeys.size(); i++){
        bignum bn_sig(sig[i]);
        uint256 pkx = publicKeys[i].getFieldV256(sfPublicKeyPair)[0];
        uint256 pky = publicKeys[i].getFieldV256(sfPublicKeyPair)[1];
        ec_point pt_pk = set_coordinates(altbn128::group(), bignum(pkx), bignum(pky));
        // TODOcheck point on curve
        ec_point m1 = multiply(altbn128::group(), bn_sig, bc); // G * s
char *char_m1 = EC_POINT_point2hex(altbn128::group(), m1.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "m1=G * pk result:" << char_m1;
OPENSSL_free(char_m1);

        ec_point m2 = multiply2(altbn128::group(), pt_pk, bn_c, bc); // pk * c
char *char_pk = EC_POINT_point2hex(altbn128::group(), pt_pk.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "pk:" << char_pk;
OPENSSL_free(char_pk);
ch1 = BN_bn2hex(bn_c.get());
JLOG(j.debug) << "c:" << ch1;
OPENSSL_free(ch1);
char *char_m2 = EC_POINT_point2hex(altbn128::group(), m2.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "m2=pk * c result:" << char_m2;
OPENSSL_free(char_m2);

        ec_point p1 = add(altbn128::group(), m1, m2, bc);
        char *char_p1 = EC_POINT_point2hex(altbn128::group(), p1.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "p1=m1 + m2 result:" << char_p1;
//OPENSSL_free(char_p1);

        ec_point m3 = multiply2(altbn128::group(), pt_h, bn_sig, bc); // h * s
char *char_m3 = EC_POINT_point2hex(altbn128::group(), m3.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "m3=h * sig result:" << char_m3;
OPENSSL_free(char_m3);

        //conver img to point
        ec_point pt_img = set_coordinates(altbn128::group(), bignum(keyImage[0]), bignum(keyImage[1]));
        ec_point m4 = multiply2(altbn128::group(), pt_img, bn_c, bc); // key * c
char *char_m4 = EC_POINT_point2hex(altbn128::group(), m4.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "m4=keyimg * c result:" << char_m4;
OPENSSL_free(char_m4);

        ec_point p2 = add(altbn128::group(), m3, m4, bc);
        char *char_p2 = EC_POINT_point2hex(altbn128::group(), p2.get(), (point_conversion_form_t)4, bc.get());
JLOG(j.debug) << "p2=m3 + m4 result:" << char_p2;
//OPENSSL_free(char_p2);
        // char ?
        z1 = char_p1+2; //jump compress type
        z2 = char_p2+2;

        if(i < publicKeys.size() -1){
            Serializer s3;
            s3.addRaw(z1, strlen(z1));
            s3.addRaw(z2, strlen(z2));
            sha256_hasher hasher2;
            std::string str_s3 = hexPrefix + s2.getHex() + s3.getString();
            boost::to_lower(str_s3);
            uint256 u256_hash_ret = sha256_s(str_s3);
    JLOG(j.debug) << "str_s2:" << s2.getHex();
    JLOG(j.debug) << "str_s3:" << str_s3;
    JLOG(j.debug) << "sha256(s3):" << u256_hash_ret;

            //mod
            bignum bn_hash(u256_hash_ret);
            BN_mod(bn_c.get(), bn_hash.get(), N.get(), bc.get());
    ch1 = BN_bn2hex(bn_c.get());
    JLOG(j.debug) << "c:" << ch1;
    OPENSSL_free(ch1);
        }
    }
    if(z1 == nullptr || z2 == nullptr){
        return false;
    }
    Serializer s4;
    s4.addRaw(z1, strlen(z1));
    s4.addRaw(z2, strlen(z2));
    std::string str_s4 = hexPrefix + s2.getHex() + s4.getString();
    boost::to_lower(str_s4);
    uint256 u256_c = sha256_s(str_s4);
    //mod
    bignum bn_hash_2(u256_c);
    BN_mod(bn_c.get(), bn_hash_2.get(), N.get(), bc.get());

    char* ch_c = BN_bn2hex(bn_c.get());
    JLOG(j.debug) << "c0:" << c0 << ",verify:" << ch_c;
    OPENSSL_free(ch_c);
    return BN_cmp(bn_c0.get(), bn_c.get()) == 0;
}

} //openssl
}
