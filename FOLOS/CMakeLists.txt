include(FetchContent)

find_package(OpenSSL REQUIRED)

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

add_executable(webhook_listener main.cpp sqlite3.c Database.cpp SMS_Parser.cpp)

find_package(Threads REQUIRED)

target_link_libraries(webhook_listener PRIVATE 
    Threads::Threads 
    HSTL::HSTL 
    httplib::httplib
    OpenSSL::SSL 
    OpenSSL::Crypto
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/public DESTINATION ${CMAKE_BINARY_DIR}/bin)