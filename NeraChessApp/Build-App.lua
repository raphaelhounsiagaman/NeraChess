project "NeraChessApp"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("../bin/%{cfg.buildcfg}/%{prj.name}")
    objdir ("../bin/Intermediates/%{cfg.buildcfg}/%{prj.name}")

    files
    {
        "src/**.cpp", 
        "src/**.cxx", 
        "src/**.c", 
        "src/**.hpp", 
        "src/**.hxx", 
        "src/**.h",

        "vendor/DearImGui/**.h",
        "vendor/DearImGui/**.cpp",
    }

    includedirs 
    {
        "src",
	    "../NeraChessCore/src",

        "vendor/SDL2/include",
        "vendor/SDL2_image/include",
        "vendor/DearImGui",

        "vendor/onnxruntime-win-x64-gpu-1.23.2/include",
    }

    libdirs
    {
        "vendor/SDL2/lib",
        "vendor/SDL2_image/lib",

        "vendor/onnxruntime-win-x64-gpu-1.23.2/lib",
    }

    links
    {
        "NeraChessCore",
        
        "SDL2",
        "SDL2main",
        "SDL2_image",
        
        "onnxruntime",
        "onnxruntime_providers_cuda",
        "onnxruntime_providers_shared",
        "onnxruntime_providers_tensorrt",
    }

    defines
    {
        "SDL_MAIN_HANDLED"
    }

    postbuildcommands 
    {
        '{COPY} "%{prj.location}/vendor/SDL2/lib/*.dll" "%{cfg.targetdir}"',
        '{COPY} "%{prj.location}/vendor/SDL2_image/lib/*.dll" "%{cfg.targetdir}"',

        '{COPY} "%{prj.location}/vendor/onnxruntime-win-x64-gpu-1.23.2/lib/*.dll" "%{cfg.targetdir}"',
    }

    -- SYSTEM SPECIFICS

    filter "system:windows"
        systemversion "latest"
        defines { "WINDOWS" }
        links { "opengl32" }

    filter "system:linux"
        links { "GL", "pthread", "dl" }

    filter "system:macosx"
        links { "OpenGL.framework" }

    filter {}


    -- COMPILER SPECIFICS

    filter "toolset:gcc"
        defines { "USING_GCC" }

    filter "toolset:clang"
        defines { "USING_CLANG" }

    filter "toolset:msc" 
        defines { "USING_MSVC" }

    filter {}
    
    -- PLATFORM SPECIFICS

    filter "platforms:x64"
      architecture "x86_64"
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
    