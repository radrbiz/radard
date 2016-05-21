#include <BeastConfig.h>
#include <ripple/app/tx/impl/Dividend.h>
#include <ripple/app/misc/DividendMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TER
Dividend::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto account = ctx.tx.getAccountID(sfAccount);
    if (account != zero)
    {
        JLOG(ctx.j.warning) << "Change: Bad source id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount (sfFee);
    if (!fee.native () || fee != beast::zero)
    {
        JLOG(ctx.j.warning) << "Non-zero fee";
        return temBAD_FEE;
    }
    
    if (!ctx.tx.isFieldPresent (sfDividendType))
    {
        JLOG(ctx.j.warning) << "No dividend type";
        return temBAD_DIV_TYPE;
    }
    if (!ctx.tx.isFieldPresent (sfDividendLedger))
    {
        JLOG(ctx.j.warning) << "No dividend ledger";
        return temINVALID;
    }
    if (!ctx.tx.isFieldPresent (sfDividendCoins))
    {
        JLOG(ctx.j.warning) << "No dividend coins";
        return temINVALID;
    }
    if (!ctx.tx.isFieldPresent (sfDividendCoinsVBC))
    {
        JLOG(ctx.j.warning) << "No dividend coins vbc";
        return temINVALID;
    }
    if (!ctx.tx.isFieldPresent (sfDividendVRank))
    {
        JLOG(ctx.j.warning) << "No dividend v rank";
        return temINVALID;
    }
    if (!ctx.tx.isFieldPresent (sfDividendVSprd))
    {
        JLOG(ctx.j.warning) << "No dividend v spread";
        return temINVALID;
    }

    // check if signing public key is trusted.
    auto const& dividendAccount = ctx.app.config ()[SECTION_DIVIDEND_ACCOUNT];
    std::string public_key = get<std::string> (dividendAccount, "public_key");
    if (public_key.empty())
    {
        JLOG(ctx.j.warning) << "public_key is not configured in dividend_account to check dividend transaction";
        return tefBAD_AUTH;
    }
    auto const accountPublic = parseBase58<AccountID> (public_key);
    if (!accountPublic ||
        calcAccountID (RippleAddress::createAccountPublic (ctx.tx.getSigningPubKey ())) != accountPublic)
    {
        JLOG(ctx.j.warning) << "apply: Invalid transaction (bad signature)";
        return temBAD_SIGNATURE;
    }

    if ((ctx.tx.getSequence () != 0) || ctx.tx.isFieldPresent (sfPreviousTxnID))
    {
        JLOG(ctx.j.warning) << "Bad sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

TER Dividend::preclaim (PreclaimContext const &ctx)
{
    if (ctx.view.txExists(ctx.tx.getTransactionID ()))
        return tefALREADY;

    uint8_t divType = ctx.tx.getFieldU8 (sfDividendType);

    if (divType == DividendMaster::DivType_Apply)
    {
        if (!ctx.tx.isFieldPresent (sfDestination))
        {
            JLOG(ctx.j.warning) << "No dividend destination";
            return temDST_NEEDED;
        }
        if (!ctx.tx.isFieldPresent (sfDividendCoinsVBCRank))
        {
            JLOG(ctx.j.warning) << "No dividend coins by rank";
            return temINVALID;
        }
        if (!ctx.tx.isFieldPresent (sfDividendCoinsVBCSprd))
        {
            JLOG(ctx.j.warning) << "No dividend coins by spread";
            return temINVALID;
        }

        auto const sle = ctx.view.read (keylet::dividend ());
        if (!sle || !sle->isFieldPresent (sfDividendLedger))
        {
            JLOG(ctx.j.warning) << "No dividend object or ledger seq";
            return tefBAD_LEDGER;
        }
        if (ctx.tx.getFieldU32 (sfDividendLedger) != sle->getFieldU32 (sfDividendLedger))
        {
            JLOG(ctx.j.warning) << "Dividend ledger mismatch";
            return tefBAD_LEDGER;
        }

        auto accountSLE = ctx.view.read (keylet::account (ctx.tx.getAccountID (sfDestination)));
        if (!accountSLE)
        {
            JLOG(ctx.j.warning) << "Dividend account not found";
            return tefEXCEPTION;
        }
        
        if (ctx.tx[sfDividendLedger] == accountSLE->getFieldU32 (sfDividendLedger))
        {
            JLOG(ctx.j.warning) << "Transaction has already applied";
            return tefBAD_LEDGER;
        }
    }
    else if (divType == DividendMaster::DivType_Start)
    {
        if (!ctx.tx.isFieldPresent (sfDividendHash))
        {
            JLOG(ctx.j.warning) << "No dividend result hash";
            return temINVALID;
        }
        auto const sle = ctx.view.read (keylet::dividend ());
        if (sle)
        {
            /*
            if (sle->isFieldPresent (sfDividendState) &&
                sle->getFieldU8 (sfDividendState) != DividendMaster::DivState_Done)
            {
                JLOG(ctx.j.warning) << "Dividend in progress";
                return tefBAD_LEDGER;
            }
            */
            if (sle->isFieldPresent (sfDividendLedger) &&
                sle->getFieldIndex (sfDividendLedger) >= ctx.tx.getFieldU32 (sfDividendLedger))
            {
                JLOG(ctx.j.warning) << "Dividend ledger mismatch";
                return tefBAD_LEDGER;
            }
        }
    }
    else
        return temBAD_DIV_TYPE;

    return tesSUCCESS;
}

void Dividend::preCompute ()
{
    account_ = ctx_.tx.getAccountID(sfAccount);
    assert(account_ == zero);
}

//achieve consensus on which ledger to start dividend
TER Dividend::startCalc ()
{
    auto const k = keylet::dividend();
    
    SLE::pointer dividendObject = view().peek (k);

    if (!dividendObject)
    {
        dividendObject = std::make_shared<SLE>(k);
        view().insert(dividendObject);
    }

    auto& tx=ctx_.tx;
    
    JLOG(j_.info) << "Previous dividend object: " << dividendObject->getText ();

    uint32_t dividendLedger = tx.getFieldU32 (sfDividendLedger);
    uint64_t dividendCoins = tx.getFieldU64 (sfDividendCoins);
    uint64_t dividendCoinsVBC = tx.getFieldU64 (sfDividendCoinsVBC);

    dividendObject->setFieldU8 (sfDividendState, DividendMaster::DivState_Start);
    dividendObject->setFieldU32 (sfDividendLedger, dividendLedger);
    dividendObject->setFieldU64 (sfDividendCoins, dividendCoins);
    dividendObject->setFieldU64 (sfDividendCoinsVBC, dividendCoinsVBC);
    dividendObject->setFieldU64 (sfDividendVRank, tx.getFieldU64 (sfDividendVRank));
    dividendObject->setFieldU64 (sfDividendVSprd, tx.getFieldU64 (sfDividendVSprd));
    dividendObject->setFieldH256 (sfDividendMarker, uint256 (0));
    dividendObject->setFieldH256 (sfDividendHash, tx.getFieldH256 (sfDividendHash));
    view ().update (dividendObject);
    
    auto& dm = ctx_.app.getDividendMaster ();
    dm.setDividendState (DividendMaster::DivType_Start);

    JLOG(j_.info) << "Current dividend object: " << dividendObject->getText ();

    return tesSUCCESS;
}
/*
// update shamap in ledger
bool Dividend::updateDividendMap ()
{
    // add new transaction node to shamap
    auto const dividendObj = view ().peek (keylet::dividend ());
    uint256 txID = mTxn.getTransactionID ();

    auto divValidMap = std::make_shared<SHAMap> (
        SHAMapType::TRANSACTION,
        dividendObj->getFieldH256 (sfDividendValidHash),
        getApp ().family (),
        deprecatedLogs ().journal ("SHAMap"));

    std::vector<SHAMapNodeID> nodeIDs;
    std::vector<uint256> nodeHashes;
    nodeIDs.reserve (1);
    nodeHashes.reserve (1);
    if (divValidMap->fetchRoot (dividendObj->getFieldH256 (sfDividendValidHash), nullptr))
    {
        divValidMap->getMissingNodes (nodeIDs, nodeHashes, 1, nullptr);
    }
    else
    {
        j_.debug << "dividend failed, while fetching valid map root.";
        return false;
    }

    Serializer s, s2;
    mTxn.add (s);
    STTx stpTrans (mTxn);
    stpTrans.delField (sfTxnSignature);
    stpTrans.add (s2);
    
    //auto tItem = std::make_shared<SHAMapItem> (mTxn.getSigningHash(), s2.peekData ());
    auto tItem = std::make_shared<SHAMapItem> (stpTrans.getTransactionID(), s2.peekData ());
    if (!divValidMap->addGiveItem (std::move(tItem), true, false))
    {
        JLOG(ctx.j.warning) << "dividend failed, while adding item to valid map.";
        return false;
    }
    
    dividendObj->setFieldH256 (sfDividendValidHash, divValidMap->getHash ());
    // flush validated hashmap to nodestore
    if (divValidMap->flushDirty (hotTRANSACTION_NODE, 0) == 0)
    {
        JLOG(ctx.j.warning) << "dividend object, flush dirty failed.";
        return false;
    }
    
    uint256 divFullMapHash = dividendObj->getFieldH256 (sfDividendResultHash);
    if (j_.trace)
    {
        j_.trace << "Add transaction item json: " << mTxn.getJson (0);
        j_.trace << "After updating dividend valid map hash "
                        << divValidMap->getHash ();
        j_.trace << "Dividend full map hash " << divFullMapHash;
    }

    // Finish dividend, set dividend done
    if (divFullMapHash == divValidMap->getHash ())
    {
        dividendObj->setFieldU8 (sfDividendState, DividendMaster::DivState_Done);
    }
    view ().update(dividendObj);
    
    return true;
}
*/

bool Dividend::updateDividendMap ()
{
    // planB
    Serializer s;
    STTx stpTrans (ctx_.tx);
    stpTrans.delField (sfTxnSignature);
    stpTrans.add (s);
    uint256 unsignedHash = stpTrans.getHash(HashPrefix::transactionID);
    JLOG(j_.debug) << "Dividend unsigned transaction id " << unsignedHash;
    
    SLE::pointer dividendObj = view ().peek (keylet::dividend ());
    
    dividendObj->setFieldH256 (sfDividendMarker, unsignedHash);
    view ().update(dividendObj);
    return true;
}

//apply dividend result here
TER Dividend::applyTx ()
{
    JLOG(j_.debug) << "radar: apply dividend.";
    
    auto& tx=ctx_.tx;

    const auto& account = tx.getAccountID (sfDestination);

    JLOG(j_.trace) << "des account " << account;

    uint64_t divCoinsVBC = tx.getFieldU64 (sfDividendCoinsVBC);
    uint64_t divCoins = tx.getFieldU64 (sfDividendCoins);

    auto sleAccoutModified = view ().peek (keylet::account (account));

    if (sleAccoutModified)
    {
        if (divCoinsVBC > 0)
        {
            sleAccoutModified->setFieldAmount (sfBalanceVBC,
                sleAccoutModified->getFieldAmount (sfBalanceVBC) + divCoinsVBC);
            ctx_.createVBC (divCoinsVBC);
        }
        if (divCoins > 0)
        {
            sleAccoutModified->setFieldAmount (sfBalance,
                sleAccoutModified->getFieldAmount (sfBalance) + divCoins);
            ctx_.createXRP (divCoins);
        }


        //Record VSpd, TSpd, DividendLedgerSeq
        if (tx.isFieldPresent (sfDividendLedger))
        {
            std::uint32_t divLedgerSeq = tx.getFieldU32 (sfDividendLedger);
            sleAccoutModified->setFieldU32 (sfDividendLedger, divLedgerSeq);

            if (tx.isFieldPresent (sfDividendVRank))
            {
                std::uint64_t divVRank = tx.getFieldU64 (sfDividendVRank);
                sleAccoutModified->setFieldU64 (sfDividendVRank, divVRank);
            }

            if (tx.isFieldPresent (sfDividendVSprd))
            {
                std::uint64_t divVSpd = tx.getFieldU64 (sfDividendVSprd);
                sleAccoutModified->setFieldU64 (sfDividendVSprd, divVSpd);
            }

            if (tx.isFieldPresent (sfDividendTSprd))
            {
                std::uint64_t divTSpd = tx.getFieldU64 (sfDividendTSprd);
                sleAccoutModified->setFieldU64 (sfDividendTSprd, divTSpd);
            }
        }
        view ().update(sleAccoutModified);

        JLOG(j_.trace) << "Dividend Applied:" << sleAccoutModified->getText ();
    }
    else
    {
        JLOG(j_.warning) << "Dividend account not found :" << account;
        return tefBAD_LEDGER;
    }
    if (!updateDividendMap ())
    {
        return tefFAILURE;
    }
    return tesSUCCESS;
}

//mark as we have done dividend apply
TER Dividend::doneApply ()
{
    auto const k = keylet::dividend();
    auto& tx = ctx_.tx;

    SLE::pointer dividendObject = view().peek (k);

    if (!dividendObject)
    {
        dividendObject = std::make_shared<SLE>(k);
        view().insert(dividendObject);
    }

    j_.info << "Previous dividend object: " << dividendObject->getText ();

    uint32_t dividendLedger = tx.getFieldU32 (sfDividendLedger);
    uint64_t dividendCoins = tx.getFieldU64 (sfDividendCoins);
    uint64_t dividendCoinsVBC = tx.getFieldU64 (sfDividendCoinsVBC);

    dividendObject->setFieldU8 (sfDividendState, DividendMaster::DivState_Done);
    dividendObject->setFieldU32 (sfDividendLedger, dividendLedger);
    dividendObject->setFieldU64 (sfDividendCoins, dividendCoins);
    dividendObject->setFieldU64 (sfDividendCoinsVBC, dividendCoinsVBC);
    dividendObject->setFieldU64 (sfDividendVRank, tx.getFieldU64 (sfDividendVRank));
    dividendObject->setFieldU64 (sfDividendVSprd, tx.getFieldU64 (sfDividendVSprd));
    dividendObject->setFieldH256 (sfDividendHash, tx.getFieldH256 (sfDividendHash));

    view ().update (dividendObject);

    JLOG(j_.info) << "Current dividend object: " << dividendObject->getText ();

    return tesSUCCESS;
}

TER Dividend::doApply ()
{
    if (ctx_.tx.getTxnType () == ttDIVIDEND)
    {
        uint8_t divOpType = ctx_.tx.isFieldPresent (sfDividendType) ? ctx_.tx.getFieldU8 (sfDividendType) : DividendMaster::DivType_Start;
        switch (divOpType)
        {
        case DividendMaster::DivType_Start:
        {
            return startCalc ();
        }
        case DividendMaster::DivType_Apply:
        {
            return applyTx ();
        }
        }
    }
    return temUNKNOWN;
}
}
