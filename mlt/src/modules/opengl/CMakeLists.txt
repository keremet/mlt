#find_package(movit)
pkg_check_modules(movit IMPORTED_TARGET movit)
if(TARGET PkgConfig::movit)

    if(MINGW)
        set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -lopengl32 -lpthread")
    endif()
    if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -FOpenGL")
        set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -lpthread -framework OpenGL")
    endif()

    pkg_get_variable(SHADERDIR movit shaderdir)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSHADERDIR=\\\"${SHADERDIR}\\\"")

    file(GLOB mltopengl_src *.c *.cpp)
    add_library(mltopengl MODULE ${mltopengl_src})
    target_link_libraries(mltopengl mlt m PkgConfig::movit)
    # Create module in parent directory, for the benefit of "source setenv".
    set_target_properties(mltopengl PROPERTIES LIBRARY_OUTPUT_DIRECTORY ..)
    install(TARGETS mltopengl LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/mlt)
    file(GLOB yml *.yml)
    install(FILES ${yml} DESTINATION ${CMAKE_INSTALL_DATADIR}/mlt/opengl)
endif()
