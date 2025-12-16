project "NeraCore"
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

    "vendor/DearImGUI/**.h",
    "vendor/DearImGUI/**.cpp",
  }

  includedirs
  {
    "src",

    "vendor/DearImGUI",

    "vendor/SDL2/include",
    "vendor/SDL2_image/include",
    "vendor/SDL2_mixer/include",
  }

  libdirs
  {
    "vendor/SDL2/lib",
    "vendor/SDL2_image/lib",
    "vendor/SDL2_mixer/lib",
  }

  links
  {
    "SDL2",
    "SDL2main",
    "SDL2_image",
    "SDL2_mixer",
  }

  defines
  {
    "SDL_MAIN_HANDLED"
  }

  postbuildcommands 
  {
    '{COPY} "%{prj.location}/vendor/SDL2/lib/*.dll" "%{cfg.targetdir}"',
    '{COPY} "%{prj.location}/vendor/SDL2_image/lib/*.dll" "%{cfg.targetdir}"',
    '{COPY} "%{prj.location}/vendor/SDL2_mixer/lib/*.dll" "%{cfg.targetdir}"',
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