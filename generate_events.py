#!/usr/bin/env python3
import os
from pathlib import Path
import re

EVENT_DIR = Path("src/events")
OUT_DIR = Path("src/events")
OUT_DIR.mkdir(exist_ok=True)

def to_snake(name: str) -> str:
    # "HTTPServer" -> "http_server"
    name = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', name)
    # "SellExploration" -> "sell_exploration"
    name = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name)
    return name.lower()

factory_include = '#include "factory.hpp"'

forward_decls = []
registry_entries = []

for header in sorted(EVENT_DIR.glob("*.hpp")):
    name = header.stem  # file base name, e.g. MultiSellExplorationData
    cpp_path = OUT_DIR / f"{name}.cpp"

    if name in ("common", "forward_decls", "forward_events", "factory", "events"):
        continue

    # Write one .cpp per header
    cpp_path.write_text(
f'''#include "{name}.hpp"
{factory_include}

template event make_event<hue::{to_snake(name)}>(const nlohmann::json &j);
'''
    )
    print(f"Generated {cpp_path}")

    # For registry & forward declarations
    forward_decls.append(f"    struct {to_snake(name)};")
    registry_entries.append(f"extern template event make_event<hue::{to_snake(name)}>(const nlohmann::json &j);")


# Write forward declarations
(Path("src/events/forward_decls.hpp")).write_text(
    "// Auto-generated forward declarations\n" +
    "#pragma once\n" +
    "namespace hue {\n" +
    "\n".join(forward_decls) +
    "\n}\n"
)
print("Generated forward_decls.hpp")
(Path("src/events/forward_events.hpp")).write_text(
    "// Auto-generated forward make_events\n" +
    "#pragma once\n" +
    "#include <nlohmann/json.hpp>\n" +
    "#include \"forward_decls.hpp\"\n" +
    "\n".join(registry_entries) +
    "\n"
)

# # Write registry init
# (Path("init_registry.cpp")).write_text(
# f'''#include "event_factory.hpp"
# #include <unordered_map>
# #include <string>
# #include "forward_decls.hpp"

# extern std::unordered_map<std::string, EventBox(*)(const json&)> registry;

# void init_registry() {{
# {os.linesep.join(registry_entries)}
# }}
# '''
# )
# print("Generated init_registry.cpp")