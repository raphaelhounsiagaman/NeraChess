
workspace "NeraChess"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "NeraChessApp"

   filter "system:windows"
      buildoptions { "/Zc:preprocessor" }
   filter {}
   
include "NeraChessCore/Build-Core.lua"

include "NeraChessApp/Build-App.lua"

