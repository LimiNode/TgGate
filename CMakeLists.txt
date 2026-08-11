cmake_minimum_required(VERSION 3.21)

project(TgGate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(IMGUI_DEFINE_MATH_OPERATORS)

option(TGGATE_BUILD_TESTS "Build TgGate tests" ON)
option(TGGATE_BUILD_UI_MODEL "Build the ImGuiX + SFML desktop targets" ON)
option(TGGATE_ENABLE_TDLIB "Build the TDLib adapter" OFF)
option(TGGATE_ENABLE_HTTP_HOST "Enable the Simple-Web-Server HTTP host" OFF)

function(tggate_require_external relative_path)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR
            "Required dependency '${relative_path}' is absent. Run: git submodule update --init --jobs 8")
    endif()
endfunction()

tggate_require_external("external/json")
add_subdirectory(external/json "${CMAKE_BINARY_DIR}/_deps/json")

add_library(tggate_domain STATIC
    src/domain/policy/PolicyEngine.cpp
    src/domain/approval/ApprovalService.cpp
    src/domain/telegram/AuthorizationStateMachine.cpp)
target_include_directories(tggate_domain PUBLIC src)
target_link_libraries(tggate_domain PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(tggate_domain PUBLIC cxx_std_20)

add_library(tggate_application STATIC
    src/application/TelegramApplicationService.cpp
    src/application/AuditService.cpp)
target_include_directories(tggate_application PUBLIC src)
target_link_libraries(tggate_application PUBLIC tggate_domain)

add_library(tggate_configuration STATIC
    src/application/config/ClientProfileRepository.cpp
    src/application/config/AccountRepository.cpp
    src/application/config/McpHostConfigurationRepository.cpp
    src/application/config/RuntimeLayout.cpp
    src/infrastructure/crypto/SecureRandom.cpp
    src/infrastructure/dpapi/DpapiProtector.cpp)
target_include_directories(tggate_configuration PUBLIC src)
target_link_libraries(tggate_configuration PUBLIC tggate_domain)
target_compile_definitions(tggate_configuration PUBLIC
    TGGATE_CONFIG_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/configs")
if(WIN32)
    target_link_libraries(tggate_configuration PRIVATE crypt32 bcrypt)
endif()

add_library(tggate_mcp STATIC
    src/mcp/core/ToolRegistry.cpp
    src/mcp/v2025_11_25/ProtocolHandler.cpp
    src/mcp/v2026_07_28/ProtocolHandler.cpp)
target_include_directories(tggate_mcp PUBLIC src)
target_link_libraries(tggate_mcp PUBLIC tggate_application)

add_library(tggate_mcp_http STATIC src/mcp/http/LocalMcpHttpService.cpp)
target_include_directories(tggate_mcp_http PUBLIC src)
target_link_libraries(tggate_mcp_http PUBLIC tggate_mcp tggate_configuration)

if(TGGATE_BUILD_UI_MODEL)
    foreach(gui_dependency IN ITEMS
        external/ImGuiX
        external/imgui
        external/imgui-sfml
        external/SFML
        external/fmt
        external/freetype
        external/dlg)
        tggate_require_external("${gui_dependency}")
    endforeach()

    # Keep the first UI slice small. Every optional ImGuiX feature is introduced
    # only when its direct dependency is added beside these modules in external/.
    set(IMGUIX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(IMGUIX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(IMGUIX_SDK_INSTALL OFF CACHE BOOL "" FORCE)
    set(IMGUIX_SDK_BUNDLE_DEPS OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_SFML_BACKEND ON CACHE BOOL "" FORCE)
    set(IMGUIX_USE_GLFW_BACKEND OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_SDL2_BACKEND OFF CACHE BOOL "" FORCE)
    set(IMGUIX_IMGUI_FREETYPE ON CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMPLOT OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMPLOT3D OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMNODEFLOW OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMGUIFILEDIALOG OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMTEXTEDITOR OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_PFD OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMCMD OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMCOOLBAR OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMSPINNER OFF CACHE BOOL "" FORCE)
    set(IMGUIX_USE_IMGUI_MD OFF CACHE BOOL "" FORCE)
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui" CACHE PATH "" FORCE)
    set(IMGUI_SFML_DIR "${CMAKE_SOURCE_DIR}/external/imgui-sfml" CACHE PATH "" FORCE)
    set(IMGUIX_SFML_SRC_DIR "${CMAKE_SOURCE_DIR}/external/SFML" CACHE PATH "" FORCE)

    add_subdirectory(external/fmt "${CMAKE_BINARY_DIR}/_deps/fmt")
    # SFML's graphics module consumes the same top-level FreeType target.
    # This prevents SFML's fallback FetchContent graph from introducing a nested copy.
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory(external/freetype "${CMAKE_BINARY_DIR}/_deps/freetype" EXCLUDE_FROM_ALL)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()

    # Add SFML before ImGuiX. Its helper then reuses these targets instead of
    # invoking SFML's nested FetchContent fallback for FreeType.
    set(SFML_BUILD_GRAPHICS ON CACHE BOOL "" FORCE)
    set(SFML_BUILD_WINDOW ON CACHE BOOL "" FORCE)
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
    set(SFML_USE_SYSTEM_DEPS ON CACHE BOOL "" FORCE)
    # SFML 3.0.1's FindFreetype module predates target-only discovery. Feed it
    # the top-level source target and headers so it does not download another copy.
    set(FREETYPE_LIBRARY freetype CACHE FILEPATH "" FORCE)
    set(FREETYPE_INCLUDE_DIR_ft2build "${CMAKE_SOURCE_DIR}/external/freetype/include" CACHE PATH "" FORCE)
    set(FREETYPE_INCLUDE_DIR_freetype2 "${CMAKE_SOURCE_DIR}/external/freetype/include" CACHE PATH "" FORCE)
    set(FREETYPE_VERSION_STRING "2.13.3" CACHE STRING "" FORCE)
    add_subdirectory(external/SFML "${CMAKE_BINARY_DIR}/_deps/sfml")

    # TgGate consumes ImGuiX in-tree; suppress its standalone package install
    # rules, whose export set intentionally belongs to an ImGuiX SDK build.
    set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
    add_subdirectory(external/ImGuiX "${CMAKE_BINARY_DIR}/_deps/imguix")
    # The upstream ImGuiX target assumes its embedding project exports these
    # include roots; keep them explicit instead of relying on nested libs/.
    target_include_directories(imguix PUBLIC
        "${CMAKE_SOURCE_DIR}/external/imgui"
        "${CMAKE_SOURCE_DIR}/external/imgui/misc/cpp")
    if(TARGET ImGui-SFML)
        # See the matching compatibility workaround in mgc-platform.
        target_compile_definitions(ImGui-SFML PRIVATE NDEBUG)
    endif()

    add_library(tggate_ui_model STATIC src/ui/desktop/PendingActionsViewModel.cpp)
    target_include_directories(tggate_ui_model PUBLIC src)
    target_link_libraries(tggate_ui_model PUBLIC tggate_application)

    add_library(tggate_desktop_state STATIC src/ui/desktop/DesktopUiState.cpp)
    target_include_directories(tggate_desktop_state PUBLIC src)
    target_link_libraries(tggate_desktop_state PRIVATE tggate_ui_model tggate_configuration)

    function(tggate_configure_imguix_executable target_name)
        target_link_libraries(${target_name} PRIVATE ImGuiX::imguix ImGui-SFML SFML::Graphics SFML::Window SFML::System)
        target_include_directories(${target_name} PRIVATE
            "${CMAKE_SOURCE_DIR}/external/SFML/include"
            "${CMAKE_SOURCE_DIR}/external/imgui"
            "${CMAKE_SOURCE_DIR}/external/imgui-sfml")
        set_target_properties(${target_name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        if(MINGW)
            target_link_libraries(${target_name} PRIVATE gdi32 user32 comctl32 dwmapi opengl32 ws2_32)
        endif()
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endfunction()

    add_executable(tggate_desktop
        apps/desktop/main.cpp
        src/ui/desktop/DesktopController.cpp)
    target_link_libraries(tggate_desktop PRIVATE tggate_desktop_state)
    tggate_configure_imguix_executable(tggate_desktop)

    add_executable(tggate_imguix_smoke apps/imguix_smoke/main.cpp)
    tggate_configure_imguix_executable(tggate_imguix_smoke)
endif()

if(TGGATE_ENABLE_HTTP_HOST)
    tggate_require_external("external/Simple-Web-Server")
    tggate_require_external("external/asio")
    # Simple-Web-Server can use standalone Asio. Keep it as a top-level
    # dependency rather than allowing an implicit Boost dependency graph.
    add_library(asio::asio INTERFACE IMPORTED)
    set_target_properties(asio::asio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/external/asio/include")
    set(USE_STANDALONE_ASIO ON CACHE BOOL "" FORCE)
    set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/Simple-Web-Server "${CMAKE_BINARY_DIR}/_deps/simple-web-server")
    add_library(tggate_http_host STATIC src/infrastructure/http/SimpleWebHttpHost.cpp)
    target_include_directories(tggate_http_host PUBLIC src)
    target_link_libraries(tggate_http_host PUBLIC simple-web-server)
    if(TGGATE_BUILD_UI_MODEL)
        target_link_libraries(tggate_desktop_state PRIVATE tggate_mcp_http tggate_http_host)
        target_compile_definitions(tggate_desktop_state PRIVATE TGGATE_WITH_HTTP_HOST=1)
    endif()
endif()

if(TGGATE_ENABLE_TDLIB)
    tggate_require_external("external/tdlib")
    tggate_require_external("external/openssl")
    add_library(tggate_tdlib STATIC src/infrastructure/tdlib/TdAccount.cpp)
    target_include_directories(tggate_tdlib PUBLIC src)
    # TDLib is built separately. Its installed CMake package propagates its
    # generated headers and OpenSSL/zlib link requirements without guessing files.
    set(TGGATE_TDLIB_PREFIX "" CACHE PATH "TDLib installation prefix")
    if(NOT TGGATE_TDLIB_PREFIX)
        message(FATAL_ERROR "Set TGGATE_TDLIB_PREFIX to the installed TDLib prefix")
    endif()
    find_package(Td CONFIG REQUIRED PATHS "${TGGATE_TDLIB_PREFIX}/lib/cmake/Td" NO_DEFAULT_PATH)
    target_link_libraries(tggate_tdlib PUBLIC Td::TdStatic)
    if(TGGATE_BUILD_UI_MODEL)
        target_link_libraries(tggate_desktop_state PRIVATE tggate_tdlib)
        target_compile_definitions(tggate_desktop_state PRIVATE TGGATE_WITH_TDLIB=1)
    endif()
    if(TGGATE_BUILD_TESTS)
        add_executable(tggate_tdlib_adapter_smoke tests/tdlib_adapter_smoke.cpp)
        target_link_libraries(tggate_tdlib_adapter_smoke PRIVATE tggate_tdlib)
        add_test(NAME tggate_tdlib_adapter_smoke COMMAND tggate_tdlib_adapter_smoke)
        if(WIN32)
            string(REPLACE ";" "\\;" tggate_runtime_path "$ENV{PATH}")
            set_tests_properties(tggate_tdlib_adapter_smoke PROPERTIES
                ENVIRONMENT "PATH=${CMAKE_SOURCE_DIR}/external/openssl/bin\\;${tggate_runtime_path}")
        endif()
    endif()
endif()

if(MSVC)
    foreach(target IN ITEMS tggate_domain tggate_application tggate_mcp)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endforeach()
endif()

if(WIN32)
    target_link_libraries(tggate_domain PRIVATE bcrypt)
endif()

if(TGGATE_BUILD_TESTS)
    enable_testing()
    add_executable(tggate_secret_buffer_tests tests/secret_buffer_tests.cpp)
    target_include_directories(tggate_secret_buffer_tests PRIVATE src)
    add_test(NAME tggate_secret_buffer_tests COMMAND tggate_secret_buffer_tests)
    add_executable(tggate_policy_tests tests/policy_tests.cpp)
    target_link_libraries(tggate_policy_tests PRIVATE tggate_mcp tggate_configuration)
    add_test(NAME tggate_policy_tests COMMAND tggate_policy_tests)
    # This self-containment gate catches adapter-header regressions even on
    # machines that intentionally do not have an installed TDLib package.
    add_executable(tggate_tdlib_header_tests tests/tdlib_header_tests.cpp)
    target_include_directories(tggate_tdlib_header_tests PRIVATE src)
    add_test(NAME tggate_tdlib_header_tests COMMAND tggate_tdlib_header_tests)
    if(TGGATE_ENABLE_HTTP_HOST)
        add_executable(tggate_http_host_tests tests/http_host_tests.cpp)
        target_link_libraries(tggate_http_host_tests PRIVATE tggate_http_host tggate_mcp_http)
        add_test(NAME tggate_http_host_tests COMMAND tggate_http_host_tests)
        set_tests_properties(tggate_http_host_tests PROPERTIES TIMEOUT 15)
    endif()
    if(TGGATE_BUILD_UI_MODEL)
        add_test(NAME tggate_imguix_window_smoke COMMAND tggate_imguix_smoke --frames 5)
        set_tests_properties(tggate_imguix_window_smoke PROPERTIES
            TIMEOUT 20
            PASS_REGULAR_EXPRESSION "TgGate ImGuiX smoke window rendered")
    endif()
endif()
