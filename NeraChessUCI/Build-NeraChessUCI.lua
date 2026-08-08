project "NeraChessUCI"
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
    "src",
    "../NeraChessEngine/src",
    "../NeraChessSearch/src",
  }

  links
  {
    "NeraChessEngine",
    "NeraChessSearch",
  }

  postbuildcommands
  {
    '{MKDIR} "%{cfg.targetdir}/Ressources/OpeningBook"',
    '{COPYFILE} "%{prj.location}/../NeraChessApp/Ressources/OpeningBook/OpeningBook.txt" "%{cfg.targetdir}/Ressources/OpeningBook/OpeningBook.txt"',
  }

  filter "system:windows"
    systemversion "latest"
  filter "system:linux"
    links { "pthread" }
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
