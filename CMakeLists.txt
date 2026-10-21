# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

project(PdfLibre VERSION 0.1.0 DESCRIPTION "Lecteur PDF libre et rapide" LANGUAGES C CXX)

set(PDFLIBRE_HOMEPAGE "https://github.com/pdf-libre/pdf-libre" CACHE STRING "Adresse du code source affichée dans l'application")
option(PDFLIBRE_BUILD_TESTS "Compiler les tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)

# --- MuPDF -------------------------------------------------------------------
# Trois façons de le trouver, dans l'ordre :
#  1. MUPDF_ROOT : compilation Windows (en-têtes dans include/, bibliothèques
#     statiques libmupdf.lib et libthirdparty.lib) ;
#  2. vcpkg (paquet unofficial-mupdf) ;
#  3. pkg-config (Linux).
add_library(mupdf_deps INTERFACE)
if(MUPDF_ROOT)
    target_include_directories(mupdf_deps INTERFACE "${MUPDF_ROOT}/include")
    file(GLOB _mupdf_libs "${MUPDF_ROOT}/lib/*.lib" "${MUPDF_ROOT}/lib/*.a")
    if(NOT _mupdf_libs)
        message(FATAL_ERROR "Aucune bibliothèque MuPDF trouvée dans ${MUPDF_ROOT}/lib")
    endif()
    target_link_libraries(mupdf_deps INTERFACE ${_mupdf_libs})
else()
    find_package(unofficial-mupdf CONFIG QUIET)
    if(TARGET unofficial::mupdf::mupdf)
        target_link_libraries(mupdf_deps INTERFACE unofficial::mupdf::mupdf)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
        # Les paquets Linux fournissent MuPDF en bibliothèque statique.
        pkg_check_modules(MUPDF_STATIC_DEPS REQUIRED IMPORTED_TARGET harfbuzz freetype2 libopenjp2 libjpeg zlib)
        find_library(MUPDF_THIRD mupdf-third)
        find_library(MUJS mujs)
        find_library(GUMBO gumbo)
        find_library(JBIG2DEC jbig2dec)
        target_link_libraries(mupdf_deps INTERFACE PkgConfig::MUPDF)
        foreach(_lib MUPDF_THIRD MUJS GUMBO JBIG2DEC)
            if(${_lib})
                target_link_libraries(mupdf_deps INTERFACE ${${_lib}})
            endif()
        endforeach()
        target_link_libraries(mupdf_deps INTERFACE PkgConfig::MUPDF_STATIC_DEPS m)
    endif()
endif()

# --- Noyau (sans interface) ---------------------------------------------------
add_library(pdflibre_core STATIC
    src/core/Types.h
    src/core/PdfDocument.h src/core/PdfDocument.cpp
    src/core/RenderService.h src/core/RenderService.cpp
    src/core/SearchService.h src/core/SearchService.cpp
)
target_include_directories(pdflibre_core PUBLIC src)
target_link_libraries(pdflibre_core PUBLIC Qt6::Core Qt6::Gui PRIVATE mupdf_deps)
find_package(Threads REQUIRED)
target_link_libraries(pdflibre_core PUBLIC Threads::Threads)

# --- Interface ----------------------------------------------------------------
add_library(pdflibre_ui STATIC
    src/app/Settings.h src/app/Settings.cpp
    src/ui/DocumentTab.h src/ui/DocumentTab.cpp
    src/ui/DocumentView.h src/ui/DocumentView.cpp
    src/ui/Icons.h src/ui/Icons.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/Printing.h src/ui/Printing.cpp
    src/ui/SearchBar.h src/ui/SearchBar.cpp
    src/ui/ThumbnailModel.h src/ui/ThumbnailModel.cpp
    src/ui/WelcomeWidget.h src/ui/WelcomeWidget.cpp
)
target_link_libraries(pdflibre_ui PUBLIC pdflibre_core Qt6::Widgets Qt6::PrintSupport)
target_compile_definitions(pdflibre_ui PUBLIC
    APP_VERSION="${PROJECT_VERSION}"
    APP_HOMEPAGE="${PDFLIBRE_HOMEPAGE}"
)

# --- Application --------------------------------------------------------------
set(APP_SOURCES src/main.cpp resources/resources.qrc)
if(WIN32)
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc" resources/PDFLibre.manifest)
endif()

add_executable(PDFLibre WIN32 ${APP_SOURCES})
target_link_libraries(PDFLibre PRIVATE pdflibre_ui)
foreach(_target pdflibre_core pdflibre_ui PDFLibre)
    if(MSVC)
        # C4611 : setjmp de MuPDF (fz_try), utilisé selon ses règles.
        target_compile_options(${_target} PRIVATE /W4 /utf-8 /wd4611)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

install(TARGETS PDFLibre RUNTIME DESTINATION .)
install(FILES LICENSE README.md THIRD-PARTY-NOTICES.md DESTINATION .)

# --- Tests --------------------------------------------------------------------
if(PDFLIBRE_BUILD_TESTS)
    find_package(Qt6 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_executable(test_core tests/test_core.cpp)
        target_link_libraries(test_core PRIVATE pdflibre_core Qt6::Test)
        add_executable(test_ui tests/test_ui.cpp)
        target_link_libraries(test_ui PRIVATE pdflibre_ui Qt6::Test)
        add_test(NAME test_core COMMAND test_core)
        add_test(NAME test_ui COMMAND test_ui)
        set_tests_properties(test_core test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
