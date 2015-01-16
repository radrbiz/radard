namespace ripple {

// account_dividend [account]
Json::Value doAccountDividend (RPC::Context& context)
{
    if (!context.params_.isMember ("account"))
    {
        return ripple::RPC::make_error(rpcTXN_NOT_FOUND, "accountDividendNotFound");
    }
    Json::Value result;
    auto account = context.params_["account"].asString();
    result["Account"] = account;
    
    Ledger::pointer ledger = getApp().getOPs().getClosedLedger();
    SLE::pointer dividendSLE = ledger->getDividendObject();
    if (dividendSLE && dividendSLE->isFieldPresent(sfDividendLedger))
    {
        std::uint32_t baseLedgerSeq = dividendSLE->getFieldU32(sfDividendLedger);
        std::string sql =
        boost::str (boost::format ("SELECT AccountTransactions.TransID FROM AccountTransactions JOIN Transactions "
                                   "ON AccountTransactions.TransID=Transactions.TransID "
                                   "WHERE Account='%s' AND AccountTransactions.LedgerSeq>%d "
                                   "AND TransType='Dividend' "
                                   "ORDER BY AccountTransactions.LedgerSeq ASC LIMIT 1;")
                    % account.c_str()
                    % baseLedgerSeq);
        Transaction::pointer txn = nullptr;
        {
            auto db = getApp().getTxnDB().getDB();
            auto sl (getApp().getTxnDB().lock());
            if (db->executeSQL(sql) && db->startIterRows())
            {
                std::string transID = "";
                db->getStr(0, transID);
                {
                    uint256 txid (transID);
                    txn = getApp().getMasterTransaction ().fetch (txid, true);
                }
            }
        }
        if (txn)
        {
            result["DividendCoins"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendCoins));
            result["DividendCoinsVBC"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendCoinsVBC));
            result["DividendCoinsVBCRank"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendCoinsVBCRank));
            result["DividendCoinsVBCSprd"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendCoinsVBCSprd));
            result["DividendTSprd"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendTSprd));
            result["DividendVRank"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendVRank));
            result["DividendVSprd"] = to_string(txn->getSTransaction()->getFieldU64(sfDividendVSprd));
            result["DividendLedger"] = to_string(txn->getSTransaction()->getFieldU32(sfDividendLedger));

            return result;
        }
    }
    result["DividendCoins"] = "0";
    result["DividendCoinsVBC"] = "0";
    result["DividendCoinsVBCRank"] = "0";
    result["DividendCoinsVBCSprd"] = "0";
    result["DividendTSprd"] = "0";
    result["DividendVRank"] = "0";
    result["DividendVSprd"] = "0";
    result["DividendLedger"] = "0";
    return result;
}

} // ripple
