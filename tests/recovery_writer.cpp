#include <chrono>
#include <iostream>
#include <sqlite3.h>
#include <thread>

// A separate process deliberately dies with an active SQLite transaction. It
// creates a hot-journal fixture without exposing a crash hook in the application.
// The marker uses an existing table: workspaces refuse unknown schema objects.
int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    sqlite3* db = nullptr;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK)
        return 3;
    const char* changes = "PRAGMA foreign_keys=ON; BEGIN IMMEDIATE;"
                          "CREATE TEMP TABLE original_blocks AS SELECT * FROM blocks;"
                          "UPDATE pages SET title='After process exit',revision=revision+1;"
                          "DELETE FROM blocks; INSERT INTO blocks SELECT * FROM original_blocks;"
                          "INSERT INTO maintenance(name,value) VALUES('interrupted_marker','1');";
    if (sqlite3_exec(db, changes, nullptr, nullptr, nullptr) != SQLITE_OK)
        return 4;
    if (std::string(argv[2]) == "commit" && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
        return 5;
    std::cout << "ready\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(20));
    sqlite3_close(db);
    return 0;
}
