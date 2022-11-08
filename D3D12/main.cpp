#include "stdafx.h"

#include "Core/Input.h"
#include "Core/Console.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include "Core/ConsoleVariables.h"
#include "Core/Window.h"
#include "DemoApp.h"

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#define BREAK_ON_ALLOC 0

int WINAPI WinMain(_In_ HINSTANCE /*hInstance*/, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int /*nShowCmd*/)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#if BREAK_ON_ALLOC > 0
	_CrtSetBreakAlloc(BREAK_ON_ALLOC);
#endif
#endif

	Thread::SetMainThread();
	CommandLine::Parse(GetCommandLineA());

	if (CommandLine::GetBool("debuggerwait"))
	{
		while (!::IsDebuggerPresent())
		{
			::Sleep(100);
		}
	}

	Console::Initialize();
	ConsoleManager::Initialize();
	TaskQueue::Initialize(std::thread::hardware_concurrency());

	Vector2i displayDimensions = Window::GetDisplaySize();

	Window app((int)(displayDimensions.x * 0.7f), (int)(displayDimensions.y * 0.7f));
	app.SetTitle("D3D12");

	DemoApp graphics(app.GetNativeWindow(), app.GetRect());

	app.OnKeyInput += [](uint32 character, bool isDown) { Input::Instance().UpdateKey(character, isDown); };
	app.OnMouseInput += [](uint32 mouse, bool isDown) { Input::Instance().UpdateMouseKey(mouse, isDown); };
	app.OnMouseMove += [](uint32 x, uint32 y) { Input::Instance().UpdateMousePosition((float)x, (float)y); };
	app.OnResizeOrMove += [&graphics](uint32 width, uint32 height) { graphics.OnResizeOrMove(width, height); };
	app.OnMouseScroll += [](float wheel) { Input::Instance().UpdateMouseWheel(wheel); };

	Time::Reset();

	while (app.PollMessages())
	{
		Time::Tick();
		graphics.Update();
		Input::Instance().Update();
	}

	TaskQueue::Shutdown();
	Console::Shutdown();

	return 0;
}
