cmake_minimum_required(VERSION 3.18)
project(system_monitor_backend LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 0. Находим зависимости через vcpkg
find_package(fmt CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "arm64-android")
endif()
set(VCPKG_INCLUDE_DIR "C:/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/include")

# 1. Собираем исходники из корня (исключая main.cpp)
file(GLOB COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# 2. Собираем файлы сервера, состояния и ядра
file(GLOB_RECURSE SERVER_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/state/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
)

# 3. Платформозависимые файлы
if(ANDROID)
    file(GLOB_RECURSE PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux_android/*.cpp")
else()
    file(GLOB_RECURSE PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/*.cpp")
endif()

# 4. Создаем библиотеку
add_library(system_monitor SHARED ${COMMON_SOURCES} ${SERVER_SOURCES} ${PLATFORM_SOURCES})

target_compile_definitions(system_monitor PRIVATE 
    BUILDING_SYSTEM_MONITOR
    FMT_HEADER_ONLY
    SPDLOG_PREVENT_CHILD_FD
    SPDLOG_NO_FWRITE_UNLOCKED # <- Отключает вызов fwrite_unlocked в spdlog
)

# 5. Подключаем все инклюды проекта + vcpkg
target_include_directories(system_monitor PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/state"
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux_android"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux_android/Shizuku"
    "${VCPKG_INCLUDE_DIR}"
)

# 6. Линковка библиотек
if(ANDROID)
    find_library(log-lib log)
    target_link_libraries(system_monitor PRIVATE 
        ${log-lib}
        fmt::fmt
        Crow::Crow
        spdlog::spdlog_header_only # <- Используем header-only версию вместо статичной библиотеки
    )
else()
    target_link_libraries(system_monitor PRIVATE 
        fmt::fmt
        Crow::Crow
        spdlog::spdlog
    )
endif()