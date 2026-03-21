cmake_minimum_required(VERSION 3.20)

project(server)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add subdirectory
add_subdirectory(data_structures)

# Main executable
add_executable(server
    main.c
    server.c
    worker.c
    static_files.c
    http.c
    response.c
)

# Include headers
target_include_directories(server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/data_structures
)

# Link data_structures library
target_link_libraries(server PRIVATE data_structures)
