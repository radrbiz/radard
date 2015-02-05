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
    mDBType = Type::MySQL;
}
    
MySQLDatabase::~MySQLDatabase()
{
}
    
// returns true if the query went ok
bool MySQLDatabase::executeSQL(const char* sql, bool fail_ok)
{
    auto stmt = getStatement();
    if (stmt->mInBatch)
    {
        stmt->mSqlQueue.push_back(sql);
        return true;
    }
    
    assert(stmt->mConnection);
    endIterRows();
    int rc = mysql_query(stmt->mConnection, sql);
    if (rc != 0)
    {
        WriteLog (lsWARNING, MySQLDatabase)
            << "executeSQL-" << sql
            << " error_info:" << mysql_error(stmt->mConnection);
        return false;
    }
    //SQL has result(like `select`)
    if (mysql_field_count(stmt->mConnection) > 0)
    {
        stmt->mResult = mysql_store_result(stmt->mConnection);
        if (stmt->mResult == nullptr)
        {
            WriteLog (lsWARNING, MySQLDatabase)
                << "startIterRows: " << mysql_error(stmt->mConnection);
            return false;
        }
        if (mysql_num_rows(stmt->mResult) > 0)
        {
            stmt->mMoreRows = true;
        }
        else
        {
            stmt->mMoreRows = false;
        }
    }
    return true;
}
    
bool MySQLDatabase::batchStart()
{
    auto stmt = getStatement();
    stmt->mInBatch = true;
    stmt->mSqlQueue.clear();
    return true;
}

bool MySQLDatabase::batchCommit(bool async)
{
    auto stmt = getStatement();
    if (!stmt->mInBatch)
    {
        return false;
    }
    stmt->mInBatch = false;
    if (async)
    {
        std::unique_lock <std::mutex> lock (mThreadBatchLock);
        std::move(stmt->mSqlQueue.begin(), stmt->mSqlQueue.end(), std::back_inserter(mSqlQueue));
        if (!mThreadBatch && !mSqlQueue.empty ())
        {
            mThreadBatch = true;
            getApp().getJobQueue().addJob(jtDB_BATCH,
                                          "dbBatch",
                                          std::bind(&MySQLDatabase::executeSQLBatch, this));
        }
    }
    else
    {
        std::move(stmt->mSqlQueue.begin(), stmt->mSqlQueue.end(), std::back_inserter(mSqlQueue));
        executeSQLBatch();
    }
    return true;
}

bool MySQLDatabase::executeSQLBatch()
{
    std::unique_lock <std::mutex> lock (mThreadBatchLock);
    while (!mSqlQueue.empty())
    {
        std::string sql = mSqlQueue.front();
        mSqlQueue.pop_front();
        lock.unlock();
        executeSQL(sql.c_str(), true);
        lock.lock();
    }
    mThreadBatch = false;
    return true;
}

// tells you how many rows were changed by an update or insert
std::uint64_t MySQLDatabase::getNumRowsAffected ()
{
    return mysql_affected_rows(getStatement()->mConnection);
}

// returns false if there are no results
bool MySQLDatabase::startIterRows (bool finalize)
{
    auto stmt = getStatement();
    if (!stmt->mMoreRows)
    {
        endIterRows ();
        return false;
    }
    stmt->mColNameTable.clear();
    auto fieldCnt = mysql_num_fields(stmt->mResult);
    stmt->mColNameTable.resize(fieldCnt);
    
    auto fields = mysql_fetch_fields(stmt->mResult);
    for (auto i = 0; i < fieldCnt; i++)
    {
        stmt->mColNameTable[i] = fields[i].name;
    }
    
    getNextRow(0);
    
    return true;
}
    
bool MySQLDatabase::getColNumber (const char* colName, int* retIndex)
{
    auto stmt = getStatement();
    for (unsigned int n = 0; n < stmt->mColNameTable.size (); n++)
    {
        if (strcmp (colName, stmt->mColNameTable[n].c_str ()) == 0)
        {
            *retIndex = n;
            return (true);
        }
    }
    return false;

}

void MySQLDatabase::endIterRows()
{
    auto stmt = getStatement();
    if (stmt->mResult)
    {
        mysql_free_result(stmt->mResult);
        stmt->mResult = nullptr;
    }
    stmt->mMoreRows = false;
    stmt->mColNameTable.clear();
    stmt->mResult = nullptr;
    stmt->mCurRow = nullptr;
}

// call this after you executeSQL
// will return false if there are no more rows
bool MySQLDatabase::getNextRow(bool finalize)
{
    auto stmt = getStatement();
    if (stmt->mMoreRows)
    {
        stmt->mCurRow = mysql_fetch_row(stmt->mResult);
        if (stmt->mCurRow)
        {
            return stmt->mCurRow;
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
    auto stmt = getStatement();
    return stmt->mCurRow[colIndex] == nullptr;
}

char* MySQLDatabase::getStr (int colIndex, std::string& retStr)
{
    auto stmt = getStatement();
    const char* text = reinterpret_cast<const char*> (stmt->mCurRow[colIndex]);
    retStr = (text == nullptr) ? "" : text;
    return const_cast<char*> (retStr.c_str ());
}

std::int32_t MySQLDatabase::getInt (int colIndex)
{
    auto stmt = getStatement();
    return boost::lexical_cast<std::int32_t>(stmt->mCurRow[colIndex]);
}

float MySQLDatabase::getFloat (int colIndex)
{
    auto stmt = getStatement();
    return boost::lexical_cast<float>(stmt->mCurRow[colIndex]);
}

bool MySQLDatabase::getBool (int colIndex)
{
    auto stmt = getStatement();
    return stmt->mCurRow[colIndex][0] != '0';
}

// returns amount stored in buf
int MySQLDatabase::getBinary (int colIndex, unsigned char* buf, int maxSize)
{
    auto stmt = getStatement();
    auto colLength = mysql_fetch_lengths(stmt->mResult);
    auto copySize = colLength[colIndex];
    if (copySize < maxSize)
    {
        maxSize = static_cast<int>(copySize);
    }
    memcpy(buf, stmt->mCurRow[colIndex], maxSize);
    return static_cast<int>(copySize);
}

Blob MySQLDatabase::getBinary (int colIndex)
{
    auto stmt = getStatement();
    auto colLength = mysql_fetch_lengths(stmt->mResult);
    const unsigned char* blob = reinterpret_cast<const unsigned char*>(stmt->mCurRow[colIndex]);
    size_t iSize = colLength[colIndex];
    Blob vucResult;
    vucResult.resize(iSize);
    std::copy (blob, blob + iSize, vucResult.begin());
    return vucResult;
}

std::uint64_t MySQLDatabase::getBigInt (int colIndex)
{
    auto stmt = getStatement();
    return boost::lexical_cast<std::int32_t>(stmt->mCurRow[colIndex]);
}

MySQLStatement* MySQLDatabase::getStatement()
{
    auto stmt = mStmt.get();
    if (!stmt)
    {
        stmt = new MySQLStatement(mHost.c_str(), mPort, mUsername.c_str(), mPassword.c_str(), mDatabase.c_str());
        mStmt.reset(stmt);
    }
    return stmt;
}
    
//---------------------------------------------------
MySQLStatement::MySQLStatement(char const* host, std::uint32_t port, char const* username, char const* password, char const* database)
{
    mConnection = mysql_init(nullptr);
    if (!mConnection || mysql_real_connect(mConnection, host,
                                           username, password,
                                           database, port, NULL, CLIENT_MULTI_STATEMENTS) == nullptr)
    {
        WriteLog (lsFATAL, MySQLDatabase)
            << "connect fail: host-" << host
            << " port-" << port
            << " database:" << database
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
    
    mInBatch = false;

    mMoreRows = false;
    mResult = nullptr;
    mCurRow = nullptr;
}

MySQLStatement::~MySQLStatement()
{
    if (mResult)
    {
        mysql_free_result(mResult);
    }

    if (mConnection)
    {
        mysql_close(mConnection);
    }
}
    
MySQLDatabaseCon::MySQLDatabaseCon(beast::StringPairArray& params, const char* initStrings[], int initCount)
{
    assert(params[beast::String("type")] == "mysql"
           && params[beast::String("host")] != beast::String::empty
           && params[beast::String("port")] != beast::String::empty
           && params[beast::String("username")] != beast::String::empty
           && params[beast::String("password")] != beast::String::empty
           && params[beast::String("database")] != beast::String::empty);
    
    std::string host = params[beast::String("host")].toStdString();
    int port = boost::lexical_cast<int>(params[beast::String("port")].toStdString());
    std::string username = params[beast::String("username")].toStdString();
    std::string password = params[beast::String("password")].toStdString();
    std::string database = params[beast::String("database")].toStdString();
    
    mDatabase = new MySQLDatabase(host.c_str(), port, username.c_str(), password.c_str(), database.c_str());
    mDatabase->connect ();
    
    for (int i = 0; i < initCount; ++i)
        mDatabase->executeSQL (initStrings[i], true);
}

} // ripple
