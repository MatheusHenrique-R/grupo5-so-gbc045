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
    # pthreads só no Linux/FreeBSD. No Windows NADA de pthread (sem winpthreads).
    find_package(Threads REQUIRED)
    target_link_libraries(ptlib PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------
if(EXISTS ${CMAKE_SOURCE_DIR}/code/bench.c)
    add_executable(bench code/bench.c)
    target_link_libraries(bench PRIVATE ptlib)
endif()

# ---------------------------------------------------------------------------
# Testes: cada testes/test_*.c vira um executável separado (cada um com seu
# main) ligado à biblioteca, e um teste do CTest.
# Exceção: test_bench.c, que é tratado logo abaixo.
# ---------------------------------------------------------------------------
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/testes/test_*.c)
foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    if(name STREQUAL "test_bench")
        continue()
    endif()
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ptlib)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endforeach()

# ---------------------------------------------------------------------------
# Teste do benchmark: executa o bench como outro processo e confere a saída.
#   - não usa a ptlib (só a biblioteca padrão de C);
#   - add_dependencies: garante que o bench é compilado ANTES do teste;
#   - $<TARGET_FILE:bench>: o CMake troca pelo caminho completo do bench.exe
#     (funciona em Debug/Release, MinGW, MSVC e Linux).
# ---------------------------------------------------------------------------
if(TARGET bench AND EXISTS ${CMAKE_SOURCE_DIR}/testes/test_bench.c)
    add_executable(test_bench testes/test_bench.c)
    add_dependencies(test_bench bench)
    add_test(NAME test_bench COMMAND test_bench $<TARGET_FILE:bench>)
    set_tests_properties(test_bench PROPERTIES TIMEOUT 300)
endif()