namespace ripple {

// ancestors [account]
Json::Value doAncestors (RPC::Context& context)
{
    if (!context.params.isMember ("account"))
    {
        return RPC::missing_field_error ("account");
    }
    Json::Value result;
    auto account = context.params["account"].asString();
    RippleAddress accountID;
    if (!accountID.setAccountID (account))
    {
        return ripple::RPC::make_error(rpcINVALID_PARAMS, "invalidAccoutParam");
    }
    Ledger::pointer ledger = getApp().getOPs().getValidatedLedger();
    RippleAddress curAccountID = accountID;
    int counter = 2000;
    while (counter-- > 0)
    {
        SLE::pointer sle = ledger->getSLEi (getAccountRootIndex (curAccountID));
        if (!sle)
            break;
        Json::Value record;
        record["account"] = curAccountID.humanAccountID();
        std::uint32_t height = sle->isFieldPresent(sfReferenceHeight) ? sle->getFieldU32(sfReferenceHeight) : 0;
        record["height"] = to_string(height);
        if (height > 0)
        {
            RippleAddress refereeAccountID = sle->getFieldAccount(sfReferee);
            record["referee"] = refereeAccountID.humanAccountID();
            curAccountID = refereeAccountID;
        }
        result.append(record);
        if (height == 0)
        {
            break;
        }
    }
    if (result == Json::nullValue)
    {
        result["account"] = accountID.humanAccountID ();
        result            = rpcError (rpcACT_NOT_FOUND, result);
    }
    return result;
}

} // ripple
