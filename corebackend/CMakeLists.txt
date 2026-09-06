# Stable C ABI over the headless core. The shared library keeps the
# boundary link-time stable for host integrations (Aura-Launcher, tools).
add_library(auracore_backend SHARED
    src/Backend.h
    src/Backend.cpp
)

target_compile_definitions(auracore_backend PRIVATE AURACORE_BACKEND_BUILDING)
target_include_directories(auracore_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(auracore_backend PRIVATE auracore_core BuildConfig Qt6::Core)

add_executable(auracore-probe tools/probe.cpp)
target_link_libraries(auracore-probe PRIVATE auracore_backend Qt6::Core)