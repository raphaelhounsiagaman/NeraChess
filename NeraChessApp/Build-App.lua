project "NeraChessApp"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
    objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")
    staticruntime "off"

    files
    {
        "src/**.cpp", 
        "src/**.cxx", 
        "src/**.c", 
        "src/**.hpp", 
        "src/**.hxx", 
        "src/**.h",

        "vendor/DearImGui/**.h",
        "vendor/DearImGui/**.cpp"
    }

    includedirs 
    {
        "src",
	    "../NeraChessCore/src",

        "vendor/SDL2/include",
        "vendor/SDL2_image/include",
        "vendor/DearImGui"
    }

    libdirs
    {
        "vendor/SDL2/lib",
        "vendor/SDL2_image/lib"
    }

    links
    {
        "NeraChessCore",
        
        "SDL2",
        "SDL2main",
        "SDL2_image"
    }

    defines
    {
        "SDL_MAIN_HANDLED"
    }


    -- SYSTEM SPECIFICS

    filter "system:windows"
        systemversion "latest"
        defines { "WINDOWS" }
        links { "opengl32" } -- opengl stuff

    filter "system:linux"
        links { "GL", "pthread", "dl" } -- opengl stuff

    filter "system:macosx"
        links { "OpenGL.framework" } -- opengl stuff

    -- COMPILER SPECIFICS

    filter "toolset:gcc"
        defines { "USING_GCC" }

    filter "toolset:clang"
        defines { "USING_CLANG" }

    filter "toolset:msc" 
        defines { "USING_MSVC" }

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
    