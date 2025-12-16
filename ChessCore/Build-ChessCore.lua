project "ChessCore"
  kind "StaticLib"
  language "C++"
  cppdialect "C++23"
  targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
  objdir ("../bin/Intermediates/%{cfg.builcfg}/%{prj.name}")
  staticruntime "off"

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
    optimize "On"
    symbols "On"

  filter "configurations:Dist"
  defines { "DIST" }
  runtime "Release"
  optimize "On"
  symbols "Off"
filter{}