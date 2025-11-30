
#include "reader.hpp"
#include "spdlog/spdlog.h"

#include <thread>

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::debug);
    auto r = std::make_shared<hue::reader>(R"(C:\Users\pooh\Saved Games\Frontier Developments\Elite Dangerous)");

    r->run();
}
