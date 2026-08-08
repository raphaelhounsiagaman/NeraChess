NeraChessMacOSDependencyPrefix = ""

if os.host() == "macosx" then
  NeraChessMacOSDependencyPrefix = os.getenv("NERACHESS_MACOS_DEPENDENCY_PREFIX")

  if not NeraChessMacOSDependencyPrefix then
    NeraChessMacOSDependencyPrefix = os.outputof("brew --prefix 2>/dev/null")
  end

  if not NeraChessMacOSDependencyPrefix or NeraChessMacOSDependencyPrefix == "" then
    error("Homebrew was not found. Install Homebrew or set NERACHESS_MACOS_DEPENDENCY_PREFIX.")
  end

  NeraChessMacOSDependencyPrefix = NeraChessMacOSDependencyPrefix:gsub("%s+$", "")
end

workspace "NeraChess"
  configurations { "Debug", "Release", "Dist" }
  startproject "NeraChessApp"

  filter "system:windows"
    architecture "x86_64"
    buildoptions { "/Zc:preprocessor" }
  filter {}

include "NeraChessEngine/Build-NeraChessEngine.lua"

include "NeraChessSearch/Build-NeraChessSearch.lua"

include "NeraChessUCI/Build-NeraChessUCI.lua"

include "NeraChessTests/Build-NeraChessTests.lua"

include "ApplicationCore/Build-ApplicationCore.lua"

include "NeraChessApp/Build-NeraChessApp.lua"
