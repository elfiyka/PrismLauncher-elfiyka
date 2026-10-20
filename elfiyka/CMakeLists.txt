cmake_minimum_required(VERSION 3.25)

set(Launcher_Name "PrismLauncher")

set(Launcher_MSA_CLIENT_ID "" CACHE STRING "Client ID for Microsoft Authentication" FORCE)
configure_file("${CMAKE_SOURCE_DIR}/buildconfig/BuildConfig.cpp.in" "${CMAKE_BINARY_DIR}/buildconfig/BuildConfig.cpp")

# Cleanly handle compiler options (strip -mguard=cf) and includes across all targets
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        macro(strip_mguard_dir dir)
            get_directory_property(_opts DIRECTORY "${dir}" COMPILE_OPTIONS)
            if(_opts)
                string(REPLACE ";-mguard=cf" "" _opts "${_opts}")
                string(REPLACE "-mguard=cf;" "" _opts "${_opts}")
                string(REPLACE "-mguard=cf" "" _opts "${_opts}")
                set_property(DIRECTORY "${dir}" PROPERTY COMPILE_OPTIONS "${_opts}")
            endif()
            get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
            foreach(t IN LISTS _targets)
                get_target_property(_tgt_type ${t} TYPE)
                if(_tgt_type MATCHES "^(STATIC_LIBRARY|MODULE_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
                    get_target_property(_tgt_opts ${t} COMPILE_OPTIONS)
                    if(_tgt_opts)
                        string(REPLACE ";-mguard=cf" "" _tgt_opts "${_tgt_opts}")
                        string(REPLACE "-mguard=cf;" "" _tgt_opts "${_tgt_opts}")
                        string(REPLACE "-mguard=cf" "" _tgt_opts "${_tgt_opts}")
                        set_target_properties(${t} PROPERTIES COMPILE_OPTIONS "${_tgt_opts}")
                    endif()
                    target_include_directories(${t} BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
                    target_compile_options(${t} PRIVATE -Wno-error)
                endif()
            endforeach()
            get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
            foreach(sd IN LISTS _subdirs)
                strip_mguard_dir("${sd}")
            endforeach()
        endmacro()
        strip_mguard_dir("${CMAKE_SOURCE_DIR}")
    endif()
endif()

target_include_directories(Launcher_logic BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_sources(Launcher_logic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/AuthServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AuthServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Elfiyka.cpp
)


if(BUILD_TESTING)
    add_executable(AuthServer_test
        ${CMAKE_CURRENT_SOURCE_DIR}/AuthServer_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AuthServer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AuthServer.h
    )
    target_include_directories(AuthServer_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_options(AuthServer_test PRIVATE -Wno-error)
    set_target_properties(AuthServer_test PROPERTIES AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
    target_link_libraries(AuthServer_test PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    enable_testing()
    add_test(NAME AuthServer_test COMMAND AuthServer_test)

    add_custom_target(elfiyka ALL DEPENDS ${Launcher_Name} AuthServer_test)
else()
    add_custom_target(elfiyka ALL DEPENDS ${Launcher_Name})
endif()
