project "NeraChessSearch"
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
    "src/**.h",
  }

  includedirs
  {
    "src",
    "../NeraChessEngine/src",
  }

  links
  {
    "NeraChessEngine",
  }

  filter "system:windows"
    systemversion "latest"
  filter {}

  filter "configurations:Debug"
    defines { "DEBUG" }
    runtime "Debug"
    symbols "On"

  filter "configurations:Release"
    defines { "RELEASE" }
    runtime "Release"
    optimize "Speed"
    symbols "On"

  filter "configurations:Dist"
    defines { "DIST" }
    runtime "Release"
    optimize "Speed"
    symbols "Off"
  filter {}
