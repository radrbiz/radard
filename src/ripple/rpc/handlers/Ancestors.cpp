namespace ripple {
// {
//   account: <indent>,
//   strict: <bool>
//           if true, only allow public keys and addresses. false, default.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
// ancestors [account]
Json::Value doAncestors (RPC::Context& context)
{
    auto& params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);

    if (!ledger)
        return result;

    if (!params.isMember (jss::account) && !params.isMember (jss::ident))
        return RPC::missing_field_error (jss::account);
    
    auto account = context.params[jss::account].asString ();
    std::string strIdent = params.isMember (jss::account) ? params[jss::account].asString () : params[jss::ident].asString ();
    bool bStrict = params.isMember (jss::strict) && params[jss::strict].asBool ();
    AccountID accountID;

    auto jvAccepted = RPC::accountFromString (accountID, strIdent, bStrict);

    if (jvAccepted)
        return jvAccepted;

    auto& message = result[jss::message];
    AccountID curAccountID = accountID;
    for (int counter = 0; counter < 2000; ++counter)
    {
        if (!curAccountID)
            break;
        auto sle = ledger->read (keylet::account (curAccountID));
        if (!sle)
            break;
        Json::Value& record (message.append (Json::objectValue));
        record[jss::account] = context.app.accountIDCache().toBase58 (curAccountID);
        AccountID refereeAccountID = sle->getAccountID(sfReferee);
        if (refereeAccountID.isNonZero ())
            record[jss::referee] = context.app.accountIDCache().toBase58 (refereeAccountID);
        curAccountID = refereeAccountID;
    }
    if (result == Json::nullValue)
    {
        result[jss::account] = context.app.accountIDCache ().toBase58 (accountID);
        result = rpcError (rpcACT_NOT_FOUND, result);
    }
    return result;
}

} // ripple
