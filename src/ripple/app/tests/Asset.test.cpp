#include <BeastConfig.h>
#include <ripple/test/jtx.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple
{
namespace test
{
struct Asset_test : public beast::unit_test::suite
{
    static Json::Value
    issue (jtx::Account const& account,
           jtx::Account const& dest,
           STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::Account] = account.human ();
        jv[jss::Destination] = dest.human ();
        jv[jss::Amount] = amount.getJson (0);
        jv[jss::TransactionType] = "Issue";
        return jv;
    }


    /** An offer exists
     */
    bool
    isOffer (jtx::Env& env,
        jtx::Account const& account,
        STAmount const& takerPays,
        STAmount const& takerGets)
    {
        bool exists = false;
        forEachItem (*env.open(), account,
            [&](std::shared_ptr<SLE const> const& sle)
            {
                if (sle->getType () == ltOFFER &&
                    sle->getFieldAmount (sfTakerPays) == takerPays &&
                        sle->getFieldAmount (sfTakerGets) == takerGets)
                    exists = true;
            });
        return exists;
    }

    void appendReleasePoint (Json::Value& jv, uint32_t expiration, uint64_t releaseRate)
    {
        auto& releaseSchedule = jv["ReleaseSchedule"];
        auto& releasePoint = releaseSchedule.append (Json::Value::null)["ReleasePoint"];
        releasePoint["Expiration"] = expiration;
        releasePoint["ReleaseRate"] = to_string (releaseRate);
    }

    void expectBalanceAndReserve (jtx::Env& env,
                     jtx::Account const& account,
                     jtx::Account const& gw,
                     jtx::IOU ASSET,
                     int balance,
                     int reserve)
    {
        auto line = env.le (
            keylet::line (account.id (),
                          gw.id (), ASSET.currency));
        expect (line, "trust line not found");
        if (!line)
            return;
        expect (line->getFieldAmount (sfBalance) == ASSET (-balance), "bad balance");
        expect (line->getFieldAmount (sfReserve) == ASSET (-reserve), "bad reserve");
    }

    void testIssue ()
    {
        using namespace jtx;
        auto const gw = Account ("gw");
        auto const ASSET = gw["4153534554000000000000000000000000000000"];

        Env env (*this);
        env.fund (XRP (100000), "alice", gw);

        // valid expiration, invalid release rate
        auto jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 86400, 0);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 0);
        appendReleasePoint (jv, 86400, 0);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 50000000);
        appendReleasePoint (jv, 86400, 50000000);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 50000000);
        appendReleasePoint (jv, 86400, 100000000);
        appendReleasePoint (jv, 172800, 1000000001);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        // invalid expiration, valid release rate
        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 0);
        appendReleasePoint (jv, 0, 50000000);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 16400, 50000000);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 86400, 50000000);
        appendReleasePoint (jv, 172800 - 1, 100000000);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 86400, 50000000);
        appendReleasePoint (jv, 0, 100000000);
        env (jv, ter (temBAD_RELEASE_SCHEDULE));

        // Destination account not exists.
        jv = issue (gw, "bob", ASSET (40000000));
        appendReleasePoint (jv, 0, 50000000);
        env (jv, ter (tecNO_DST));

        // Issuer != source account.
        jv = issue ("alice", "bob", ASSET (40000000));
        appendReleasePoint (jv, 0, 50000000);
        env (jv, ter (temBAD_ISSUER));

        // Successful issue.
        jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 50000000);
        appendReleasePoint (jv, 86400, 100000000);
        appendReleasePoint (jv, 172800, 1000000000);
        env (jv);

        expect (env.le (keylet::asset (gw.id (), ASSET.currency)));
        unexpected (env.le (keylet::asset (Account ("alice").id (),
                                           ASSET.currency)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 40000000, 0);

        uint256 assetStateIndexBase = getAssetStateIndex (Account ("alice").id (), gw.id (), ASSET.currency);
        uint256 assetStateIndexZero = getQualityIndex (assetStateIndexBase);
        expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                "bad asset state created for hot wallet");
        auto const assetStateNext = env.open ()->succ (assetStateIndexZero, getQualityNext (assetStateIndexZero));
        expect (!assetStateNext, "bad asset state created for hot wallet");

        // Can not issue again.
        env (jv, ter (tefCREATED));
    }

    void testRelease (uint64_t releaseRate1, uint64_t releaseRate2, uint64_t releaseRate3)
    {
        releaseRate1 *= 10000000;
        releaseRate2 *= 10000000;
        releaseRate3 *= 10000000;

        using namespace jtx;
        auto const gw = Account ("gw");
        auto const ASSET = gw["4153534554000000000000000000000000000000"];
        uint256 assetStateIndexBase = getAssetStateIndex (Account ("bob").id (), gw.id (), ASSET.currency),
                assetStateIndexZero = getQualityIndex (assetStateIndexBase),
                assetStateIndexNext = getQualityNext (assetStateIndexZero);
        int32_t balance = 0,
                reserve = 0;
        struct AssetState
        {
            uint64_t amount = 0;
            uint64_t delivered = 0;
        };
        AssetState state[2];

        Env env (*this);

        auto updateBalance = [&](auto& state, auto const& amount, auto const& releaseRate)
        {
            state.amount += amount;
            auto delivered = state.amount * releaseRate / 1000000000 - state.delivered;
            balance += delivered;
            reserve += amount - delivered;
            state.delivered += delivered;
        };

        auto checkBalance = [&]()
        {
            expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, balance, reserve);
        };

        auto checkAssetState = [&](auto assetStateNext, auto& state)
        {
            auto &amount = state.amount, &delivered = state.delivered;
            std::shared_ptr<SLE const> sle = env.le (keylet::asset_state (*assetStateNext));
            expect (sle, "asset state not found");
            if (!sle)
                return;
            expect (sle->getFieldAmount (sfAmount) == (amount == 0 ? 0 : ASSET (amount)), "bad amount");
            expect (sle->getFieldAmount (sfDeliveredAmount) == (delivered == 0 ? 0 : ASSET (delivered)), "bad delievered");
        };
        env.fund (XRP (100000), "alice", "bob", "carol", gw);

        /// Asset released imediately and fully released at last release point.
        auto jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, releaseRate1);
        appendReleasePoint (jv, 86400, releaseRate2);
        appendReleasePoint (jv, 172800, releaseRate3);
        env (jv);
        env.close (std::chrono::seconds (86400 + 600));

        env (trust ("bob", ASSET (109)));

        {
            // first release, all reserved, [0/5]
            env (pay ("alice", "bob", ASSET (5)));
            updateBalance (state[0], 5, releaseRate1);
            checkBalance ();

            expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                    "bad asset state zero created");
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            expect (assetStateNext, "asset state today not created");
            if (assetStateNext)
            {
                checkAssetState (assetStateNext, state[0]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (!assetStateNext, "too many asset state created");
            }
        }

        {
            // first release, [4/99]
            env (pay ("alice", "bob", ASSET (94)));
            updateBalance (state[0], 94, releaseRate1);
            checkBalance ();

            expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                    "bad asset state zero created");
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            expect (assetStateNext, "asset state missing");
            if (assetStateNext)
            {
                checkAssetState (assetStateNext, state[0]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (!assetStateNext, "too many asset state created");
            }
        }

        {
            // first release, [5/104]
            env (pay ("alice", "bob", ASSET (5)));
            updateBalance (state[0], 5, releaseRate1);
            checkBalance ();

            expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                    "bad asset state zero created");
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            expect (assetStateNext, "asset state missing");
            if (assetStateNext)
            {
                checkAssetState (assetStateNext, state[0]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (!assetStateNext, "too many asset state created");
            }
        }

        env (trust ("carol", ASSET (100)));
        env.close (std::chrono::seconds (86400));

        {
            // line should not be updated now.
            checkBalance ();

            // second release, [10/104, 0/5]
            env (pay ("alice", "bob", ASSET (5)));
            updateBalance (state[0], 0, releaseRate2);
            updateBalance (state[1], 5, releaseRate1);
            checkBalance ();

            expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                    "bad asset state zero created");
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            expect (assetStateNext, "asset state missing");
            if (assetStateNext)
            {
                checkAssetState (assetStateNext, state[0]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (assetStateNext, "asset state today not created");
                checkAssetState (assetStateNext, state[1]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (!assetStateNext, "too many asset state created");
            }
        }

        env.close (std::chrono::seconds (86400));

        {
            // line should not be updated now.
            checkBalance ();

            // third release, [0/5]
            env (offer ("bob", XRP (50), ASSET (50)), txflags (tfSell));
            updateBalance (state[0], 0, releaseRate3);
            updateBalance (state[1], 0, releaseRate2);
            checkBalance ();

            if (releaseRate3 == 100 * 10000000)
            {
                expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                        "bad asset state zero created");
            }
            else
            {
                expect (env.le (keylet::asset_state (assetStateIndexZero)),
                        "asset state zero missing");
                checkAssetState (&assetStateIndexZero, state[0]);
            }
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            expect (assetStateNext, "asset state missing");
            if (assetStateNext)
            {
                checkAssetState (assetStateNext, state[1]);
                assetStateNext = env.open ()->succ (*assetStateNext, assetStateIndexNext);
                expect (!assetStateNext, "too many asset state created");
            }
        }

        env.close (std::chrono::seconds (86400));

        {
            // line should not be updated now.
            checkBalance ();
            // last release, no more asset state
            env (offer ("bob", XRP (50), ASSET (50)), txflags (tfSell));
            updateBalance (state[1], 0, releaseRate3);
            checkBalance ();

            if (releaseRate3 == 100 * 10000000)
            {
                expect (!env.le (keylet::asset_state (assetStateIndexZero)),
                        "bad asset state zero created");
            }
            else
            {
                expect (env.le (keylet::asset_state (assetStateIndexZero)),
                        "asset state zero missing");
                state[0].amount += state[1].amount;
                state[0].delivered += state[1].delivered;
                checkAssetState (&assetStateIndexZero, state[0]);
            }
            auto assetStateNext = env.open ()->succ (assetStateIndexZero, assetStateIndexNext);
            unexpected (assetStateNext, "asset state not removed");
            checkBalance ();
        }
    }

    void testPayment ()
    {
        using namespace jtx;
        auto const gw = Account ("gw");
        auto const ASSET = gw["4153534554000000000000000000000000000000"];

        Env env (*this);
        
        env.fund (XRP (100000), "alice", "bob", "carol", gw);
        env (trust ("alice", ASSET (40000000)));
        auto jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 5 * 10000000);
        appendReleasePoint (jv, 86400, 10 * 10000000);
        appendReleasePoint (jv, 172800, 100 * 10000000);
        env (jv);
        env.close (std::chrono::seconds (86400 + 600));

        env (trust ("bob", ASSET (200)));
        env (trust ("carol", ASSET (200)));

        // Payments that should fail.
        // gw -> hotwallet
        env (pay (gw, "alice", ASSET (100)), ter (temBAD_ISSUER));
        // gw -> account
        env (pay (gw, "bob", ASSET (100)), ter (temBAD_ISSUER));
        // hotwallet -> gw
        env (pay ("alice", gw, ASSET (100)), ter (temBAD_ISSUER));
        // account -> gw
        env (pay ("bob", gw, ASSET (100)), ter (temBAD_ISSUER));
        // hotwallet -> account
        env (pay ("alice", "bob", ASSET (4)), ter (temBAD_CURRENCY));
        env (pay ("alice", "bob", ASSET (5)), txflags (tfPartialPayment),
             ter (temBAD_SEND_XRP_PARTIAL));
        env (pay ("alice", "bob", ASSET (10)), sendmax (ASSET (5)),
             ter (temBAD_SEND_XRP_MAX));
        env (pay ("alice", "bob", ASSET (100.1)), ter (temBAD_CURRENCY));


        // Day 0. alice->bob, alice->carol, bob->alice, carol->bob
        env (pay ("alice", "bob", ASSET (100)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999900, 0);
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 5, 95);

        env (pay ("alice", "carol", ASSET (100)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999800, 0);
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 5, 95);

        env.close ();
        env (pay ("bob", "alice", ASSET (6)), ter (tecPATH_PARTIAL));
        env (pay ("bob", "alice", ASSET (5)));
        env (pay ("bob", "alice", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 95);
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999805, 0);

        env (pay ("carol", "bob", ASSET (6)), ter (tecPATH_PARTIAL));
        env (pay ("carol", "bob", ASSET (5)));
        env (pay ("carol", "bob", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 0, 95);
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 100);


        // Day 1. alice->bob, carol->alice, bob->carol
        env.close (std::chrono::seconds (86400));

        // test hotwallet -> account
        env (pay ("alice", "bob", ASSET (100)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999705, 0);
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 10, 190);

        // test account -> hotwallet
        env (pay ("carol", "alice", ASSET (6)), ter (tecPATH_PARTIAL));
        env (pay ("carol", "alice", ASSET (5)));
        env (pay ("carol", "alice", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 0, 90);
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999710, 0);
        
        // test account -> account
        env.close ();
        env (pay ("bob", "carol", ASSET (11)), ter (tecPATH_PARTIAL));
        env (pay ("bob", "carol", ASSET (10)));
        env (pay ("bob", "carol", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 190);
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 0, 100);


        // Day 2. alice->carol, bob->alice
        env.close (std::chrono::seconds (86400));

        // test hotwallet -> account
        env (pay ("alice", "carol", ASSET (100)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999610, 0);
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 96, 104);

        // test account -> hotwallet
        env (pay ("bob", "alice", ASSET (101)), ter (tecPATH_PARTIAL));
        env (pay ("bob", "alice", ASSET (100)));
        env (pay ("bob", "alice", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 90);
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999710, 0);


        // Day 3. bob->alice
        env.close (std::chrono::seconds (86400));

        // test account -> hotwallet
        env (pay ("bob", "alice", ASSET (91)), ter (tecPATH_PARTIAL));
        env (pay ("bob", "alice", ASSET (90)));
        env (pay ("bob", "alice", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 0);
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999800, 0);
        
        // Day 3. carol->bob
        env.close (std::chrono::seconds (86400));
        
        // test account -> account
        env (pay ("carol", "bob", ASSET (201)), ter (tecPATH_PARTIAL));
        env (pay ("carol", "bob", ASSET (200)));
        env (pay ("carol", "bob", ASSET (5)), ter (tecPATH_DRY));
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 0, 0);
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 10, 190);

        // Delete trust.
        env.close ();
        jv = trust ("bob", ASSET (0));
        jv[jss::Flags] = tfClearNoRipple;
        env (jv, ter (temDISABLED));

        jv = trust ("carol", ASSET (0));
        jv[jss::Flags] = tfClearNoRipple;
        env (jv);

        unexpected (env.le (
            keylet::line (Account ("carol").id (),
                          gw.id (), ASSET.currency)));
    }

    void testOffer ()
    {
        using namespace jtx;
        auto const gw = Account ("gw");
        auto const ASSET = gw["4153534554000000000000000000000000000000"];

        Env env (*this);
        
        env.fund (XRP (100000), "alice", "bob", "carol", gw);
        env (trust ("alice", ASSET (40000000)));
        auto jv = issue (gw, "alice", ASSET (40000000));
        appendReleasePoint (jv, 0, 5 * 10000000);
        appendReleasePoint (jv, 86400, 10 * 10000000);
        appendReleasePoint (jv, 172800, 100 * 10000000);
        env (jv);
        env.close (std::chrono::seconds (86400 + 600));

        env (trust ("bob", ASSET (200)));
        env (trust ("carol", ASSET (200)));

        // Offers that should fail.
        // gw
        env (offer (gw, XRP (50), ASSET (50)), txflags (tfSell), ter (temDISABLED));
        env (offer (gw, ASSET (50), XRP (50)), txflags (tfSell), ter (temDISABLED));
        // hotwallet
        env (offer ("alice", XRP (50), ASSET (50)), ter (temDISABLED));
        env (offer ("alice", ASSET (50), XRP (50)), txflags (tfSell), ter (temDISABLED));
        // account
        env (offer ("bob", XRP (50), ASSET (50)), ter (temDISABLED));
        env (offer ("bob", ASSET (50), XRP (50)), txflags (tfSell), ter (temDISABLED));
        env (offer ("bob", ASSET (4), XRP (50)), ter (temBAD_CURRENCY));
        env (offer ("bob", XRP (50), ASSET (4)), txflags (tfSell), ter (temBAD_CURRENCY));
        env (offer ("bob", ASSET (50.1), XRP (50)), ter (temBAD_CURRENCY));
        env (offer ("bob", XRP (50), ASSET (50.1)), txflags (tfSell), ter (temBAD_CURRENCY));


        // Day 0. hotwallet sell, account buy.
        env (offer ("alice", XRP (200), ASSET (200)), txflags (tfSell),
             require (offers ("alice", 1)));
        env (offer ("bob", ASSET (100), XRP (100)),
             require (offers ("alice", 1), offers ("bob", 0)));
        expect (isOffer (env, "alice", XRP (100), ASSET (100)));
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999900, 0);
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 5, 95);

        // Day 0. account over sell, hotwallet buy.
        env.close();
        env (offer ("bob", XRP (10), ASSET (100)), txflags (tfSell),
             require (offers ("bob", 1)));
        env (offer ("alice", ASSET (10), XRP (1)),
             require (offers ("alice", 2), offers ("bob", 0)));
        expect (isOffer (env, "alice", ASSET (5), XRP (0.5)));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 95);
        expectBalanceAndReserve (env, Account ("alice"), gw, ASSET, 39999905, 0);

        // Day 0. offer cancel.
        env.close();
        env (Json::Value (),
             json (jss::Account, Account ("alice").human ()),
             json (Json::StaticString ("TransactionType"), "OfferCancel"),
             json (Json::StaticString ("OfferSequence"), env.seq ("alice") - 1),
             require (offers ("alice", 1)));
        
        // Day 0. account over sell without balance 0.
        env (offer ("bob", XRP (1), ASSET (10)), txflags (tfSell), ter (tecUNFUNDED_OFFER));

        
        env.close (std::chrono::seconds (86400));
        // Day 1. account over sell before release.
        env (offer ("bob", XRP (1), ASSET (10)), txflags (tfSell),
             require (offers ("bob", 1)));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 5, 90);


        // Day 2. account over buy.
        env.close (std::chrono::seconds (86400));
        env (offer ("carol", ASSET (120), XRP (120)), txflags (tfImmediateOrCancel),
             require (offers ("alice", 0), offers ("carol", 0), offers ("bob", 0)));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 85, 0);
        expectBalanceAndReserve (env, Account ("carol"), gw, ASSET, 5, 105);

        // Day 2. failed tfFillOrKill
        env (offer ("bob", XRP (1), ASSET (85)), txflags (tfSell|tfFillOrKill),
             require (offers ("bob", 0)));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 85, 0);
        
        // Day 2. successful tfFillOrKill
        env.close();
        env (offer ("carol", ASSET (120), XRP (120)),
             require (offers ("carol", 1)));

        env.close();
        env (offer ("bob", XRP (1), ASSET (85)), txflags (tfSell|tfFillOrKill),
             require (offers ("bob", 0), offers ("carol", 1)));
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 0);
        
        // Delete trust.
        env.close ();
        expectBalanceAndReserve (env, Account ("bob"), gw, ASSET, 0, 0);
        jv = trust ("carol", ASSET (0));
        jv[jss::Flags] = tfClearNoRipple;
        env (jv, ter (temDISABLED));

        jv = trust ("bob", ASSET (0));
        jv[jss::Flags] = tfClearNoRipple;
        env (jv);

        unexpected (env.le (
            keylet::line (Account ("bob").id (),
                          gw.id (), ASSET.currency)));
    }

    void run () override
    {
        testIssue ();
        testRelease (5, 10, 100);
        testRelease (0, 10, 95);
        testPayment ();
        testOffer ();
    }
};

BEAST_DEFINE_TESTSUITE (Asset, test, ripple);

} // test
} // ripple
