add_library(FluidUI SHARED
    FluidUI.cpp
    Style.cpp
    DrawList.cpp
    Context.cpp
)
add_library(FluidUI::FluidUI ALIAS FluidUI)

target_compile_definitions(FluidUI PRIVATE FLUIDUI_BUILD)
target_compile_features(FluidUI PUBLIC cxx_std_20)

set_target_properties(FluidUI PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(FluidUI
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

install(TARGETS FluidUI
    RUNTIME DESTINATION bin      # libFluidUI.dll
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)     # libFluidUI.dll.a (import library)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/FluidUI
    DESTINATION include FILES_MATCHING PATTERN "*.h")