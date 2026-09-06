#include <cstdio>
#include <string>
#include <vector>
#include <sqlite3.h>

#include "creatures.h"
#include "errors.h"
#include "states.h"



// File-scope handle: opened once, used by every creature_* function below.
static sqlite3* db = nullptr;

static const char* kSchema =
    "CREATE TABLE IF NOT EXISTS creatures ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name       TEXT    NOT NULL UNIQUE,"
    "  pid        INTEGER,"
    "  mem_limit  INTEGER NOT NULL,"
    "  status     TEXT    NOT NULL DEFAULT 'ALIVE'"
    "             CHECK (status IN ('ALIVE','SLEEPING','RELEASED'))"
    ");";

// Open (creating the file if missing) and ensure the schema exists.
bool db_open(const char* path)
{
    if (sqlite3_open(path, &db) != SQLITE_OK) { // creates database connection and the database file if not created
        fprintf(stderr, "db_open: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);   // handle can be non-null even on failure
        db = nullptr;
        return false;
    }

    char* err = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) { // create the creature table if it doesn't exist inside the database file
        fprintf(stderr, "db_open schema: %s\n", err);
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    return true;
}

// Called with done with database for the process (parent)
// However - data is still stored on disk; just the connection is closed
void db_close()
{
    sqlite3_close(db); 
    db = nullptr;
}

// Insert a new creature with default statue = 'ALIVE'
// Returns the new row id or -1 (if failed)
int creature_insert(const std::string& name, int mem_limit_mb)
{
    const char* sql = "INSERT INTO creatures(name, mem_limit) VALUES(?, ?)";
    sqlite3_stmt* st = nullptr; //stores pointer here


    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) { // compiles char into sqlite3_stmt obj
        fprintf(stderr, "creature_insert prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT); //put vals in the placeholder
    sqlite3_bind_int (st, 2, mem_limit_mb);

    int rc = sqlite3_step(st); // execute the statement - insert the row in the db with the name and mem limit
    sqlite3_finalize(st); // free the compiled object; data sits untouched in the db file

    if (rc != SQLITE_DONE) {
        // SQLITE_CONSTRAINT here means the UNIQUE name already exists.
        fprintf(stderr, "creature_insert step: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return (int)sqlite3_last_insert_rowid(db); // returns rowid of the latest creature added to the db
}

// TODO: bool creature_set_pid(int id, int pid)
//   UPDATE creatures SET pid = ? WHERE id = ?
//   prepare -> bind_int(1,pid) -> bind_int(2,id) -> step -> finalize
//   return sqlite3_step(...) == SQLITE_DONE
bool creature_set_pid(int id, int pid)
{
    const char* sql = "UPDATE creatures SET pid = ? WHERE id = ?";
    sqlite3_stmt* st = nullptr; //stores pointer here

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) { // compiles char into sqlite3_stmt obj
        fprintf(stderr, "creature_set_pid prepare: %s\n", sqlite3_errmsg(db));
        return false;
    }

    // fills in the values into the compiled object
    sqlite3_bind_int(st, 1, pid); 
    sqlite3_bind_int(st, 2, id);  

    int rc = sqlite3_step(st); // execute the statement
    sqlite3_finalize(st); // free the compiled object; data sits untouched in the db file

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "creature_set_pid step: %s\n", sqlite3_errmsg(db));
        return false;
    }
    return true;

}

bool creature_set_status(int id, const std::string& status)
{
    const char * sql = "UPDATE creatures SET status = ? WHERE id = ?";
    sqlite3_stmt * st = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    {
        fprintf(stderr, "creature_set_status prepare: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, id);

    int rc = sqlite3_step(st); // execute the statement
    sqlite3_finalize(st); // free the compiled object; data sits untouched in the db file

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "creature_set_status step: %s\n", sqlite3_errmsg(db));
        return false;
    }
    return true;

}

std::vector<Creature> creature_list()
{
    std::vector<Creature> cl;
    const char * sql = "SELECT id, name, pid, mem_limit, status FROM creatures ORDER BY id";
    sqlite3_stmt * st = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    {
        fprintf(stderr, "creature_set_status prepare: %s\n", sqlite3_errmsg(db));
        return cl;
    }

    while (sqlite3_step(st) == SQLITE_ROW) //st holds the current row
    {
        Creature c;
        c.id = sqlite3_column_int(st, 0);
        c.name = (const char*)sqlite3_column_text(st, 1);
        c.pid = sqlite3_column_int(st, 2);
        c.mem_limit_mb = sqlite3_column_int(st, 3);
        c.status = (const char*)sqlite3_column_text(st, 4);

        cl.push_back(c);
    }

    sqlite3_finalize(st); // free the compiled object; data sits untouched in the db file

    return cl;
}

