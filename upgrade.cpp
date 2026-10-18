#include "upgrade.h"

#include "player.h"

upgrade::upgrade(upgrade_type TYPE,int ID, std::string NAME) 
: type(TYPE),id(ID), name(NAME) {}




bool check_avaliblity(int id_check,int max_allowed, std::vector<upgrade> &list) {
    int instances = 0;
    for (auto &a : list) {
        if (a.get_id() == id_check) {
            instances ++;
        }
    }
    if (instances >= max_allowed) {
        return false;
    } else {
        return true;
    }
}


int get_instances_owned(int id_check, std::vector<upgrade> &list) {
    int amount = 0;

    for (auto &a : list) {
        if (a.get_id() == id_check) {
            amount ++;
        }
    }
    return amount;
}

upgrade generate_upgrade_option(std::vector<upgrade>& list) {
    int id;
    int max_allowed;
    upgrade_type type;
    std::string name; 

    while (true) {
        int id_roll = GetRandomValue(1, 46);

        switch (id_roll) {
            case 1 ... 10:   id = 1;  max_allowed = 9999; type = upgrade_type::STANDARD; name = "health upgrade"; break; // health
            case 11 ... 20:  id = 2;  max_allowed = 9999; type = upgrade_type::STANDARD; name = "damage upgrade"; break; // damage
            case 21 ... 30:  id = 3;  max_allowed = 9999; type = upgrade_type::STANDARD; name = "speed upgrade"; break; // speed
            case 31 ... 40:  id = 4;  max_allowed = 9999; type = upgrade_type::STANDARD; name = "meele speed upgrade"; break; // melee speed
            case 41:         id = 5;  max_allowed = 5;    type = upgrade_type::STANDARD; name = "life steal"; break;
            case 42:         id = 6;  max_allowed = 3;    type = upgrade_type::STANDARD; name = "starting arrow"; break;
            case 43:         id = 7;  max_allowed = 2;    type = upgrade_type::STANDARD; name = "poisoned blade"; break;
            case 44:         id = 8;  max_allowed = 1;    type = upgrade_type::SPECIAL;  name = "first hit sheild"; break;
            case 45:         id = 9;  max_allowed = 1;    type = upgrade_type::SPECIAL;  name = "dash"; break;
            case 46:         id = 10; max_allowed = 1;    type = upgrade_type::SPECIAL;  name = "charge attack"; break;
        }

        if (check_avaliblity(id, max_allowed, list)) break;
    }

    return upgrade(type, id, name);
}

std::string get_description(int obj_id) {
switch (obj_id) {
    case 1:
    return "simply upgrades how much health your character has";
    break;
    case 2:
    return "mildly increases how much damage your characters meeles and ranged attacks dose";
    break;
    case 3:
    return "increases how fast your character can move across the map";
    break;
    case 4:
    return "decreases the cool down after each attack";
    break;
    case 5:
    return "gives a slight life steal allowing you to heal from damaging enemy";
    break;
    case 6:
    return "grants a arrow when the round begins for free";
    break;
    case 7:
    return "causes your meele to do extra damage over time";
    break;
    case 8:
    return "allows you to not take any damge from the first attack you take during the round";
    break;
    case 9:
    return "allows your character to dash using Q";
    break;
    case 10:
    return "allows your character to charge attack by holding the attack button";
    break;
}
}

void apply_upgrades(Player &player, std::vector<upgrade> &list) {
player.reset_mults();
player.apply_mults();
for (auto &a : list) {
    if (a.get_id() == 1) {
        player.add_health_mult(15);
    } else if (a.get_id() == 2) {
        player.add_damage_mult(1);
    } else if (a.get_id() == 3) {
        player.add_speed_mult(0.15f);
    } else if (a.get_id() == 4) {
        player.add_meele_speed_mult(0.15f);
    }
}
player.apply_mults();
}