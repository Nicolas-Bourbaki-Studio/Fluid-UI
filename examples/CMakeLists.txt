add_executable(basic_glfw_gl main.cpp)
target_link_libraries(basic_glfw_gl PRIVATE FluidUI glfw opengl32)

add_custom_command(TARGET basic_glfw_gl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:FluidUI> $<TARGET_FILE_DIR:basic_glfw_gl>)

install(TARGETS basic_glfw_gl RUNTIME DESTINATION bin)