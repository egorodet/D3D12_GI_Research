require "vstudio"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"
WIN_SDK = "latest"

function runtimeDependency(source, destination)
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries/" .. source .. "\" \"$(OutDir)" .. destination .. "/\"") }
end

workspace (ENGINE_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release", "DebugASAN" }
    platforms { "x64" }
	defines { "x64" }
	language "C++"
	cppdialect "c++17"
	startproject (ENGINE_NAME)
	symbols "On"
	architecture "x64"
	characterset "MBCS"
	flags {"MultiProcessorCompile", "ShadowedVariables", "FatalWarnings"}
	rtti "Off"
	warnings "Extra"
	system "windows"
	conformancemode "On"
	defines { "PLATFORM_WINDOWS=1", "WIN32" }
	targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
	objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")
	
	-- Unreferenced variable
	disablewarnings {"4100"}
	-- unreferenced function with internal linkage has been removed
	disablewarnings {"4505"}
	
	filter "configurations:Debug"
		runtime "Debug"
		defines { "_DEBUG" }
		optimize "Off"
		--inlining "Explicit"

	filter "configurations:Release"
 		runtime "Release"
		defines { "RELEASE" }
		optimize "Full"
		flags { "NoIncrementalLink" }

	filter "configurations:DebugASAN"
 		runtime "Debug"
		defines { "_DEBUG" }
		optimize "Off"
		flags { "NoRuntimeChecks", "NoIncrementalLink"}
		sanitize { "Address" }
		removeflags {"FatalWarnings"}

	filter {}

	project (ENGINE_NAME)
		location (ROOT .. ENGINE_NAME)
		pchheader ("stdafx.h")
		pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")
		systemversion (WIN_SDK)
		kind "WindowedApp"

		includedirs { "$(ProjectDir)" }

		for i, dir in pairs(os.matchdirs(SOURCE_DIR .. "External/*")) do
			dirname = string.explode(dir, "/")
			dir = dirname[#dirname]
			includedirs ("$(ProjectDir)External/" .. dir)
		end

		files
		{ 
			(SOURCE_DIR .. "**.h"),
			(SOURCE_DIR .. "**.hpp"),
			(SOURCE_DIR .. "**.cpp"),
			(SOURCE_DIR .. "**.inl"),
			(SOURCE_DIR .. "**.c"),
			(SOURCE_DIR .. "**.natvis"),
			(SOURCE_DIR .. "**.editorconfig"),
		}

		filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
			removeflags "FatalWarnings"
			warnings "Default"
		filter {}

		includedirs "$(ProjectDir)Resources/Shaders/Interop"

		-- D3D12
		includedirs "$(SolutionDir)Libraries/D3D12/include"
		runtimeDependency("D3D12/bin/D3D12Core.dll", "D3D12")
		runtimeDependency("D3D12/bin/d3d12SDKLayers.dll", "D3D12")
		links {	"d3d12.lib", "dxgi", "dxguid" }

		-- Pix
		includedirs "$(SolutionDir)Libraries/Pix/include"
		libdirs "$(SolutionDir)Libraries/Pix/lib"
		runtimeDependency("Pix/bin/WinPixEventRuntime.dll", "")
		links { "WinPixEventRuntime" }

		-- DXC
		includedirs "$(SolutionDir)Libraries/Dxc/include"
		runtimeDependency ("Dxc/bin/dxcompiler.dll", "")
		runtimeDependency ("Dxc/bin/dxil.dll", "")

		-- DirectXMath
		includedirs "$(SolutionDir)Libraries/DirectXMath/include"


newaction {
	trigger     = "clean",
	description = "Remove all binaries and generated files",

	execute = function()
		os.rmdir("../Build")
		os.rmdir("../ipch")
		os.rmdir("../.vs")
		os.remove("../*.sln")
		os.remove(SOURCE_DIR .. "*.vcxproj.*")
	end
}
			
--------------------------------------------------------