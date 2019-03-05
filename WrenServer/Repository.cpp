#include <sqlite3.h>
#include <string>
#include "Account.h"
#include "Repository.h"

using namespace std;

constexpr auto DB_NAME = "Wren.db";

constexpr auto FAILED_TO_OPEN = "Failed to open database.";
constexpr auto FAILED_TO_PREPARE = "Failed to prepare SQLite statement.";
constexpr auto FAILED_TO_EXECUTE = "Failed to execute statement.";

constexpr auto ACCOUNT_EXISTS_QUERY = "SELECT id FROM Accounts WHERE account_name = '%s' LIMIT 1;";
constexpr auto CREATE_ACCOUNT_QUERY = "INSERT INTO Accounts (account_name, hashed_password) VALUES('%s', '%s');";
constexpr auto GET_ACCOUNT_QUERY = "SELECT * FROM Accounts WHERE account_name = '%s' LIMIT 1;";

bool Repository::AccountExists(const string& accountName)
{
    auto dbConnection = GetConnection();

    char query[100];
    sprintf_s(query, ACCOUNT_EXISTS_QUERY, accountName.c_str());
        
    auto statement = PrepareStatement(dbConnection, query);

    const auto result = sqlite3_step(statement);
    if (result == SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return true;
    }
    else if (result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return false;
    }
    else
    {
        sqlite3_finalize(statement);
        throw exception(FAILED_TO_EXECUTE);
    }
}

void Repository::CreateAccount(const string& accountName, const string& password)
{
    auto dbConnection = GetConnection();

    char query[300];
    sprintf_s(query, CREATE_ACCOUNT_QUERY, accountName.c_str(), password.c_str());

    auto statement = PrepareStatement(dbConnection, query);

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        throw exception(FAILED_TO_EXECUTE);
    }
}

Account* Repository::GetAccount(const string& accountName)
{
    auto dbConnection = GetConnection();
                
    char query[100];
    sprintf_s(query, GET_ACCOUNT_QUERY, accountName.c_str());

    auto statement = PrepareStatement(dbConnection, query);

    const auto result = sqlite3_step(statement);
    if (result == SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        const int id = sqlite3_column_int(statement, 0);
        const unsigned char *hashedPassword = sqlite3_column_text(statement, 2);
        return new Account(id, accountName, string(reinterpret_cast<const char*>(hashedPassword)));
    }
    else if (result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return nullptr;
    }
    else
    {
        sqlite3_finalize(statement);
        throw exception(FAILED_TO_EXECUTE);
    }
}

sqlite3* Repository::GetConnection()
{
    sqlite3* dbConnection;
    if (sqlite3_open(DB_NAME, &dbConnection) != SQLITE_OK)
        throw exception(FAILED_TO_OPEN);
    return dbConnection;
}

sqlite3_stmt* Repository::PrepareStatement(sqlite3* dbConnection, const char *query)
{
    sqlite3_stmt* statement;
    if (sqlite3_prepare_v2(dbConnection, query, -1, &statement, NULL) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        throw exception(FAILED_TO_PREPARE);
    }
}
