add_executable(mc2mod main.cpp)
set_target_properties(mc2mod PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mc2mod mc2mods)
