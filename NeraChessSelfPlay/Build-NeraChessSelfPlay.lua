project "NeraChessSelfPlay"
  kind "ConsoleApp"
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
    "../NeraChessNNUE/src",
    "../NeraChessSearch/src",
  }

  links
  {
    "NeraChessEngine",
    "NeraChessNNUE",
    "NeraChessSearch",
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
