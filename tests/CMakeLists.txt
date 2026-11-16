add_executable(FluidUIStyleDrawTests style_draw_tests.cpp)
target_link_libraries(FluidUIStyleDrawTests PRIVATE FluidUI::FluidUI)
add_test(NAME FluidUIStyleDrawTests COMMAND FluidUIStyleDrawTests)
set_tests_properties(FluidUIStyleDrawTests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:FluidUI>")
