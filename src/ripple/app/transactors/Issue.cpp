#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple
{
class IssueAsset
    : public Transactor
{
public:
    IssueAsset(
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor(
              txn,
              params,
              engine,
              deprecatedLogs().journal("IssueAsset"))
    {
    }

    TER doApply() override
    {
        Account const uDstAccountID(mTxn.getFieldAccount160(sfDestination));
        if (!uDstAccountID) {
            m_journal.trace << "Malformed transaction: Issue destination account not specified.";
            return temDST_NEEDED;
        } else if (mTxnAccountID == uDstAccountID) {
            m_journal.trace << "Malformed transaction: Can not issue asset to self.";
            return temDST_IS_SRC;
        }

        STAmount const saDstAmount(mTxn.getFieldAmount(sfAmount));
        if (saDstAmount <= zero) {
            m_journal.trace << "Malformed transaction: bad amount: " << saDstAmount.getFullText();
            return temBAD_AMOUNT;
        } else if (saDstAmount.getIssuer() != mTxnAccountID) {
            m_journal.trace << "Malformed transaction: bad issuer: " << saDstAmount.getFullText();
            return temBAD_ISSUER;
        }

        Currency const currency(saDstAmount.getCurrency());
        if (currency != assetCurrency()) {
            m_journal.trace << "Malformed transaction: bad currency: " << saDstAmount.getFullText();
            return temBAD_CURRENCY;
        }

        STArray const releaseSchedule(mTxn.getFieldArray(sfReleaseSchedule));
        int64_t lastExpiration = -1, lastReleaseRate = -1;
        for (const auto& releasePoint : releaseSchedule) {
            const auto& rate = releasePoint.getFieldU32(sfReleaseRate);
            if (rate <= lastReleaseRate || rate > QUALITY_ONE)
                return temBAD_RELEASE_SCHEDULE;

            const auto& expire = releasePoint.getFieldU32(sfExpiration);
            if (expire % getConfig().ASSET_INTERVAL_MIN > 0 || expire <= lastExpiration)
                return temBAD_RELEASE_SCHEDULE;

            lastExpiration = expire;
            lastReleaseRate = rate;
        }

        SLE::pointer sleAsset(mEngine->entryCache(ltASSET, getAssetIndex(mTxnAccountID, currency)));
        if (sleAsset) {
            m_journal.trace << "Asset already issued.";
            return tefCREATED;
        }

        SLE::pointer sleDst(mEngine->entryCache(ltACCOUNT_ROOT, getAccountRootIndex(uDstAccountID)));
        if (!sleDst) {
            m_journal.trace << "Delay transaction: Destination account does not exist.";
            return tecNO_DST;
        }

        sleAsset = mEngine->entryCreate(ltASSET, getAssetIndex(mTxnAccountID, currency));
        sleAsset->setFieldAmount(sfAmount, saDstAmount);
        sleAsset->setFieldAccount(sfRegularKey, uDstAccountID);
        sleAsset->setFieldArray(sfReleaseSchedule, releaseSchedule);
        
        return mEngine->view ().rippleCredit (mTxnAccountID, uDstAccountID, saDstAmount, false);
    }
};

TER transact_Issue(
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return IssueAsset(txn, params, engine).apply();
}

} // ripple
