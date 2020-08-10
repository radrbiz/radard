
#include <BeastConfig.h>
#include <ripple/protocol/JsonFields.h>     // jss:: definitions
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>
#include <ripple/protocol/digest.h>
#include <ripple/app/tx/impl/RingWithdraw.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/crypto/impl/openssl.h>
#include <ripple/crypto/AltBn128.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

    using bignum = openssl::bignum;
    using bn_ctx = openssl::bn_ctx;
    using ec_point = openssl::ec_point;
    using namespace altbn128;
    
class Ring_test : public beast::unit_test::suite
{

public:
    void test_hash_value(){
        testcase ("sha256 value");
        std::string msg = "1";

        // sha256(openssl) seems equal to js(crypto) lib
        sha256_hasher h;
        h(msg.data(), msg.size());
        std::array<std::uint8_t, 32> r1 = static_cast<typename sha256_hasher::result_type>(h);
        uint256 u256_result;
        std::memcpy(u256_result.data(), r1.data(), r1.size());
        log<< "sha256(\"1\"):" << u256_result;
        expect(u256_result == from_hex_text<uint256>("6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b"), "sha256(\"1\") result mismatch");
    }

    void test_h1_h2(){
        testcase ("h1 and h2");
        std::string msg = "0x1";
        sha256_hasher h;
        h(msg.data(), msg.size());
        std::array<std::uint8_t, 32> r1 = static_cast<typename sha256_hasher::result_type>(h);
        uint256 u256_result;
        std::memcpy(u256_result.data(), r1.data(), r1.size());
        log<< "sha256(\"0x1\"):" << u256_result;
        expect(u256_result == from_hex_text<uint256>("a03279d346f550999209e77159c3bc678a1e3c9455e2815280078b65d671dfe3"), "sha256(\"0x1\") result mismatch");
        
        bignum r(u256_result);
        char* ch = BN_bn2hex(r.get());
        log<< "sha256(\"0x1\") as bignum:" << ch;
        OPENSSL_free(ch);
        
        bn_ctx ctx;
        BN_mod(r.get(), r.get(), N.get(), ctx.get());
        ch = BN_bn2hex(r.get());
        log<< "h1(\"0x1\") as bignum:" << ch;
        OPENSSL_free(ch);
        bignum r_expect(from_hex_text<uint256>("f058e7aa360701c6919164dd53fb350118283bae8b62f9eb461aaaa0671dfe0"));
        expect(BN_cmp(r.get(), r_expect.get()) == 0, "h1 mismatch");
        
        const auto h2_r = scalarToPoint(u256_result);
        bignum x,y;
        EC_POINT_get_affine_coordinates_GFp(altbn128::group(), h2_r.get(), x.get(), y.get(), ctx.get());

        ch = BN_bn2hex(x.get());
        char* ch2 = BN_bn2hex(y.get());
        log << "h2(h1(\"0x1\")) x:" << ch << " y:" << ch2;
        OPENSSL_free(ch);
        OPENSSL_free(ch2);
        bignum y_expect(from_hex_text<uint256>("2E059DDD00D0F476588572E7E9DF5B836606FBAB18F8B4522243B46C02329D64"));
        expect(BN_cmp(y.get(), y_expect.get()) == 0, "h1 mismatch");
    }

    void test_ecc_mul_g(){
        testcase ("test ecc s mul g");
        bn_ctx bc;

        bignum s(from_hex_text<uint256>("1172a7084c95e4c3655602cc810042b28cb968f5dbe7577357990ef0fbf4735"));
        ec_point m12 = multiply(altbn128::group(), s, bc); // g * s
        char *char_m12 = EC_POINT_point2hex(altbn128::group(), m12.get(), (point_conversion_form_t)2, bc.get());
        log << "char_m12:" << char_m12;

        bignum expX(from_hex_text<uint256>("e3e6465fd13407477fd2dcb084ad45ed5f14fec900b745e94ebcece9a4c6a1b"));
        bignum expY(from_hex_text<uint256>("2caf4602eae1c1a042f1999a8d5954c73e50a1c96fb823dff45cc4139f0f9aae"));
        ec_point exp = set_coordinates(altbn128::group(), expX, expY);

        char *char_exp = EC_POINT_point2hex(altbn128::group(), exp.get(), (point_conversion_form_t)2, bc.get());
        log << "char_exp:" << char_exp;
        expect(EC_POINT_cmp(altbn128::group(), m12.get(), exp.get(), bc.get()) == 0, "g * s calc incorrect");
    }

    void test_ecc_mul(){
        testcase ("test ecc multipy2");
        bn_ctx bc;

        bignum s(from_hex_text<uint256>("18f602495784694262d165d60c708be3d253586237d1dd337e6aed38fbeb5b08"));
        bignum bn_qx(from_hex_text<uint256>("2746772212517b82f02a301fcd108b55eabfec33a67eebfad7847a43defa8eff"));
        bignum bn_qy(from_hex_text<uint256>("1009b804c6a4841e17e9b7aa77a3c6e696ea73f8c0fe608202eac73366d57ac6"));
        ec_point pt_q = set_coordinates(altbn128::group(), bn_qx, bn_qy);
        char *char_q = EC_POINT_point2hex(altbn128::group(), pt_q.get(), (point_conversion_form_t)4, bc.get());
        log << "ecc q:" << char_q;

        ec_point pt_qs = multiply2(altbn128::group(), pt_q, s, bc); // pt_g * s
        //ec_point pt_qs(altbn128::group());
        //EC_POINT_mul(altbn128::group(), pt_qs.get(), nullptr, pt_q.get(), s.get(), bc.get());
        if (!EC_POINT_is_on_curve(altbn128::group(), pt_q.get(), bc.get())){
            Throw<std::runtime_error> ("q is not on curve");
        }

        char *char_qs = EC_POINT_point2hex(altbn128::group(), pt_qs.get(), (point_conversion_form_t)4, bc.get());
        log << "multply2 result:" << char_qs;

        bignum expX(from_hex_text<uint256>("21c195bb2d8ef237216ad5213d6a1c8a4752ebbfb8a6e6b82d7e33448fe0eca"));
        bignum expY(from_hex_text<uint256>("2ae64824ba2f4cd11ed3581c71a382534f8455d21f126ddb5c26f8cd031b0525"));
        ec_point pt_exp = set_coordinates(altbn128::group(), expX, expY);

        expect(EC_POINT_cmp(altbn128::group(), pt_qs.get(), pt_exp.get(), bc.get()) == 0, "q * s calc incorrect");
    }

    void test_ecc_add(){
        testcase("test ecc add");
        bn_ctx bc;

        bignum bn_p1x(from_hex_text<uint256>("80299577fb66721b92c1f9ec8cde9987af184abe267f734ee384a373a63f465"));
        bignum bn_p1y(from_hex_text<uint256>("aa3770fe52df27369426a75f7e1b0421d4f4fcc18f572c8ec1b69624b05c283"));
        bignum bn_p2x(from_hex_text<uint256>("14b6a1cacc53cec03dc6044915fcfcdc8da5d7f5ee2087f804962d98b9093270"));
        bignum bn_p2y(from_hex_text<uint256>("7129f3b89c633e6e09fd4756301a36d7874de7c1cdc945c2e9368b8114349b6"));
        ec_point pt_p1 = set_coordinates(altbn128::group(), bn_p1x, bn_p1y);
        ec_point pt_p2 = set_coordinates(altbn128::group(), bn_p2x, bn_p2y);

        ec_point pt_add = add(altbn128::group(), pt_p1, pt_p2, bc); // p1 + p2
        char *char_add = EC_POINT_point2hex(altbn128::group(), pt_add.get(), (point_conversion_form_t)4, bc.get());
        log << "add result:" << char_add;

        bignum expX(from_hex_text<uint256>("2a23a9dee33004766661bf5ebce2f280ecb8d3db0465ac7787d1cc2f4158e6f5"));
        bignum expY(from_hex_text<uint256>("17f96fb86806d684f3ef1df7211f61482273a9f9b44b7c2c42447d24367001d"));
        ec_point pt_exp = set_coordinates(altbn128::group(), expX, expY);

        expect(EC_POINT_cmp(altbn128::group(), pt_add.get(), pt_exp.get(), bc.get()) == 0, "a + b calc incorrect");
    }

    void test_verify(){
        uint256 c0, k0, k1, s0, s1;
        c0.SetHex("231ad96ac307efe1d69639fc6b834eed917a77b3f700bde9a7a0192488375512");
        STVector256 keyImage;
        k0.SetHex("4d08ba2afba7642aa4a40e1df7860063c02aedba83934e8809ab37c3d59daf0");
        k1.SetHex("2404c35d3d769058c7c877e208471fb05b0f426b9c0268fb4a487ba8fb9f624f");
        keyImage.push_back(k0);
        keyImage.push_back(k1);
        STVector256 sig;
        s0.SetHex("2e9677acce82f47748cc256514b4818b182c79b0cab910fa6316f0a93cbbc5af");
        s1.SetHex("1073346c84703a05bfa2e7ccd8f033ce02085a97fa2a89e6662c51871bb7e499");
        sig.push_back(s0);
        sig.push_back(s1);

        STArray publicKeys (sfPublicKeys);
        publicKeys.push_back(STObject(sfRingHolder));
        STObject& obj = publicKeys.back();
        STVector256 pk0, pk1;
        uint256 pkx, pky;
        pkx.SetHex("1e2154a11aeca6d2fd7abeabe29a014bf75abc03af32803a053acf5e887f1f49");
        pky.SetHex("1ed8fc9c8d908c67c1e36d8f8827204c7edc7bae943fb7b47ba39a699b2bb167");
        pk0.push_back(pkx);
        pk0.push_back(pky);
        obj.setFieldV256(sfPublicKeyPair, pk0);

        publicKeys.push_back(STObject(sfRingHolder));
        STObject& obj2 = publicKeys.back();
        pkx.SetHex("207452c121248338bd9d84118c37d74937e7d1246950251a3ed3c8d12d5fa9c0");
        pky.SetHex("81989c43820a65866b769cb1721bdf98bcf49eb6ec1bd295099fbc98d336f2f");
        pk1.push_back(pkx);
        pk1.push_back(pky);
        obj2.setFieldV256(sfPublicKeyPair, pk1);

        std::string msg = "test";
        expect(altbn128::ringVerify(msg, c0, keyImage, sig, publicKeys, beast::Journal{}), "verfiy...");
    }

    std::tuple<uint256, std::vector<uint256>, STVector256> ring_sign(const std::string &message, STArray publicKeys, int index, uint256 privateKey)
    {
        auto pt2uin256pair = [](const ec_point &pt)->std::pair<uint256, uint256>{
            bn_ctx ctx;
            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pt.get(), xBN.get(), yBN.get(), ctx.get());
            return std::make_pair(uint256_from_bignum_clear(xBN), uint256_from_bignum_clear(yBN));
        };

        auto publicKeysToHexString = [](const STArray &publicKeys)->std::string  {
            Serializer ser;
            for(auto const pk : publicKeys){
                STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
                ser.add256(keyPair[0]);
                ser.add256(keyPair[1]);
            }
            std::string serHex = "0x" + ser.getHex();         
            return serHex;
        };

        auto debugPointOutput = [](const ec_point &pt, std::string prefix)->std::string {
            bn_ctx ctx;
            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pt.get(), xBN.get(), yBN.get(), ctx.get());
            return std::string(prefix 
                + "\n\tx=0x" 
                + std::string(BN_bn2hex(xBN.get())) 
                + ",\n\ty=0x" 
                + std::string(BN_bn2hex(yBN.get()))
            );
        };

        auto sha256hash = [](const std::string &data)->uint256{
            sha256_hasher hasher;
            hasher(data.data(), data.size());
            std::array<std::uint8_t, 32> hRes = static_cast<typename sha256_hasher::result_type>(hasher);
            uint256 h;
            std::memcpy(h.data(), hRes.data(), hRes.size());
            return h;       
        };

        auto uint256tostring = [](const uint256& n)->std::string {
            bignum N(n);
            return std::string(BN_bn2hex(N.get()));
        };

        auto h1hash = [sha256hash](const std::string &d)->uint256 {
            // prefix 0x
            std::string data = "0x" + d;
            // to lower case
            std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { 
                return std::tolower(c); 
            });
            uint256 h1 = sha256hash(data);
            bignum h1BN = bignum(h1);
            bn_ctx ctx;
            BN_mod(h1BN.get(), h1BN.get(), N.get(), ctx.get());
            return uint256_from_bignum_clear(h1BN);
        };

        int keyCount = publicKeys.size();
        std::vector<uint256> c(keyCount);
        std::vector<uint256> s(keyCount);
        bn_ctx ctx;

        // STEP 1
        std::string pksHex = publicKeysToHexString(publicKeys);
        log << "step1 data: " << pksHex;
        uint256 hBin = sha256hash(pksHex);

        ec_point h = scalarToPoint(hBin);
        log << debugPointOutput(h, "h:");

        ec_point yTildePt = multiply2(altbn128::group(), h, bignum(privateKey), ctx); // h * privateKey
        log << debugPointOutput(yTildePt, "yTildePt:");

        // STEP 2
            // yTilde
        // bignum yTildeBN = point2bn(altbn128::group(), yTildePt);
        // uint256 yTilde = uint256_from_bignum_clear(yTildeBN);
            // Gu
        // bignum u = bignum::rand(128);
        bignum u(from_hex_text<uint256>("1172a7084c95e4c3655602cc810042b28cb968f5dbe7577357990ef0fbf4735"));
        ec_point GuPt = multiply(altbn128::group(), u, ctx);        // bn128.ecMul(G, u)
        log << debugPointOutput(GuPt, "GuPt:");
        
            // hu
        ec_point huPt = multiply2(altbn128::group(), h, u, ctx);    // bn128.ecMul(h, u)
        log << debugPointOutput(huPt, "huPt:");
            // c[idx+1] = h1(...)
        Serializer ser;
        for(auto const pk : publicKeys){
            STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
            ser.add256(keyPair[0]);
            ser.add256(keyPair[1]);
        }
        auto yTildePtPair = pt2uin256pair(yTildePt);
        ser.add256(yTildePtPair.first);
        ser.add256(yTildePtPair.second);
        ser.addRaw(message.data(), message.size());
        auto GuPtPair = pt2uin256pair(GuPt);
        ser.add256(GuPtPair.first);
        ser.add256(GuPtPair.second);
        auto huPtPair = pt2uin256pair(huPt);
        ser.add256(huPtPair.first);
        ser.add256(huPtPair.second);
        uint256 hStep2 = h1hash(ser.getHex());

        log << "c[(index+1) % keyCount]=" << uint256tostring(hStep2);

        c[(index+1) % keyCount] = hStep2;

        
        // STEP 3
        std::deque<int> indice;
        for (int i = index+1; i < keyCount; i++) {
            indice.push_back(i);
        }
        for (int i = 0; i < index; i++) {
            indice.push_back(i);
        }
        {
                std::string idxStr = "";
                for (int i : indice) {
                    idxStr += to_string(i);
                    idxStr += ", ";
                }
                log << idxStr;
        }
        
        for (int i : indice) {
            // s[i]
            // bignum sBN = bignum::rand(128);
            bignum sBN(from_hex_text<uint256>("4d08ba2afba7642aa4a40e1df7860063c02aedba83934e8809ab37c3d59daf0"));
            // s[i] = uint256_from_bignum_clear(sBN);
            
            bignum cBN(c[i]);

            log << "sBN: " << uint256tostring(s[i]);
            // z1
                // -- bn128.ecMul(G, s[i])
            ec_point GsPt = multiply(altbn128::group(), sBN, ctx);
            log << debugPointOutput(GsPt, "GsPt:");
                // -- bn128.ecMul(publicKeys[i], c[i])
            uint256 pkx = publicKeys[i].getFieldV256(sfPublicKeyPair)[0];
            uint256 pky = publicKeys[i].getFieldV256(sfPublicKeyPair)[1];
            ec_point pkPt = set_coordinates(altbn128::group(), bignum(pkx), bignum(pky));
            ec_point pkcPt = multiply2(altbn128::group(), pkPt, cBN, ctx);
            log << uint256tostring(c[i]);
            log << debugPointOutput(pkPt, "pkPt:");
            log << debugPointOutput(pkcPt, "pkcPt:");
                // -- z1 = bn128.ecAdd(bn128.ecMul(G, s[i]), bn128.ecMul(publicKeys[i], c[i]))
            ec_point z1Pt = add(altbn128::group(), GsPt, pkcPt, ctx);
            log << debugPointOutput(z1Pt, "z1Pt:");
            

            // z2
                // -- bn128.ecMul(h, s[i])
            ec_point hsPt = multiply2(altbn128::group(), h, sBN, ctx);
                // -- bn128.ecMul(yTilde, c[i])
            ec_point hTcPt = multiply2(altbn128::group(), yTildePt, cBN, ctx);
                // z2 = bn128.ecAdd(bn128.ecMul(h, s[i]), bn128.ecMul(yTilde, c[i]))
            ec_point z2Pt = add(altbn128::group(), hsPt, hTcPt, ctx);
            log << debugPointOutput(z2Pt, "z2Pt:");

            // // c[(i + 1) % keyCount] = h1(...)
            // c[(i + 1) % keyCount] = sha256(
            //     publicKeys,
            //     yTilde,
            //     message,
            //     z1,
            //     z2
            // );
            Serializer s3;
            for(auto const pk : publicKeys){
                STVector256 keyPair = pk.getFieldV256(sfPublicKeyPair);
                s3.add256(keyPair[0]);
                s3.add256(keyPair[1]);
            }
            auto yTildePtPair = pt2uin256pair(yTildePt);
            s3.add256(yTildePtPair.first);
            s3.add256(yTildePtPair.second);
            s3.addRaw(message.data(), message.size());
            
            auto z1PtPair = pt2uin256pair(z1Pt);
            s3.add256(z1PtPair.first);
            s3.add256(z1PtPair.second);
            auto z2PtPair = pt2uin256pair(z2Pt);
            s3.add256(z2PtPair.first);
            s3.add256(z2PtPair.second);

            uint256 hStep3 = h1hash(s3.getHex());
            log << "c[(i + 1) % keyCount]=" << uint256tostring(hStep3);
            c[(i + 1) % keyCount] = hStep3;

            s[i] = uint256_from_bignum_clear(sBN);
        }

        log << "======";

        //uint256 zero(from_hex_text<uint256>("0"));

        // STEP 4
        bignum privateKeyBN(privateKey);
        log << "privateKey: " << uint256tostring(privateKey);
        bignum cBN(c[index]);
        log << "c[index]: " << uint256tostring(c[index]);
        bignum pkc;
        BN_mul(pkc.get(), privateKeyBN.get(), cBN.get(), ctx.get());
        log << "pkc: ";
        bignum sci;
        BN_mod(sci.get(), pkc.get(), N.get(), ctx.get());
        log << "sci: " << BN_bn2hex(sci.get());

        bignum usci;
        BN_sub(usci.get(), u.get(), sci.get());
        log << "usci: " << BN_bn2hex(usci.get());

        bignum usci1;
        bignum zero(from_hex_text<uint256>("0"));
        if (BN_cmp(usci.get(), zero.get()) < 0) {
            BN_add(usci1.get(), usci.get(), N.get());
        } else {
            BN_copy(usci1.get(), usci.get());
        }
        log << "usci1: " << BN_bn2hex(usci1.get());

        bignum sBN;
        BN_mod(sBN.get(), usci1.get(), N.get(), ctx.get());
        log << "sBN: " << BN_bn2hex(sBN.get());

        s[index] = uint256_from_bignum_clear(sBN);
        log << "s[index]: " << uint256tostring(s[index]);

        bignum c0BN(c[0]);
        uint256 c0 = uint256_from_bignum_clear(c0BN);

        // auto yTildePtPair = pt2uin256pair(yTildePt);
        STVector256 yTilde;
        yTilde.push_back(yTildePtPair.first);
        yTilde.push_back(yTildePtPair.second);
        return std::make_tuple(c0, s, yTilde);
    }

    void test_sign()
    {
        testcase ("sign ring tx");
        
        bn_ctx ctx;

        std::string message = "ETH for you and everyone!";
        log << "message: " << message;

        uint256 sk1, sk2, sk3, sk4;
        sk1.SetHex("55c107dee56e341b72c5a561bf7635c91149ffab4b3e0bff9bd2d03cb02a3ac6");
        sk2.SetHex("fb89e99db15de766e40ae613ce20b9591648171d2f6fe5d9ea6ac78aa85aec76");
        sk3.SetHex("fb89e99db15de766e40ae613ce20b9591648171d2f6fe5d9ea6ac78aa85aec71");
        sk4.SetHex("fb89e99db15de766e40ae613ce20b9591648171d2f6fe5d9ea6ac78aa85aec26");
        // 855c107dee56e341b72c5a561bf7635c91149ffab4b3e0bff9bd2d03cb02a3ac6
        // 8fb89e99db15de766e40ae613ce20b9591648171d2f6fe5d9ea6ac78aa85aec76

        log << "secret key 1: 0x" <<  BN_bn2hex(bignum(sk1).get());
        log << "secret key 2: 0x" <<  BN_bn2hex(bignum(sk2).get());

        STArray publicKeys;
        {
            ec_point pkPt = multiply(altbn128::group(), bignum(sk1), ctx);
            
            char *pkPtStr = EC_POINT_point2hex(altbn128::group(), pkPt.get(), (point_conversion_form_t)2, ctx.get());
            log << "pkPt:" << pkPtStr;

            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pkPt.get(), xBN.get(), yBN.get(), ctx.get());
            log << "public key 0: x=0x" <<  BN_bn2hex(xBN.get()) << ", y=0x" << BN_bn2hex(yBN.get());

            uint256 x = uint256_from_bignum_clear(xBN);
            uint256 y = uint256_from_bignum_clear(yBN);

            ec_point exp = set_coordinates(altbn128::group(), bignum(x), bignum(y));
            char *char_exp = EC_POINT_point2hex(altbn128::group(), exp.get(), (point_conversion_form_t)2, ctx.get());
            log << "char_exp:" << char_exp;


            STVector256 pk;
            pk.push_back(x);
            pk.push_back(y);

            publicKeys.push_back(STObject(sfRingHolder));
            STObject& obj = publicKeys.back();
            obj.setFieldV256(sfPublicKeyPair, pk);
        }
        {
            ec_point pkPt = multiply(altbn128::group(), bignum(sk2), ctx);
            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pkPt.get(), xBN.get(), yBN.get(), ctx.get());
            log << "public key 0: x=0x" <<  BN_bn2hex(xBN.get()) << ", y=0x" << BN_bn2hex(yBN.get());

            uint256 x = uint256_from_bignum_clear(xBN);
            uint256 y = uint256_from_bignum_clear(yBN);
     
            STVector256 pk;
            pk.push_back(x);
            pk.push_back(y);

            publicKeys.push_back(STObject(sfRingHolder));
            STObject& obj = publicKeys.back();
            obj.setFieldV256(sfPublicKeyPair, pk);
        }
        {
            ec_point pkPt = multiply(altbn128::group(), bignum(sk3), ctx);
            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pkPt.get(), xBN.get(), yBN.get(), ctx.get());
            log << "public key 0: x=0x" <<  BN_bn2hex(xBN.get()) << ", y=0x" << BN_bn2hex(yBN.get());

            uint256 x = uint256_from_bignum_clear(xBN);
            uint256 y = uint256_from_bignum_clear(yBN);
     
            STVector256 pk;
            pk.push_back(x);
            pk.push_back(y);

            publicKeys.push_back(STObject(sfRingHolder));
            STObject& obj = publicKeys.back();
            obj.setFieldV256(sfPublicKeyPair, pk);
        }
        {
            ec_point pkPt = multiply(altbn128::group(), bignum(sk4), ctx);
            bignum xBN, yBN;
            EC_POINT_get_affine_coordinates_GFp(altbn128::group(), pkPt.get(), xBN.get(), yBN.get(), ctx.get());
            log << "public key 0: x=0x" <<  BN_bn2hex(xBN.get()) << ", y=0x" << BN_bn2hex(yBN.get());

            uint256 x = uint256_from_bignum_clear(xBN);
            uint256 y = uint256_from_bignum_clear(yBN);
     
            STVector256 pk;
            pk.push_back(x);
            pk.push_back(y);

            publicKeys.push_back(STObject(sfRingHolder));
            STObject& obj = publicKeys.back();
            obj.setFieldV256(sfPublicKeyPair, pk);
        }                

        int index = 2;
        uint256 privateKey = sk3;

        log << "call ring sign now ...";
        
        auto sig = ring_sign(message, publicKeys, index, privateKey);
        // c0
        auto& c0 = std::get<0>(sig);
        bignum c0BN(c0);
        log << "c0=" << BN_bn2hex(c0BN.get());
        // s
        auto& s = std::get<1>(sig);
        int cnt = 0;
        for (auto sn : s) {
            bignum snBN(sn);
            log << "s[" << cnt++ << "]=" << BN_bn2hex(snBN.get());
        }
        // yTilde
        auto& yTilde = std::get<2>(sig);
        bignum yTilde0BN(yTilde[0]);
        bignum yTilde1BN(yTilde[1]);
        log << "yTilde: x=" << BN_bn2hex(yTilde0BN.get());
        log << "yTilde: y=" << BN_bn2hex(yTilde1BN.get());

        expect(true, "ring sign ...");
    }

    void test_ecdsa(){
        EC_KEY* ec_key = EC_KEY_new();
        EC_KEY_set_group(ec_key, altbn128::group());
        uint256 usk = from_hex_text<uint256>("55c107dee56e341b72c5a561bf7635c91149ffab4b3e0bff9bd2d03cb02a3ac6");
        bignum bnsk(usk);
        EC_KEY_set_private_key(ec_key, bnsk.get());

        // sign msg=publickey with privatekey
        uint256 pkx, pky;
        pkx.SetHex("1a57a2bc4bfddfb2882046ce108413e8d11f4b3a6119f2e79a48016836b854fe");
        pky.SetHex("27c8062919234180093bea0586d7395e7d8f4e78607066991cedd0afb7a92679");
 

        Blob sig = ECDSASign(pkx, usk);
        log << strHex(sig.begin(), sig.size());

        //EC_KEY* ec_pk = EC_KEY_new();
        //EC_KEY_set_group(ec_key, altbn128::group());
        // verify by publickey
        //ECDSAVerify(pkx, sig, )

        // if verify ok, refund ringdeposit to account
    }

    void run() override
    {
        test_hash_value();
        test_h1_h2();
        test_ecc_mul();
        test_ecc_mul_g();
        test_ecc_add();
        // //test_ringSign();
        // //test_();
        test_verify();
        test_sign();
        test_ecdsa();
    }
};

BEAST_DEFINE_TESTSUITE(Ring, ripple_data, ripple);

} // ripple
