#include <mysql.h>

namespace ripple {
MySQLDatabase::MySQLDatabase(char const* host, std::uint32_t port,
                             char const* username, char const* password,
                             char const* database)
    : Database (host),
      mPort(port),
      mUsername(username),
      mPassword(password),
      mDatabase(database)
{
    mDBType = "MySQL";
    mStmtTemp = nullptr;
    mCurrentStmt = nullptr;
}
    
MySQLDatabase::~MySQLDatabase()
{
    if (mStmtTemp)
    {
        delete mStmtTemp;
    }
}
    
void MySQLDatabase::connect()
{
    mConnection = mysql_init(nullptr);
    if (!mConnection || mysql_real_connect(mConnection, mHost.c_str(),
                                           mUsername.c_str(), mPassword.c_str(),
                                           mDatabase.c_str(), mPort, NULL, 0) == nullptr)
    {
        WriteLog (lsFATAL, MySQLDatabase)
            << "connect fail: host-" << mHost
            << " port-" << mPort
            << " database:" << mDatabase
            << " error_info:" << mysql_error(mConnection);
        if (mConnection)
        {
            mysql_close(mConnection);
            mConnection = nullptr;
        }
        assert(false);
    }
    // set auto connect
    my_bool reconnect = 1;
    mysql_options(mConnection, MYSQL_OPT_RECONNECT, &reconnect);
}

void MySQLDatabase::disconnect()
{
    if (mConnection)
    {
        mysql_close(mConnection);
        mConnection = nullptr;
    }
}
    
// returns true if the query went ok
bool MySQLDatabase::executeSQL(const char* sql, bool fail_ok)
{
    assert(mConnection && (mCurrentStmt == nullptr));
    int rc = mysql_query(mConnection, sql);
    if (rc != 0)
    {
        WriteLog (lsWARNING, MySQLDatabase)
            << "executeSQL-" << sql
            << " error_info:" << mysql_error(mConnection);
        return false;
    }
    
    //SQL has result(like `select`)
    if (mysql_field_count(mConnection) > 0)
    {
        mCurrentStmt = getStatement();
        assert(mCurrentStmt);
        mCurrentStmt->mResult = mysql_store_result(mConnection);
        if (mCurrentStmt->mResult == nullptr)
        {
            WriteLog (lsWARNING, MySQLDatabase)
                << "startIterRows: " << mysql_error(mConnection);
            return false;
        }
        if (mysql_num_rows(mCurrentStmt->mResult) > 0)
        {
            mCurrentStmt->mMoreRows = true;
        }
        else
        {
            mCurrentStmt->mMoreRows = false;
        }
    }
    return true;
}

// tells you how many rows were changed by an update or insert
std::uint64_t MySQLDatabase::getNumRowsAffected ()
{
    return mysql_affected_rows(mConnection);
}

// returns false if there are no results
bool MySQLDatabase::startIterRows (bool finalize)
{
    assert(mCurrentStmt);
    if (!mCurrentStmt->mMoreRows)
    {
        endIterRows ();
        return false;
    }
    mCurrentStmt->mColNameTable.clear();
    auto fieldCnt = mysql_num_fields(mCurrentStmt->mResult);
    mCurrentStmt->mColNameTable.resize(fieldCnt);
    
    auto fields = mysql_fetch_fields(mCurrentStmt->mResult);
    for (auto i = 0; i < fieldCnt; i++)
    {
        mCurrentStmt->mColNameTable[i] = fields[i].name;
    }
    
    getNextRow(0);
    
    return true;
}
    
bool MySQLDatabase::getColNumber (const char* colName, int* retIndex)
{
    for (unsigned int n = 0; n < mCurrentStmt->mColNameTable.size (); n++)
    {
        if (strcmp (colName, mCurrentStmt->mColNameTable[n].c_str ()) == 0)
        {
            *retIndex = n;
            return (true);
        }
    }
    return false;

}

void MySQLDatabase::endIterRows()
{
    if (mCurrentStmt && mCurrentStmt->mResult)
    {
        mysql_free_result(mCurrentStmt->mResult);
    }
}

// call this after you executeSQL
// will return false if there are no more rows
bool MySQLDatabase::getNextRow(bool finalize)
{
    assert(mCurrentStmt);
    if (mCurrentStmt->mMoreRows)
    {
        mCurrentStmt->mCurRow = mysql_fetch_row(mCurrentStmt->mResult);
        if (mCurrentStmt->mCurRow)
        {
            return mCurrentStmt->mCurRow;
        }
    }
    if (finalize)
    {
        endIterRows();
    }
    return false;
}

bool MySQLDatabase::beginTransaction()
{
    return executeSQL("START TRANSACTION;", false);
}

bool MySQLDatabase::endTransaction()
{
    return executeSQL("COMMIT;", false);
}
    
bool MySQLDatabase::getNull (int colIndex)
{
    return mCurrentStmt->mCurRow[colIndex] == nullptr;
}

char* MySQLDatabase::getStr (int colIndex, std::string& retStr)
{
    const char* text = reinterpret_cast<const char*> (mCurrentStmt->mCurRow[colIndex]);
    retStr = (text == nullptr) ? "" : text;
    return const_cast<char*> (retStr.c_str ());
}

std::int32_t MySQLDatabase::getInt (int colIndex)
{
    return boost::lexical_cast<std::int32_t>(mCurrentStmt->mCurRow[colIndex]);
}

float MySQLDatabase::getFloat (int colIndex)
{
    return boost::lexical_cast<float>(mCurrentStmt->mCurRow[colIndex]);
}

bool MySQLDatabase::getBool (int colIndex)
{
    return mCurrentStmt->mCurRow[colIndex][0] != '0';
}

// returns amount stored in buf
int MySQLDatabase::getBinary (int colIndex, unsigned char* buf, int maxSize)
{
    auto colLength = mysql_fetch_lengths(mCurrentStmt->mResult);
    auto copySize = colLength[colIndex];
    if (copySize < maxSize)
    {
        maxSize = static_cast<int>(copySize);
    }
    memcpy(buf, mCurrentStmt->mCurRow[colIndex], maxSize);
    return static_cast<int>(copySize);
}

Blob MySQLDatabase::getBinary (int colIndex)
{
    auto colLength = mysql_fetch_lengths(mCurrentStmt->mResult);
    const unsigned char* blob = reinterpret_cast<const unsigned char*>(mCurrentStmt->mCurRow[colIndex]);
    size_t iSize = colLength[colIndex];
    Blob vucResult;
    vucResult.resize(iSize);
    std::copy (blob, blob + iSize, vucResult.begin());
    return vucResult;
}

std::uint64_t MySQLDatabase::getBigInt (int colIndex)
{
    return boost::lexical_cast<std::int32_t>(mCurrentStmt->mCurRow[colIndex]);
}

MySQLStatement* MySQLDatabase::getStatement()
{
    if (mStmtTemp == nullptr)
    {
        mStmtTemp = new MySQLStatement();
    }
    mStmtTemp->mDatabase = this;
    mStmtTemp->mMoreRows = false;
    mStmtTemp->mColNameTable.clear();
    mStmtTemp->mResult = nullptr;
    mStmtTemp->mCurRow = nullptr;
    return mStmtTemp;
}
    
//---------------------------------------------------
MySQLStatement::MySQLStatement()
{
}

MySQLStatement::~MySQLStatement()
{
}
        
} // ripple
