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

        //
        // Open a ledger for editing.
        SLE::pointer sleReferee(mEngine->entryCache(ltACCOUNT_ROOT, getAccountRootIndex(refereeID)));
        SLE::pointer sleReference(mEngine->entryCache(ltACCOUNT_ROOT, getAccountRootIndex(referenceID)));

        if (!sleReferee) {
            // Referee account does not exist.
            m_journal.trace <<  "Referee account does not exist.";

            return tecNO_DST;
        } else if (!sleReference) {
            // Reference account does not exist.
            m_journal.trace << "Reference account does not exist.";

            return terNO_ACCOUNT;
        } else if (sleReference->isFieldPresent(sfReferee)
                   && sleReference->getFieldAccount(sfReferee).getAccountID().isNonZero()) {
            m_journal.trace << "Referee has been set.";

            return tefREFEREE_EXIST;
        } else if (sleReference->isFieldPresent(sfReferences)
                   && !sleReference->getFieldArray(sfReferences).empty()) {
            m_journal.trace << "Reference has been set.";
            
            return tefREFERENCE_EXIST;
        } else {
            STArray references(sfReferences);
            bool oldFormat = false;
            auto const referIndex = getAccountReferIndex (refereeID);
            SLE::pointer sleRefer(mEngine->entryCache (ltREFER, referIndex));
            if (sleRefer && sleRefer->isFieldPresent(sfReferences)) {
                references = sleReference->getFieldArray(sfReferences);
            } else if (sleReferee->isFieldPresent(sfReferences)) {
                references = sleReferee->getFieldArray(sfReferences);
                oldFormat = true;
            }

            for (auto it = references.begin(); it != references.end(); ++it) {
                Account id = it->getFieldAccount(sfReference).getAccountID();
                if (id == referenceID) {
                    m_journal.trace << "Malformed transaction: Reference has been set.";
                    return tefREFERENCE_EXIST;
                }
            }

            int referenceHeight=0;
            if (sleReferee->isFieldPresent(sfReferenceHeight))
                referenceHeight=sleReferee->getFieldU32(sfReferenceHeight);
            
            mEngine->entryModify(sleReference);
            mEngine->entryModify(sleReferee);
            sleReference->setFieldAccount(sfReferee, refereeID);
            sleReference->setFieldU32(sfReferenceHeight, referenceHeight+1);
            references.push_back(STObject(sfReferenceHolder));
            references.back().setFieldAccount(sfReference, referenceID);
            if (oldFormat) {
                sleReferee->delField(sfReferences);
            }
            if (!sleRefer) {
                sleRefer = mEngine->entryCreate(ltREFER, referIndex);
            } else {
                mEngine->entryModify(sleRefer);
            }
            sleRefer->setFieldArray(sfReferences, references);
        }

        return tesSUCCESS;
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
