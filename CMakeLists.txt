cmake_minimum_required(VERSION 3.16)
project(gbc045_tema2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Biblioteca: camada de abstração (um único .c; o #ifdef _WIN32 dentro dele
# escolhe Linux ou Windows) + os arquivos do buffer, QUANDO existirem.
# ---------------------------------------------------------------------------
set(FONTES_LIB code/platform_thread.c)

foreach(arq code/buffer.c code/buffer_a.c code/buffer_b.c)
    if(EXISTS ${CMAKE_SOURCE_DIR}/${arq})
        list(APPEND FONTES_LIB ${arq})
    endif()
endforeach()

add_library(ptlib STATIC ${FONTES_LIB})
target_include_directories(ptlib PUBLIC code)

if(NOT WIN32)
    # pthreads só no Linux/FreeBSD.
    find_package(Threads REQUIRED)
    target_link_libraries(ptlib PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Testes: cada testes/test_*.c vira um executável separado (cada um com seu
# main) ligado à biblioteca, e um teste do CTest.
# ---------------------------------------------------------------------------
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/testes/test_*.c)
foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ptlib)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endforeach()