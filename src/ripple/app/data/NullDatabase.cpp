namespace ripple {
NullDatabase::NullDatabase()
    : Database("null")
{
    mDBType = Type::Null;
}
    
NullDatabase::~NullDatabase()
{
}
    
// returns true if the query went ok
bool NullDatabase::executeSQL(const char* sql, bool fail_ok)
{
    return false;
}
    
bool NullDatabase::batchStart()
{
    return false;
}

bool NullDatabase::batchCommit(bool async)
{
    return false;
}

bool NullDatabase::executeSQLBatch()
{
    return false;
}

// tells you how many rows were changed by an update or insert
std::uint64_t NullDatabase::getNumRowsAffected ()
{
    return 0;
}

// returns false if there are no results
bool NullDatabase::startIterRows (bool finalize)
{
    return false;
}

void NullDatabase::endIterRows()
{
}

// call this after you executeSQL
// will return false if there are no more rows
bool NullDatabase::getNextRow(bool finalize)
{
    return false;
}

bool NullDatabase::beginTransaction()
{
    return executeSQL("START TRANSACTION;", false);
}

bool NullDatabase::endTransaction()
{
    return executeSQL("COMMIT;", false);
}
    
bool NullDatabase::getNull (int colIndex)
{
    return false;
}

char* NullDatabase::getStr (int colIndex, std::string& retStr)
{
    return NULL;
}

std::int32_t NullDatabase::getInt (int colIndex)
{
    return 0;
}

float NullDatabase::getFloat (int colIndex)
{
    return 0;
}

bool NullDatabase::getBool (int colIndex)
{
    return false;
}

// returns amount stored in buf
int NullDatabase::getBinary (int colIndex, unsigned char* buf, int maxSize)
{
    return 0;
}

Blob NullDatabase::getBinary (int colIndex)
{
    Blob vucResult;
    return vucResult;
}

std::uint64_t NullDatabase::getBigInt (int colIndex)
{
    return 0;
}

} // ripple
