project "NeraChessUCI"
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

  -- Order matters: GNU ld resolves static archives left to right, so each
  -- library must come before the ones it depends on. Getting this backwards
  -- links fine on macOS and fails on Linux.
  links
  {
    "NeraChessSearch",
    "NeraChessNNUE",
    "NeraChessEngine",
  }

  postbuildcommands
  {
    '{MKDIR} "%{cfg.targetdir}/Resources/OpeningBook"',
    '{COPYFILE} "%{prj.location}/../NeraChessApp/Resources/OpeningBook/OpeningBook.txt" "%{cfg.targetdir}/Resources/OpeningBook/OpeningBook.txt"',
    -- The evaluation network, so a built engine plays out of the box rather
    -- than needing EvalFile set. Startup discovery looks here.
    '{MKDIR} "%{cfg.targetdir}/Resources/NNUE"',
    '{COPYFILE} "%{prj.location}/../NeraChessApp/Resources/NNUE/nera.nnue" "%{cfg.targetdir}/Resources/NNUE/nera.nnue"',
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
    linktimeoptimization "On"

  filter "configurations:Dist"
    defines { "DIST" }
    runtime "Release"
    optimize "Speed"
    symbols "Off"
    linktimeoptimization "On"
  filter {}
