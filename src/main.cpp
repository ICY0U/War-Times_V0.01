#include "Core/Application.h"
#include "Util/Log.h"
#include <Windows.h>

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Enable console output in debug builds
#ifdef _DEBUG
    AllocConsole();
    FILE* pFile = nullptr;
    freopen_s(&pFile, "CONOUT$", "w", stdout);
    freopen_s(&pFile, "CONOUT$", "w", stderr);
#endif

    LOG_INFO("=== War Times V0.01 ===");
    LOG_INFO("Starting up...");

    auto& app = WT::Application::Get();

    if (!app.Init(hInstance, 1920, 1080)) {
        LOG_ERROR("Failed to initialize application!");
        MessageBox(nullptr, L"Failed to initialize application.", L"Error", MB_ICONERROR);
        return -1;
    }

    int result = app.Run();

    app.Shutdown();

    LOG_INFO("Exiting with code %d", result);

#ifdef _DEBUG
    FreeConsole();
#endif

    return result;
}
