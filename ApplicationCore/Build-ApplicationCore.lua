project "ApplicationCore"
  kind "StaticLib"
  language "C++"
  cppdialect "C++23"
  targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
  objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")
  staticruntime "off"
  warnings "Extra"

  files
  {
    "src/**.cpp",
    "src/**.cxx",
    "src/**.c",

    "src/**.hpp",
    "src/**.hxx",
    "src/**.h",

    "vendor/DearImGUI/**.h",
    "vendor/DearImGUI/**.cpp",
  }

  includedirs
  {
    "src",

    "vendor/DearImGUI",
  }

  defines
  {
    "SDL_MAIN_HANDLED"
  }

  -- Platform

  filter "system:windows"
    systemversion "latest"
    includedirs
    {
      "vendor/SDL2/include",
      "vendor/SDL2_image/include",
      "vendor/SDL2_mixer/include",
    }

  filter "system:macosx"
    externalincludedirs
    {
      NeraChessMacOSDependencyPrefix .. "/include/SDL2",
    }
  filter {}

  -- Configurations

  filter "configurations:Debug"
    defines { "DEBUG" }
    runtime "Debug"
    symbols "on"

  filter "configurations:Release"
    defines { "RELEASE" }
    runtime "Release"
    optimize "On"
    symbols "On"

  filter "configurations:Dist"
    defines { "DIST" }
    runtime "Release"
    optimize "On"
    symbols "Off"
  filter {}
