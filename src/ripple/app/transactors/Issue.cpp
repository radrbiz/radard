#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

class IssueAsset
    : public Transactor
{
public:
    IssueAsset(
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("IssueAsset"))
    {

    }

    TER doApply () override
    {
        STAmount amount (mTxn.getFieldAmount (sfAmount));
        STArray const releaseSchedule (mTxn.getFieldArray (sfReleaseSchedule));
        
        int64_t lastExpiration = -1;
        uint32_t sumReleaseRate = 0;
        
        for (const auto& releasePoint: releaseSchedule)
        {
            const auto& expire = releasePoint.getFieldU32(sfExpiration);
            
            if (expire <= lastExpiration)
                return temINVALID;
            
            const auto& rate = releasePoint.getFieldU32(sfReleaseRate);
            
            if (rate > QUALITY_ONE)
                return temINVALID;
            
            lastExpiration = expire;
            sumReleaseRate += rate;
            
            if (sumReleaseRate > QUALITY_ONE)
                return temINVALID;
        }
        
        
        if (amount.getIssuer() == mTxnAccountID)
        {
            const auto& naSeed = RippleAddress::createSeedRandom();
            const auto& naGenerator = RippleAddress::createGeneratorPublic (naSeed);
            const auto& naAccount = RippleAddress::createAccountPublic (naGenerator, 0);
            amount.setIssuer(naAccount.getAccountID());
        }

        return tesSUCCESS;
    }
};

TER
transact_Issue (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return IssueAsset(txn, params, engine).apply();
}

}  // ripple
