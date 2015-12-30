namespace ripple {

// ledger_dividend [until]
Json::Value doDividendObject (RPC::Context& context)
{
    std::shared_ptr<SLE const> dividendSLE = nullptr;
    
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
            auto db = context.app.getLedgerDB ().checkoutDb ();
            boost::optional<std::uint64_t> ledgerSeq64;
            *db << sql, soci::into (ledgerSeq64);
            if (db->got_data ())
            {
                uint32_t ledgerSeq =
                    rangeCheckedCast<std::uint32_t> (ledgerSeq64.value_or (0));
                //CARL should we find a seq more pricisely?
                Ledger::pointer ledger = context.ledgerMaster.getLedgerBySeq (ledgerSeq);
                dividendSLE = ledger->read (keylet::dividend ());
            }
        }
    }
    else //no time param specified, query from the lastet closed ledger
    {
        Ledger::pointer ledger = context.ledgerMaster.getValidatedLedger ();
        dividendSLE = ledger->read (keylet::dividend ());
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
