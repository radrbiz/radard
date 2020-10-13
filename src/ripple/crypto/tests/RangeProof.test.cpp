
#include <BeastConfig.h>
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>

#include <secp256k1.h>
#include <secp256k1_rangeproof.h>
#include <secp256k1_schnorrsig.h>
#include <secp256k1_ecdh.h>

namespace ripple {

class RangeProof_test : public beast::unit_test::suite
{

public:
    void test_range_proof(){
        // pedersen commit => output commit
        /* Generates a pedersen commitment: *commit = blind * G + value * G2. The blinding factor is 32 bytes.*/
        // rangeproof sign => output proof
        // rangeproof rewind == verify + recover info ==> verify proof
        const uint64_t testvs[11] = {0, 1, 5, 11, 65535, 65537, INT32_MAX, UINT32_MAX, INT64_MAX - 1, INT64_MAX, UINT64_MAX};
        secp256k1_pedersen_commitment commit;
        secp256k1_pedersen_commitment commit2;
        unsigned char proof[5134 + 1]; /* One additional byte to test if trailing bytes are rejected */
        unsigned char blind[32] = {"a"};
        unsigned char blindout[32];
        unsigned char message[4096];
        size_t mlen;
        uint64_t v;
        uint64_t vout;
        uint64_t vmin;
        uint64_t minv;
        uint64_t maxv;
        size_t len;
        size_t i;
        size_t j;
        size_t k;
        /* Short message is a Simone de Beauvoir quote */
        const unsigned char message_short[120] = "When I see my own likeness in the depths of someone else's consciousness,  I always experience a moment of panic.";
        /* Long message is 0xA5 with a bunch of this quote in the middle */
        unsigned char message_long[3968];
        memset(message_long, 0xa5, sizeof(message_long));
        for (i = 1200; i < 3600; i += 120) {
            memcpy(&message_long[i], message_short, sizeof(message_short));
        }

        static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
        // no way to generate rand256 by using testrand
        //secp256k1_rand256(blind);
        for (i = 0; i < 11; i++) {
            v = testvs[i];
            expect(secp256k1_pedersen_commit(ctx, &commit, blind, v, secp256k1_generator_h), "secp256k1_pedersen_commit");
            expect(secp256k1_pedersen_commit(ctx, &commit2, blind, v, secp256k1_generator_h), "secp256k1_pedersen_commit");
log << "pedersen commitment result:" << commit.data;
            for (vmin = 0; vmin < (i<9 && i > 0 ? 2 : 1); vmin++) {
                const unsigned char *input_message = NULL;
                size_t input_message_len = 0;
                /* vmin is always either 0 or 1; if it is 1, then we have no room for a message.
                 * If it's 0, we use "minimum encoding" and only have room for a small message when
                 * `testvs[i]` is >= 4; for a large message when it's >= 2^32. */
                if (vmin == 0 && i > 2) {
                    input_message = message_short;
                    input_message_len = sizeof(message_short);
                }
                if (vmin == 0 && i > 7) {
                    input_message = message_long;
                    input_message_len = sizeof(message_long);
                }
                len = 5134;
                secp256k1_rangeproof_sign(ctx, proof, &len, vmin, &commit, blind, commit.data, 0, 0, v, input_message, input_message_len, NULL, 0, secp256k1_generator_h);
log << "sign result:" << proof;
log << "sign input_message:" << input_message;
                expect(len <= 5134);
                mlen = 4096;
                expect(secp256k1_rangeproof_rewind(ctx, blindout, &vout, message, &mlen, commit.data, &minv, &maxv, &commit, proof, len, NULL, 0, secp256k1_generator_h));
log << "rewind blindout:" << blindout;
log << "rewind vout:" << vout;
log << "rewind msgout:" << message;

                if (input_message != NULL) {
                    expect(memcmp(message, input_message, input_message_len) == 0);
                }
                for (j = input_message_len; j < mlen; j++) {
                    expect(message[j] == 0);
                }
                expect(mlen <= 4096);
                expect(memcmp(blindout, blind, 32) == 0);
                expect(vout == v);
                expect(minv <= v);
                expect(maxv >= v);
                len = 5134;
                expect(secp256k1_rangeproof_sign(ctx, proof, &len, v, &commit, blind, commit.data, -1, 64, v, NULL, 0, NULL, 0, secp256k1_generator_h));
                expect(len <= 73);
                expect(secp256k1_rangeproof_rewind(ctx, blindout, &vout, NULL, NULL, commit.data, &minv, &maxv, &commit, proof, len, NULL, 0, secp256k1_generator_h));
                expect(memcmp(blindout, blind, 32) == 0);
                expect(vout == v);
                expect(minv == v);
                expect(maxv == v);

                /* Check with a committed message */
                len = 5134;
                expect(secp256k1_rangeproof_sign(ctx, proof, &len, v, &commit, blind, commit.data, -1, 64, v, NULL, 0, message_short, sizeof(message_short), secp256k1_generator_h));
                expect(len <= 73);
                expect(!secp256k1_rangeproof_rewind(ctx, blindout, &vout, NULL, NULL, commit.data, &minv, &maxv, &commit, proof, len, NULL, 0, secp256k1_generator_h));
                expect(!secp256k1_rangeproof_rewind(ctx, blindout, &vout, NULL, NULL, commit.data, &minv, &maxv, &commit, proof, len, message_long, sizeof(message_long), secp256k1_generator_h));
                expect(secp256k1_rangeproof_rewind(ctx, blindout, &vout, NULL, NULL, commit.data, &minv, &maxv, &commit, proof, len, message_short, sizeof(message_short), secp256k1_generator_h));
                expect(memcmp(blindout, blind, 32) == 0);
                expect(vout == v);
                expect(minv == v);
                expect(maxv == v);
            }
        }
        secp256k1_context_destroy(ctx);
    };

    void test_schnorr(){
        // input(40) = output(25) + change(15)
        // 40*G + ri*H = (25*G + rc*H) + (15*G + ro*H)
        // k:rand_nonce
        // r:blind_factor
        static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
        // sender
        unsigned char rs[32];
        unsigned char ks[32];
        memset(rs, 14, 1);
        // receiver
        unsigned char rr[32];
        memset(rr, 11, 1);
        unsigned char kr[32];

        //step 1
        // sender calculate (pkrs=rs*G, pkks=ks*G)
        secp256k1_pubkey pkks;
        secp256k1_pubkey pkrs;
        secp256k1_ec_pubkey_create(ctx, &pkrs, rs);
        secp256k1_ec_pubkey_create(ctx, &pkks, ks);

        // step 2
        // receiver calculate (pkrr=rr*G, pkkr=kr*g, Sr(schnorr sign)=kr+e*rr)
        secp256k1_pubkey pkkr;
        secp256k1_pubkey pkrr;
        secp256k1_schnorrsig sigReceiver;
        secp256k1_ec_pubkey_create(ctx, &pkkr, kr);
        secp256k1_ec_pubkey_create(ctx, &pkrr, rr);
        unsigned char msg[] = "schnorr sig message: fee|ledger_index";
        memcpy(msg, &pkks.data, sizeof(pkks.data));
        //msg = msg + pkks+pkkr + pkrs+pkrr
log << "msg:" << msg;
        secp256k1_schnorrsig_sign(ctx, &sigReceiver, NULL, msg, kr, NULL, NULL);

        // step 3
        // verify
        expect(secp256k1_schnorrsig_verify(ctx, &sigReceiver, msg, &pkkr) == 1, "");
        // sign
        secp256k1_schnorrsig sigSender;
        secp256k1_schnorrsig_sign(ctx, &sigSender, NULL, msg, ks, NULL, NULL);

        // step 4
        // calc ss*G+sr*G
        secp256k1_pubkey pkss;
        secp256k1_pubkey pksr;
        secp256k1_pubkey sssrpk;
        secp256k1_ec_pubkey_create(ctx, &pkss, sigReceiver.data);
        secp256k1_ec_pubkey_create(ctx, &pksr, sigSender.data);
        const secp256k1_pubkey* sssrpks[2] = {&pkss, &pksr};
        expect(secp256k1_ec_pubkey_combine(ctx, &sssrpk, sssrpks, 2) == 1);
        // calc (ks+kr)*G
        secp256k1_pubkey kskrpk;
        const secp256k1_pubkey* kskrpks[2] = {&pkkr, &pkks};
        expect(secp256k1_ec_pubkey_combine(ctx, &kskrpk, kskrpks, 2) == 1);
        // calc e*25*G

        // verify ss*G + sr*G == ks*G + kr*G + e*(25*G)
        //    sssrpk + neg kskrpk ?= 
        expect(secp256k1_ec_pubkey_negate(ctx, &kskrpk) == 1);
        expect(secp256k1_ec_pubkey_tweak_add(ctx, &sssrpk, kskrpk.data) == 1);
        //     calc commitment(public data on-chain)
        secp256k1_pedersen_commitment commit1;
        secp256k1_pedersen_commitment commit2;
        secp256k1_pedersen_commitment commit3;
        uint64_t v1 = 40; // src
        uint64_t v2 = 15; // change
        uint64_t v3 = 25; // dst
        unsigned char blind1[32] = {20};  // src
        unsigned char blind2[32] = {34};  // change
        unsigned char blind3[32] = {11};  // dst
        expect(secp256k1_pedersen_commit(ctx, &commit1, blind1, v1, secp256k1_generator_h), "secp256k1_pedersen_commit src");
        expect(secp256k1_pedersen_commit(ctx, &commit2, blind2, v2, secp256k1_generator_h), "secp256k1_pedersen_commit change");
        expect(secp256k1_pedersen_commit(ctx, &commit3, blind3, v3, secp256k1_generator_h), "secp256k1_pedersen_commit dst");
        // excess = commit1 - commit2 - commit3

        // check( sssrpk + neg kskrpk ?= excess)

        // free ctx
        secp256k1_context_destroy(ctx);
    }

    void test_payment_ct(){
        // Step0 sender got 50
        // Step1 50(sender) = 10(toA change) + 40(toB receiver)
        static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
        // (r,R) r:tx_key R:tx_public_key
        // (a,A) receiver a:private_view_key A:receiver_public_view_key
        secp256k1_pubkey R;
        secp256k1_pubkey A;
        unsigned char r[32];
        unsigned char a[32];
        memset(r, 1, 1);
        memset(a, 2, 1);
        expect(secp256k1_ec_pubkey_create(ctx, &R, r) == 1, "create publickey");
        expect(secp256k1_ec_pubkey_create(ctx, &A, a) == 1, "create publickey");

        // ecdh
        unsigned char dh1[32];
        unsigned char dh2[32];
        expect(secp256k1_ecdh(ctx, dh1, &A, r , NULL, NULL) == 1, "compute a dh secret.");
        expect(secp256k1_ecdh(ctx, dh2, &R, a , NULL, NULL) == 1, "compute a dh secret.");
        expect(std::memcmp(dh1, dh2, 32) == 0, "dh1 != dh2");

        // mask = Hash("commitment_mask" || Hash(8aR||i)) A*r = a*R
        // en(amount) = 8 byte amount XOR first 8 bytes of keccak("amount" || Hs(8rA||i))
        // de(amount) = 8 byte encrypted amount XOR first 8 bytes of keccak("amount" || Hs(8aR||i))
        // P = H(rA)G + B
        // dst P = H(rA)G + B

        auto u256tou64 = [](unsigned char* in){
            char* temp = reinterpret_cast< char*>(in);
            return strtoull(temp, NULL, 0);
        };
        // C = mask*G + amount*H
        //receiver commit
        uint256 hDh = sha512Half(&dh1);
        std::string prefixAmount = "amount";
        std::string prefixMask = "commitment_mask";
        uint256 amount1 = sha512Half(prefixAmount + to_string(hDh));
        amount1 ^= uint256(40);
        uint256 mask1 = sha512Half(prefixMask + to_string(hDh));
        secp256k1_pedersen_commitment commit1;
        // uint256 to uint64
        uint64_t u64amount1 = u256tou64(amount1.data());
        expect(secp256k1_pedersen_commit(ctx, &commit1, mask1.data(), u64amount1, secp256k1_generator_h), "secp256k1_pedersen_commit");
        // range proof
        unsigned char proof[5134 + 1]; /* One additional byte to test if trailing bytes are rejected */
        size_t len = 5134;
        uint64_t vmin = 0;
        const unsigned char input_message[11] = "payment ct";
        secp256k1_rangeproof_sign(ctx, proof, &len, vmin, &commit1, mask1.data(), commit1.data, 0, 0, u64amount1, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h);

        // change commit
        // sending change to yourself; derivation = a*R(a:sender_view_secret_key, R:tx_public_key)
        // sending to the recipient; derivation = r*A (r:tx_secret_key A:dst_view_public_key)
        unsigned char sender_view_secret_key[32];
        memset(sender_view_secret_key, 3, 1);
        //secp256k1_pubkey sender_view_public_key;
        //expect(secp256k1_ec_pubkey_create(ctx, &sender_view_public_key, sender_view_secret_key) == 1, "create sender view publickey");
        unsigned char dh3[32];
        expect(secp256k1_ecdh(ctx, dh3, &R, sender_view_secret_key , NULL, NULL) == 1, "compute a charge secret.");

        uint256 amount2 = sha512Half(prefixAmount + std::string((char*)dh3));
        amount2 ^= uint256(10);
        uint256 mask2 = sha512Half(prefixMask + std::string((char*)dh3));
        secp256k1_pedersen_commitment commit2;
        uint64_t u64amount2 = u256tou64(amount2.data());
        expect(secp256k1_pedersen_commit(ctx, &commit2, mask2.data(), u64amount2, secp256k1_generator_h), "secp256k1_pedersen_commit");
        // range proof
        secp256k1_rangeproof_sign(ctx, proof, &len, vmin, &commit2, mask2.data(), commit2.data, 0, 0, u64amount2, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h);
    }

    // first time transfer C = mask*G + amount*H
    // mask = Hash("commitment_mask" || Hash(8aR||i))   dh = sender_view_secret_key * tx_public_key
    // en(amount) = 8 byte amount XOR first 8 bytes of keccak("amount" || Hs(8rA||i))
    void test_deposit_ct(){
        std::string prefixAmount = "amount";
        std::string prefixMask = "commitment_mask";
        static secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN);
        auto u256tou64 = [](unsigned char* in){
            char* temp = reinterpret_cast< char*>(in);
            return strtoull(temp, NULL, 0);
        };
        unsigned char sender_view_secret_key[32];
        memset(sender_view_secret_key, 1, 1);
        secp256k1_pubkey sender_view_public_key;
        expect(secp256k1_ec_pubkey_create(ctx, &sender_view_public_key, sender_view_secret_key) == 1, "create sender view publickey");

        unsigned char tx_key[32];
        memset(tx_key, 2, 1);
        secp256k1_pubkey tx_public_key;
        expect(secp256k1_ec_pubkey_create(ctx, &tx_public_key, tx_key) == 1, "create tx publickey");
        
        unsigned char dh[32];
        expect(secp256k1_ecdh(ctx, dh, &tx_public_key, sender_view_secret_key , NULL, NULL) == 1, "compute shared secret.");

        uint256 amount = sha512Half(prefixAmount + std::string((char*)dh));
        amount ^= uint256(50);
        uint64_t u64amount = u256tou64(amount.data());
        uint256 mask = sha512Half(prefixMask + std::string((char*)dh));
        secp256k1_pedersen_commitment commit;
        expect(secp256k1_pedersen_commit(ctx, &commit, mask.data(), u64amount, secp256k1_generator_h), "secp256k1_pedersen_commit");

        // proof
        unsigned char proof[5134 + 1]; /* One additional byte to test if trailing bytes are rejected */
        size_t len = 5134;
        uint64_t vmin = 0;
        const unsigned char input_message[11] = "deposit ct";
log << "pedersen commitment result:" << commit.data;
        expect(secp256k1_rangeproof_sign(ctx, proof, &len, vmin, &commit, mask.data(), commit.data,
            0, 0, u64amount, input_message, sizeof(input_message), NULL, 0, secp256k1_generator_h) == 1,
                "range proof");

        // verify
        //expect(secp256k1_rangeproof_rewind(ctx, blindout, &vout, message, &mlen, commit.data, &minv, &maxv, &commit, proof, len, NULL, 0, secp256k1_generator_h));
    };

    void run() override
    {
        //test_range_proof();
        //test_schnorr();
        test_deposit_ct();
        test_payment_ct();
    }
};

BEAST_DEFINE_TESTSUITE(RangeProof, ripple_data, ripple);
}   //ripple