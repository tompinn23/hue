//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     materials data = nlohmann::json::parse(jsonString);

#pragma once

#include "common.hpp"



namespace hue {
    using nlohmann::json;



    struct encoded {
        int64_t count;
        std::string name;
        /**
         * The localised value will be omitted if it is exactly the same as Name
         */
        std::optional<std::string> name_localised;
    };

    struct manufactured {
        int64_t count;
        std::string name;
        /**
         * The localised value will be omitted if it is exactly the same as Name
         */
        std::optional<std::string> name_localised;
    };

    struct raw {
        int64_t count;
        std::string name;
        /**
         * The localised value will be omitted if it is exactly the same as Name
         */
        std::optional<std::string> name_localised;
    };

    /**
     * When written: at startup, when loading from main menu into game
     */
    struct materials {
        std::vector<encoded> encoded;
        std::string event;
        std::vector<manufactured> manufactured;
        std::vector<raw> raw;
        /**
         * Timestamp in UTC, ISO 8601
         */
        std::string timestamp;
    };
}

namespace hue {
    void from_json(const json & j, encoded & x);
    void to_json(json & j, const encoded & x);

    void from_json(const json & j, manufactured & x);
    void to_json(json & j, const manufactured & x);

    void from_json(const json & j, raw & x);
    void to_json(json & j, const raw & x);

    void from_json(const json & j, materials & x);
    void to_json(json & j, const materials & x);

    inline void from_json(const json & j, encoded& x) {
        x.count = j.at("Count").get<int64_t>();
        x.name = j.at("Name").get<std::string>();
        x.name_localised = get_stack_optional<std::string>(j, "Name_Localised");
    }

    inline void to_json(json & j, const encoded & x) {
        j = json::object();
        j["Count"] = x.count;
        j["Name"] = x.name;
        j["Name_Localised"] = x.name_localised;
    }

    inline void from_json(const json & j, manufactured& x) {
        x.count = j.at("Count").get<int64_t>();
        x.name = j.at("Name").get<std::string>();
        x.name_localised = get_stack_optional<std::string>(j, "Name_Localised");
    }

    inline void to_json(json & j, const manufactured & x) {
        j = json::object();
        j["Count"] = x.count;
        j["Name"] = x.name;
        j["Name_Localised"] = x.name_localised;
    }

    inline void from_json(const json & j, raw& x) {
        x.count = j.at("Count").get<int64_t>();
        x.name = j.at("Name").get<std::string>();
        x.name_localised = get_stack_optional<std::string>(j, "Name_Localised");
    }

    inline void to_json(json & j, const raw & x) {
        j = json::object();
        j["Count"] = x.count;
        j["Name"] = x.name;
        j["Name_Localised"] = x.name_localised;
    }

    inline void from_json(const json & j, materials& x) {
        x.encoded = j.at("Encoded").get<std::vector<encoded>>();
        x.event = j.at("event").get<std::string>();
        x.manufactured = j.at("Manufactured").get<std::vector<manufactured>>();
        x.raw = j.at("Raw").get<std::vector<raw>>();
        x.timestamp = j.at("timestamp").get<std::string>();
    }

    inline void to_json(json & j, const materials & x) {
        j = json::object();
        j["Encoded"] = x.encoded;
        j["event"] = x.event;
        j["Manufactured"] = x.manufactured;
        j["Raw"] = x.raw;
        j["timestamp"] = x.timestamp;
    }
}


