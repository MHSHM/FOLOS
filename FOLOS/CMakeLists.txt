include(FetchContent)

if(MSVC)
    add_compile_options(/utf-8)
endif()

FetchContent_Declare(
    HSTL
    GIT_REPOSITORY https://github.com/MHSHM/hstl.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(HSTL)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(cpp_httplib)

add_executable(webhook_listener main.cpp sqlite3.c)

find_package(Threads REQUIRED)

target_link_libraries(webhook_listener PRIVATE 
    Threads::Threads 
    HSTL::HSTL 
    httplib::httplib
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/public DESTINATION ${CMAKE_BINARY_DIR}/bin)