namespace ripple {

// account_dividend [account]
Json::Value doAccountDividend (RPC::Context& context)
{
    if (!context.params_.isMember ("account"))
    {
        return ripple::RPC::make_error(rpcTXN_NOT_FOUND, "accountDividendNotFound");
    }
    
    auto account = context.params_["account"].asString();
    
    Ledger::pointer ledger = getApp().getOPs().getClosedLedger();
    SLE::pointer dividendSLE = ledger->getDividendObject();
    if (dividendSLE && dividendSLE->isFieldPresent(sfDividendLedger))
    {
        std::uint32_t baseLedgerSeq = dividendSLE->getFieldU32(sfDividendLedger);
        std::string sql =
        boost::str (boost::format ("SELECT AccountTransactions.TransID FROM AccountTransactions JOIN Transactions "
                                   "ON AccountTransactions.TransID=Transactions.TransID "
                                   "WHERE Account='%s' AND AccountTransactions.LedgerSeq>%d "
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
            return txn->getJson(0);
        }
    }
    
    return rpcError (rpcTXN_NOT_FOUND);
}

} // ripple
