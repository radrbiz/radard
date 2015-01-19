namespace ripple {

// ancestors [account]
Json::Value doAncestors (RPC::Context& context)
{
    if (!context.params_.isMember ("account"))
    {
        return ripple::RPC::make_error(rpcACT_NOT_FOUND, "ancestorsNotFound");
    }
    Json::Value result;
    auto account = context.params_["account"].asString();
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
        Json::Value record;
        record["account"] = curAccountID.humanAccountID();
        SLE::pointer sle = ledger->getSLEi (Ledger::getAccountRootIndex (curAccountID));
        std::uint32_t height = sle->getFieldU32(sfReferenceHeight);
        record["height"] = to_string(height);
        RippleAddress refereeAccountID;
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
    return result;
}

} // ripple
