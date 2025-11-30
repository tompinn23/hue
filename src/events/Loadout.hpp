//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     loadout data = nlohmann::json::parse(jsonString);

#pragma once

#include "common.hpp"



namespace hue {
    using nlohmann::json;

    /**
     * When written: at startup, when loading from main menu, or when switching ships, or after
     * changing the ship in Outfitting, or when docking SRV back in mothership
     */
    struct loadout {
        int64_t cargo_capacity;
        std::string event;
        fuel_capacity fuel_capacity;
        /**
         * If wanted at startup
         */
        std::optional<bool> hot;
        double hull_health;
        std::optional<int64_t> hull_value;
        /**
         * based on zero cargo, and just enough fuel for 1 jump
         */
        double max_jump_range;
        std::vector<module_element> modules;
        std::optional<int64_t> modules_value;
        int64_t rebuy;
        std::string ship;
        /**
         * Ship id number (indicates which of your ships you are in)
         */
        int64_t ship_id;
        /**
         * User-defined ship ID string
         */
        std::string ship_ident;
        /**
         * User-defined ship name
         */
        std::string ship_name;
        /**
         * Timestamp in UTC, ISO 8601
         */
        std::string timestamp;
        /**
         * Mass of Hull and Modules, excludes fuel and cargo
         */
        double unladen_mass;
    };
}

namespace hue {
    void from_json(const json & j, fuel_capacity & x);
    void to_json(json & j, const fuel_capacity & x);

    void from_json(const json & j, engineering & x);
    void to_json(json & j, const engineering & x);

    void from_json(const json & j, module_element & x);
    void to_json(json & j, const module_element & x);

    void from_json(const json & j, loadout & x);
    void to_json(json & j, const loadout & x);

    inline void from_json(const json & j, fuel_capacity& x) {
        x.main = j.at("Main").get<double>();
        x.reserve = j.at("Reserve").get<double>();
    }

    inline void to_json(json & j, const fuel_capacity & x) {
        j = json::object();
        j["Main"] = x.main;
        j["Reserve"] = x.reserve;
    }



    inline void from_json(const json & j, engineering& x) {
        x.blueprint_id = j.at("BlueprintID").get<int64_t>();
        x.blueprint_name = j.at("BlueprintName").get<std::string>();
        x.engineer = get_stack_optional<std::string>(j, "Engineer");
        x.engineer_id = j.at("EngineerID").get<int64_t>();
        x.experimental_effect = get_stack_optional<std::string>(j, "ExperimentalEffect");
        x.experimental_effect_localised = get_stack_optional<std::string>(j, "ExperimentalEffect_Localised");
        x.level = j.at("Level").get<int64_t>();
        x.modifiers = j.at("Modifiers").get<std::vector<modifier>>();
        x.quality = j.at("Quality").get<double>();
    }

    inline void to_json(json & j, const engineering & x) {
        j = json::object();
        j["BlueprintID"] = x.blueprint_id;
        j["BlueprintName"] = x.blueprint_name;
        j["Engineer"] = x.engineer;
        j["EngineerID"] = x.engineer_id;
        j["ExperimentalEffect"] = x.experimental_effect;
        j["ExperimentalEffect_Localised"] = x.experimental_effect_localised;
        j["Level"] = x.level;
        j["Modifiers"] = x.modifiers;
        j["Quality"] = x.quality;
    }

    inline void from_json(const json & j, module_element& x) {
        x.ammo_in_clip = get_stack_optional<int64_t>(j, "AmmoInClip");
        x.ammo_in_hopper = get_stack_optional<int64_t>(j, "AmmoInHopper");
        x.engineering = get_stack_optional<engineering>(j, "Engineering");
        x.health = j.at("Health").get<double>();
        x.item = j.at("Item").get<std::string>();
        x.on = j.at("On").get<bool>();
        x.priority = j.at("Priority").get<int64_t>();
        x.slot = j.at("Slot").get<std::string>();
        x.value = get_stack_optional<int64_t>(j, "Value");
    }

    inline void to_json(json & j, const module_element & x) {
        j = json::object();
        j["AmmoInClip"] = x.ammo_in_clip;
        j["AmmoInHopper"] = x.ammo_in_hopper;
        j["Engineering"] = x.engineering;
        j["Health"] = x.health;
        j["Item"] = x.item;
        j["On"] = x.on;
        j["Priority"] = x.priority;
        j["Slot"] = x.slot;
        j["Value"] = x.value;
    }

    inline void from_json(const json & j, loadout& x) {
        x.cargo_capacity = j.at("CargoCapacity").get<int64_t>();
        x.event = j.at("event").get<std::string>();
        x.fuel_capacity = j.at("FuelCapacity").get<fuel_capacity>();
        x.hot = get_stack_optional<bool>(j, "Hot");
        x.hull_health = j.at("HullHealth").get<double>();
        x.hull_value = get_stack_optional<int64_t>(j, "HullValue");
        x.max_jump_range = j.at("MaxJumpRange").get<double>();
        x.modules = j.at("Modules").get<std::vector<module_element>>();
        x.modules_value = get_stack_optional<int64_t>(j, "ModulesValue");
        x.rebuy = j.at("Rebuy").get<int64_t>();
        x.ship = j.at("Ship").get<std::string>();
        x.ship_id = j.at("ShipID").get<int64_t>();
        x.ship_ident = j.at("ShipIdent").get<std::string>();
        x.ship_name = j.at("ShipName").get<std::string>();
        x.timestamp = j.at("timestamp").get<std::string>();
        x.unladen_mass = j.at("UnladenMass").get<double>();
    }

    inline void to_json(json & j, const loadout & x) {
        j = json::object();
        j["CargoCapacity"] = x.cargo_capacity;
        j["event"] = x.event;
        j["FuelCapacity"] = x.fuel_capacity;
        j["Hot"] = x.hot;
        j["HullHealth"] = x.hull_health;
        j["HullValue"] = x.hull_value;
        j["MaxJumpRange"] = x.max_jump_range;
        j["Modules"] = x.modules;
        j["ModulesValue"] = x.modules_value;
        j["Rebuy"] = x.rebuy;
        j["Ship"] = x.ship;
        j["ShipID"] = x.ship_id;
        j["ShipIdent"] = x.ship_ident;
        j["ShipName"] = x.ship_name;
        j["timestamp"] = x.timestamp;
        j["UnladenMass"] = x.unladen_mass;
    }
}


