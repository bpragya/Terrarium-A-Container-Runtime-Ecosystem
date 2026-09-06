#pragma once
#include <string>
#include <vector>

struct Creature {
    int         id;
    std::string name;
    int         mem_limit_mb;
    int         pid;          // 0 until the child is cloned
    std::string status;       // one of state:: (states.h)
};

bool db_open(const char* path);   // open + create schema if missing
void db_close();                  // close the database

int  creature_insert(const std::string& name, int mem_limit_mb);  // new row id, -1 on fail
bool creature_set_pid(int id, int pid);
bool creature_set_status(int id, const std::string& status);
std::vector<Creature> creature_list();
