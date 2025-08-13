project "NeraChessCore"
    kind "StaticLib"
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
        "src/**.h" 
    }

    includedirs
    {
        "src"
    }

    filter "system:windows"
        systemversion "latest"
        defines { }

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