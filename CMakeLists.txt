cmake_minimum_required(VERSION 3.16)
project(shpunt_final_elem VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(shpunt_final_elem
    main.cpp
    mainwindow.cpp
    mainwindow.h
    params_io.cpp
    params_io.h
    shpunt_calc.cpp
    shpunt_calc.h
)


# Debug — консольная подсистема (виден терминал),
# Release и остальные конфигурации — GUI-подсистема (без терминала)
set_target_properties(shpunt_final_elem PROPERTIES
    WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
)

target_link_libraries(shpunt_final_elem PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
