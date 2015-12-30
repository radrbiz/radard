#include <BeastConfig.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/test/jtx.h>

namespace ripple
{
namespace test
{
struct Refer_test : public beast::unit_test::suite
{
    static Json::Value
    active (jtx::Account const& account,
            jtx::Account const& dest,
            jtx::Account const& referee,
            STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::Account] = account.human ();
        jv[jss::Reference] = dest.human ();
        jv[jss::Referee] = referee.human ();
        jv[jss::Amount] = amount.getJson (0);
        jv[jss::TransactionType] = "ActiveAccount";
        return jv;
    }

    void testActive ()
    {
        using namespace jtx;
        auto const gw = Account ("gw");
        auto const USD = gw["USD"];

        Env env (*this);
        env.fund (XRP (100000), "alice", gw);
        env (trust ("alice", USD (1000)));
        env (pay (gw, "alice", USD (100)));

        env (active (gw, "alice", "bob", XRP (100)), ter (tefCREATED));
        env (active ("alice", "bob", "bob", XRP (100)), ter (temDST_IS_SRC));
        env (active ("alice", "bob", "carol", XRP (100)), ter (tecNO_DST));

        env (active ("alice", "bob", gw, XRP (100)));
        auto sle = env.le ("bob");
        expect (sle &&
                sle->getAccountID (sfReferee) == gw.id () &&
                sle->getFieldAmount (sfBalance) == XRP (100));

        Json::Value amounts;
        amounts[0u]["Entry"][jss::Amount] = STAmount (USD (100)).getJson (0);
        env (active ("alice", "carol", "alice", XRP (100)),
             json (Json::StaticString ("Amounts"), amounts),
             ter (temBAD_CURRENCY));

        Json::Value limits;
        limits[0u]["Entry"][jss::LimitAmount] = STAmount (USD (100)).getJson (0);
        amounts[0u]["Entry"][jss::Amount] = STAmount (XRP (100)).getJson (0);
        env (active ("alice", "carol", "alice", XRP (100)),
             json (Json::StaticString ("Amounts"), amounts),
             json (Json::StaticString ("Limits"), limits));
        sle = env.le ("carol");
        expect (sle &&
                sle->getAccountID (sfReferee) == Account ("alice").id () &&
                sle->getFieldAmount (sfBalance) == XRP (200));
        expect (env.le (
            keylet::line (Account ("carol").id (),
                          gw.id (), USD.currency)));
    }

    void run () override
    {
        testActive ();
    }
};

BEAST_DEFINE_TESTSUITE (Refer, test, ripple);

} // test
} // ripple
