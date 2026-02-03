cmake_minimum_required(VERSION 3.16)
project(Distibuted_Hash_Table)

set(CMAKE_CXX_FLAGS "-O3")

set(CMAKE_CXX_STANDARD 23)

add_executable(Distibuted_Hash_Table main.cpp
        Storage.cpp
        Storage.h
        Client.cpp
        Client.h
        Request.h
        ds/HashMap/phmap.hpp
        ds/HashMap/gtl_base.hpp
        ds/HashMap/gtl_config.hpp
        ds/HashMap/phmap_fwd_decl.hpp
        ds/HashMap/phmap_utils.hpp
        ds/HashMap/bits.hpp
        ds/concurrentqueue.h)
