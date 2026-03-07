workspace "NeraChess"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "NeraChessApp"

   filter "system:windows"
      buildoptions { "/Zc:preprocessor" }
   filter {}

include "NeraChessEngine/Build-NeraChessEngine.lua"

include "ApplicationCore/Build-ApplicationCore.lua"

include "NeraChessApp/Build-NeraChessApp.lua"