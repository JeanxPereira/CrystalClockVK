#include "app/HostInputs.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "scene/ClockDate.hpp"

namespace app {

namespace {

using nlohmann::json;

struct Range {
    int low, high;
};

// HDD OSD 1.10U 0x203A20 config_set_default_main: language field 0 reads as 1, aspect 0, summer 0, 24 hour, date format 0.
constexpr int kDefaultLanguage = 1;
constexpr int kDefaultAspect = 0;
constexpr int kDefaultDateFormat = 0;
constexpr int kDefaultTimeFormat = 0;

// config_get_osd_language 0x203DD8 (language 1 to 7), config_get_aspect_ratio 0x203D30 (0 to 2), config_set_timezone_offset
// 0x203F00 (11 bits), config_get_date_format 0x204048 (0 to 2), config_get_time_format 0x204008 (1 bit).
constexpr Range kLanguage{1, 7};
constexpr Range kAspect{0, 2};
constexpr Range kTimeZone{-1024, 1023};
constexpr Range kDateFormat{0, 2};
constexpr Range kTimeFormat{0, 1};

constexpr uint32_t kNoDisc = 0x64;
constexpr uint32_t kScreenWidth = 640;
constexpr uint32_t kScreenHeightNtsc = 0xE0;
constexpr uint32_t kScreenHeightPal = 0x100;

int checked(const char* key, int value, Range range, const char* origin) {
    if (value < range.low || value > range.high)
        throw std::runtime_error(std::string(origin) + ": " + key + " " + std::to_string(value) + " is outside " + std::to_string(range.low) + ".." + std::to_string(range.high));
    return value;
}

int integer(const json& j, const char* key, Range range) {
    const json& v = j.at(key);
    if (!v.is_number_integer()) throw std::runtime_error(std::string("settings.json: ") + key + " is not an integer");
    return checked(key, v.get<int>(), range, "settings.json");
}

void readSettings(const std::filesystem::path& path, scene::ColdInputs& out, bool& zoneGiven) {
    std::ifstream in(path);
    if (!in.good()) return;
    json j;
    try {
        j = json::parse(in);
    } catch (const json::exception& e) {
        throw std::runtime_error("settings.json: " + std::string(e.what()));
    }
    if (!j.is_object()) throw std::runtime_error("settings.json: not an object");
    for (const auto& [key, value] : j.items()) {
        (void)value;
        if (key != "language" && key != "aspect" && key != "timeZone" && key != "summerTime" && key != "dateFormat" && key != "timeFormat")
            throw std::runtime_error("settings.json: unknown key " + key);
    }
    if (j.contains("language")) out.language = integer(j, "language", kLanguage);
    if (j.contains("aspect")) out.aspect = integer(j, "aspect", kAspect);
    if (j.contains("timeZone")) {
        out.timeZone = integer(j, "timeZone", kTimeZone);
        zoneGiven = true;
    }
    if (j.contains("summerTime")) {
        const json& v = j.at("summerTime");
        if (v.is_boolean()) out.summerTime = v.get<bool>();
        else if (v.is_number_integer() && (v.get<int>() == 0 || v.get<int>() == 1)) out.summerTime = v.get<int>() == 1;
        else throw std::runtime_error("settings.json: summerTime is not a boolean");
    }
    if (j.contains("dateFormat")) out.dateFormat = integer(j, "dateFormat", kDateFormat);
    if (j.contains("timeFormat")) out.timeFormat = integer(j, "timeFormat", kTimeFormat);
}

}

scene::ColdInputs hostInputs(const HostOptions& options, std::chrono::system_clock::time_point now, const std::filesystem::path& settings, const std::chrono::time_zone& zone) {
    scene::ColdInputs out;
    out.language = kDefaultLanguage;
    out.aspect = kDefaultAspect;
    out.dateFormat = kDefaultDateFormat;
    out.timeFormat = kDefaultTimeFormat;
    out.summerTime = false;
    out.pal = options.pal;
    out.screenWidth = kScreenWidth;
    out.screenHeight = options.pal ? kScreenHeightPal : kScreenHeightNtsc;
    out.randState = 1;
    out.wide = false;
    out.screenCode = kNoDisc;

    bool zoneGiven = false;
    readSettings(settings, out, zoneGiven);
    if (options.language) out.language = checked("language", *options.language, kLanguage, "--language");
    if (options.aspect) out.aspect = checked("aspect", *options.aspect, kAspect, "--aspect");
    if (!zoneGiven) {
        const auto offset = std::chrono::duration_cast<std::chrono::minutes>(zone.get_info(now).offset);
        out.timeZone = checked("timeZone", static_cast<int>(offset.count()), kTimeZone, "host time zone");
    }

    const auto console = std::chrono::floor<std::chrono::seconds>(now) + std::chrono::minutes(scene::kBaseZone);
    const auto day = std::chrono::floor<std::chrono::days>(console);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss time{console - day};
    out.rtc = {static_cast<int32_t>(date.year()), static_cast<int32_t>(static_cast<unsigned>(date.month())), static_cast<int32_t>(static_cast<unsigned>(date.day())),
               static_cast<int32_t>(time.hours().count()), static_cast<int32_t>(time.minutes().count()), static_cast<int32_t>(time.seconds().count())};
    return out;
}

}
