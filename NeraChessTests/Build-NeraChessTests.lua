project "NeraChessTests"
  kind "ConsoleApp"
  language "C++"
  cppdialect "C++23"
  targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
  objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")
  staticruntime "off"

  files
  {
    "src/**.cpp",
    "src/**.h",
  }

  includedirs
  {
    "../NeraChessEngine/src",
    "../NeraChessSearch/src",
  }

  links
  {
    "NeraChessEngine",
    "NeraChessSearch",
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
