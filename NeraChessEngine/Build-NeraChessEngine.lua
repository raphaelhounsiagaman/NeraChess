project "NeraChessEngine"
  kind "StaticLib"
  language "C++"
  cppdialect "C++23"
  targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
  objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")
  staticruntime "off"
  warnings "Extra"

  filter "toolset:gcc"
    fatalwarnings "All"
  filter "toolset:clang"
    fatalwarnings "All"
  filter {}

  files
  {
    "src/**.cpp",
    "src/**.cxx",
    "src/**.c",

    "src/**.hpp",
    "src/**.hxx",
    "src/**.h",
  }

  includedirs
  {
    "src"
  }

  -- Platform

  filter "system:windows"
    systemversion "latest"
    defines { }

  filter {}

  -- Configurations

  filter "configurations:Debug"
    defines { "DEBUG" }
    runtime "Debug"
    symbols "on"

  filter "configurations:Release"
    defines { "RELEASE" }
    runtime "Release"
    optimize "Speed"
    symbols "On"
    linktimeoptimization "On"

  filter "configurations:Dist"
    defines { "DIST" }
    runtime "Release"
    optimize "Speed"
    symbols "Off"
    linktimeoptimization "On"
  filter {}
