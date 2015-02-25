namespace ripple {

// ledger_dividend [until]
Json::Value doDividendObject (RPC::Context& context)
{
    SLE::pointer dividendSLE = nullptr;
    
    //time param specified, query from ledger before this time
    if (context.params.isMember ("until"))
    {
        auto time_value = context.params["until"];
        if (!time_value.isNumeric() || time_value.asUInt() <= 0)
        {
            return ripple::RPC::make_error(rpcINVALID_PARAMS, "dividendObjectMalformed");
        }
        auto time = time_value.asUInt();

        std::string sql =
        boost::str (boost::format (
                                   "SELECT * FROM Ledgers WHERE ClosingTime <= %u ORDER BY LedgerSeq desc LIMIT 1")
                    % time);
        {
            auto db = getApp().getLedgerDB().getDB();
            auto sl (getApp().getLedgerDB ().lock ());
            if (db->executeSQL(sql) && db->startIterRows())
            {
                std::uint32_t ledgerSeq = db->getInt("LedgerSeq");
                db->endIterRows();
                //CARL should we find a seq more pricisely?
                Ledger::pointer ledger = getApp().getOPs().getLedgerBySeq(ledgerSeq);
                dividendSLE = ledger->getDividendObject();
            }
        }
    }
    else //no time param specified, query from the lastet closed ledger
    {
        Ledger::pointer ledger = getApp().getOPs().getClosedLedger();
        dividendSLE = ledger->getDividendObject();
    }

    if (dividendSLE)
    {
        return dividendSLE->getJson(0);
    }
    else
    {
        return ripple::RPC::make_error(rpcDIVOBJ_NOT_FOUND, "dividendObjectNotFound");
    }
}

} // ripple
