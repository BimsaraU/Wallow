# CMakeList.txt : CMake project for WAlow - lightweight WhatsApp Web browser
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("LMBrowser")

# =============================================================================
# WebView2 SDK - Download via NuGet
# =============================================================================
set(WEBVIEW2_VERSION "1.0.2903.40")
set(WEBVIEW2_DIR "${CMAKE_BINARY_DIR}/packages/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")
set(NUGET_EXE "${CMAKE_BINARY_DIR}/nuget.exe")

# Download nuget.exe if not present
if(NOT EXISTS "${NUGET_EXE}")
  message(STATUS "Downloading nuget.exe...")
  file(DOWNLOAD
    "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"
    "${NUGET_EXE}"
    TLS_VERIFY ON
  )
endif()

# Install WebView2 NuGet package
if(NOT EXISTS "${WEBVIEW2_DIR}")
  message(STATUS "Installing Microsoft.Web.WebView2 NuGet package...")
  execute_process(
    COMMAND "${NUGET_EXE}" install Microsoft.Web.WebView2
      -Version ${WEBVIEW2_VERSION}
      -OutputDirectory "${CMAKE_BINARY_DIR}/packages"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endif()

# Determine arch for WebView2 loader lib
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WEBVIEW2_ARCH "x64")
else()
  set(WEBVIEW2_ARCH "x86")
endif()

set(WEBVIEW2_INCLUDE "${WEBVIEW2_DIR}/build/native/include")
set(WEBVIEW2_LIB_DIR "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}")

# =============================================================================
# ImGui sources
# =============================================================================
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(IMGUI_SOURCES
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/imgui_demo.cpp"
  "${IMGUI_DIR}/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/imgui_impl_dx11.cpp"
)

# =============================================================================
# Application
# =============================================================================
add_executable(LMBrowser WIN32
  "WAlow.cpp"
  "WAlow.h"
  "LMBrowser.rc"
  ${IMGUI_SOURCES}
)

set_property(TARGET LMBrowser PROPERTY CXX_STANDARD 20)

target_include_directories(LMBrowser PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${IMGUI_DIR}"
  "${WEBVIEW2_INCLUDE}"
)

target_link_directories(LMBrowser PRIVATE
  "${WEBVIEW2_LIB_DIR}"
)

target_link_libraries(LMBrowser PRIVATE
  d3d11
  dxgi
  psapi
  dwmapi
  WebView2LoaderStatic.lib
)

# Link Windows subsystem
target_link_options(LMBrowser PRIVATE "/SUBSYSTEM:WINDOWS")
