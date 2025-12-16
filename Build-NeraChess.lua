workspace "NeraChess"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "NeraChessApp"

   filter "system:windows"
      buildoptions { "/Zc:preprocessor" }
   filter {}

include "ChessCore/Build-ChessCore.lua"

include "NeraCore/Build-NeraCore.lua"

include "NeraChessApp/Build-NeraChessApp.lua"