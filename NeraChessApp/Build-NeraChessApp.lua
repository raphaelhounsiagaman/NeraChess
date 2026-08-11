project "NeraChessApp"
  kind "ConsoleApp"
  language "C++"
  cppdialect "C++23"
  targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
  objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")
  debugdir ("%{cfg.targetdir}")
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
    "src/**.cxx",
    "src/**.c",

    "src/**.hpp", 
    "src/**.hxx", 
    "src/**.h",
  }

  includedirs 
  {
    "src",

    "../NeraChessEngine/src",
    "../NeraChessNNUE/src",
    "../NeraChessSearch/src",

    "../ApplicationCore/src",
    "../ApplicationCore/vendor/DearImGUI",
  }

  links
  {
    "NeraChessEngine",
    "NeraChessNNUE",
    "NeraChessSearch",
    "ApplicationCore",
  }

  defines
  {
    "SDL_MAIN_HANDLED"
  }

  -- SYSTEM SPECIFICS

  filter "system:windows"
    systemversion "latest"
    defines { "WINDOWS" }
    includedirs
    {
      "../ApplicationCore/vendor/SDL2/include",
      "../ApplicationCore/vendor/SDL2_image/include",
      "../ApplicationCore/vendor/SDL2_mixer/include",
    }
    libdirs
    {
      "../ApplicationCore/vendor/SDL2/lib",
      "../ApplicationCore/vendor/SDL2_image/lib",
      "../ApplicationCore/vendor/SDL2_mixer/lib",
    }
    links
    {
      "SDL2",
      "SDL2_image",
      "SDL2_mixer",
    }
    postbuildcommands
    {
      '{COPYDIR} "%{prj.location}/Resources" "%{cfg.targetdir}/Resources"',
      '{COPYFILE} "%{prj.location}/../ApplicationCore/vendor/SDL2/lib/SDL2.dll" "%{cfg.targetdir}"',
      '{COPYFILE} "%{prj.location}/../ApplicationCore/vendor/SDL2_image/lib/SDL2_image.dll" "%{cfg.targetdir}"',
      '{COPYFILE} "%{prj.location}/../ApplicationCore/vendor/SDL2_mixer/lib/SDL2_mixer.dll" "%{cfg.targetdir}"',
    }

  filter "system:linux"
    links { "GL", "pthread", "dl" }

  filter "system:macosx"
    externalincludedirs
    {
      NeraChessMacOSDependencyPrefix .. "/include/SDL2",
    }
    libdirs
    {
      NeraChessMacOSDependencyPrefix .. "/lib",
    }
    links
    {
      "SDL2",
      "SDL2_image",
      "SDL2_mixer",
    }
    postbuildcommands
    {
      '{COPYDIR} "%{prj.location}/Resources/." "%{cfg.targetdir}/Resources"',
    }
  filter {}

  -- COMPILER SPECIFICS

  filter "toolset:gcc"
    defines { "USING_GCC" }

  filter "toolset:clang"
    defines { "USING_CLANG" }

  filter "toolset:msc" 
    defines { "USING_MSVC" }

  filter {}

    -- CONFIG SPECIFICS

  filter "configurations:Debug" 
    defines { "DEBUG" }
    runtime "Debug"
    symbols "On"

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

  filter {}
