#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

class AddReferee
    : public Transactor
{
public:
    AddReferee(
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("AddReferee"))
    {

    }

    TER doApply () override
    {
        Account const refereeID (mTxn.getFieldAccount160 (sfDestination));
        Account const referenceID (mTxnAccountID);

        if (!refereeID)
        {
            m_journal.warning <<
                "Malformed transaction: Referee account not specified.";

            return temDST_NEEDED;
        }
        else if (referenceID == refereeID)
        {
            // You're referring yourself.
            m_journal.trace <<
                "Malformed transaction: Redundant transaction:" <<
                " reference=" << to_string(referenceID) <<
                " referee=" << to_string(refereeID);

            return temINVALID;
        }

        return mEngine->view ().addRefer(refereeID, referenceID);
    }
};

TER
transact_AddReferee (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return AddReferee(txn, params, engine).apply();
}

}  // ripple
