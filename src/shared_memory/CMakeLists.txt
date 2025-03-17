project (optee_example_shared_memory C)

set (SRC host/main.c)

add_executable (${PROJECT_NAME} ${SRC})


# 添加包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE ta/include
    PRIVATE include
)

# 链接 OpenSSL 库
target_link_libraries(${PROJECT_NAME} PRIVATE teec)

install (TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
