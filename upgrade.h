#pragma once

//upgrades
#include "types.h"

#include <vector>


// id and id attached
// 1 health
// 2 damage
// 3 speed
// 4 meele speed
// (1-4 standard non unique)
// 5 life steal (5 max stack (5% life steal per stack))
// 6 extra starting arrows (3 max stacks)
// 7 poisoned blade (damaged over time) (max stack of 2)
// (semi unique)
//  8 first hit sheild 
//  9 dash 
//  10 charge attack
//fully unique


class Player;






class upgrade {
private:
    upgrade_type type; //if its special it can only be given to player once
    std::string name;
    int id; //in handling area it will be given a desciption and atual effect off of the id
protected:
public:
upgrade(upgrade_type TYPE, int ID, std::string NAME);

    std::string get_name() const {return name;}
    upgrade_type get_upgrade_type() const {return type;}
    int get_id() const {return id;}
}; //for making and containing all upgrade options inside a arry for random selection 

bool check_avaliblity(int id_check,int max_allowed, std::vector<upgrade> &list);
int get_instances_owned(int id_check, std::vector<upgrade> &list);
upgrade generate_upgrade_option(std::vector<upgrade> &list);
std::string get_description(int obj_id);

void apply_upgrades(Player &player, std::vector<upgrade> &list); //need to make it unably previos and then apply new


