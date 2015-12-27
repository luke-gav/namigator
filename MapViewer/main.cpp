#define NOMINMAX

#include <memory>
#include <windows.h>
#include <windowsx.h>
#include <sstream>

#include "resource.h"

#include "Renderer.hpp"
#include "CommonControl.hpp"
#include "parser.hpp"
#include "Output/Continent.hpp"

#include <limits>

#define START_X             100
#define START_Y             100
#define START_WIDTH         1200
#define START_HEIGHT        800

#define CONTROL_WIDTH       300
#define CONTROL_HEIGHT      250

#define CAMERA_STEP         2.f

// FIXME: Amount to shift control window leftwards.  Find out proper solution for this later!
#define MAGIC_LEFT_SHIFT    15

HWND gGuiWindow, gControlWindow;

std::unique_ptr<Renderer> gRenderer;
std::unique_ptr<::parser::Continent> gContinent;

// this is the main message handler for the program
LRESULT CALLBACK GuiWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CLOSE:
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return TRUE;
        }

        case WM_MOVING:
        {
            const RECT *rect = (const RECT *)lParam;

            MoveWindow(gControlWindow, rect->right - MAGIC_LEFT_SHIFT, rect->top, CONTROL_WIDTH, CONTROL_HEIGHT, FALSE);

            return TRUE;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case ID_FILE_EXIT:
                    PostQuitMessage(0);
                    return TRUE;
            }

            break;
        }

        case WM_KEYDOWN:
        {
            switch ((char)wParam)
            {
                case ' ':
                    gRenderer->GetCamera()->MoveVertical(CAMERA_STEP);
                    return TRUE;
                case 'Z':
                    gRenderer->GetCamera()->MoveVertical(-CAMERA_STEP);
                    return TRUE;
                case 'A':
                    gRenderer->GetCamera()->Yaw(0.1f);
                    return TRUE;
                case 'D':
                    gRenderer->GetCamera()->Yaw(-0.1f);
                    return TRUE;
                case 'W':
                    gRenderer->GetCamera()->Pitch(0.1f);
                    return TRUE;
                case 'X':
                    gRenderer->GetCamera()->Pitch(-0.1f);
                    return TRUE;
                //case 'Q':
                //    gRenderer->GetCamera()->LookAtUpDown(-CAMERA_STEP);
                //    return TRUE;
                //case 'E':
                //    gRenderer->GetCamera()->LookAtUpDown(CAMERA_STEP);
                //    return TRUE;
            }
                
            break;
        }

        case WM_MOUSEWHEEL:
        {
            short distance = (short)(wParam >> 16);
            auto key = wParam & 0xFFFF;

            std::wstringstream str;

            str << L"Key: 0x" << std::hex << key << L" pressed.  Distance: " << std::dec << distance;

            MessageBox(hWnd, str.str().c_str(), L"DEBUG", 0);

            return TRUE;
        }
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

enum Buttons : int
{
    LoadPosition,
    LoadADT
};

LRESULT CALLBACK ControlWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_COMMAND:
        {
            switch (wParam)
            {
                case Buttons::LoadPosition:
                {
                    MessageBox(hWnd, L"Not implemented yet.", L"DEBUG", 0);
                    
                    return TRUE;
                }

                case Buttons::LoadADT:
                {
                    gContinent.reset(new ::parser::Continent("Azeroth"));

                    auto const adt = gContinent->LoadAdt(32, 48);
                    
                    float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();

                    for (int x = 0; x < 16; ++x)
                        for (int y = 0; y < 16; ++y)
                        {
                            auto chunk = adt->GetChunk(x, y);

                            std::vector<ColoredVertex> vertices;
                            vertices.reserve(chunk->m_terrainVertices.size());

                            for (unsigned int i = 0; i < chunk->m_terrainVertices.size(); ++i)
                            {
                                if (chunk->m_terrainVertices[i].Z < minZ)
                                    minZ = chunk->m_terrainVertices[i].Z;
                                if (chunk->m_terrainVertices[i].Z > maxZ)
                                    maxZ = chunk->m_terrainVertices[i].Z;

                                vertices.push_back({ chunk->m_terrainVertices[i].X, chunk->m_terrainVertices[i].Y, chunk->m_terrainVertices[i].Z, {0.1334f, 0.69412f, 0.298f, 1.f} });
                            }

                            gRenderer->AddGeometry(vertices, chunk->m_terrainIndices);

                            vertices.clear();
                            vertices.reserve(chunk->m_liquidVertices.size());

                            for (unsigned int i = 0; i < chunk->m_liquidVertices.size(); ++i)
                                vertices.push_back({ chunk->m_liquidVertices[i].X, chunk->m_liquidVertices[i].Y, chunk->m_liquidVertices[i].Z, { 0.24706f, 0.28235f, 0.8f, 1.f } });

                            gRenderer->AddGeometry(vertices, chunk->m_liquidIndices);
                        }

                    gRenderer->GetCamera()->Move(adt->MaxX, adt->MaxY, maxZ + 1.1f*(maxZ - minZ));
                    gRenderer->GetCamera()->LookAt((adt->MaxX + adt->MinX) / 2.f, (adt->MaxY + adt->MinY) / 2.f, (maxZ + minZ) / 2.f);

                    return TRUE;
                }
            }
        }
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void InitializeWindows(HINSTANCE hInstance, HWND &guiWindow, HWND &controlWindow)
{
    WNDCLASSEX wc;

    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GuiWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = L"DXWindow";
    wc.hIconSm = (HICON)LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 16, 16, 0);
    wc.hIcon = (HICON)LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 32, 32, 0);
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);

    RegisterClassEx(&wc);

    RECT wr = { START_X, START_Y, START_X + START_WIDTH, START_Y + START_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, true);

    guiWindow = CreateWindowEx(WS_EX_RIGHTSCROLLBAR,
        L"DXWindow",
        L"CMaNGOS Map Debugging Interface",
        WS_OVERLAPPEDWINDOW,
        wr.left,
        wr.top,
        wr.right - wr.left,
        wr.bottom - wr.top,
        HWND_DESKTOP,
        nullptr,
        hInstance,
        nullptr);

    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ControlWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = L"ControlWindow";
    wc.hIconSm = (HICON)LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 16, 16, 0);
    wc.hIcon = (HICON)LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 32, 32, 0);

    RegisterClassEx(&wc);

    controlWindow = CreateWindowEx(WS_EX_RIGHTSCROLLBAR,
        L"ControlWindow",
        L"Control",
        (WS_BORDER | WS_CAPTION) & (~WS_ICONIC),
        wr.right - MAGIC_LEFT_SHIFT,
        wr.top,
        CONTROL_WIDTH,
        CONTROL_HEIGHT,
        HWND_DESKTOP,
        nullptr,
        hInstance,
        nullptr);
}

// the entry point for any Windows program
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    ::parser::Parser::Initialize();

    InitializeWindows(hInstance, gGuiWindow, gControlWindow);

    ShowWindow(gGuiWindow, nCmdShow);
    ShowWindow(gControlWindow, nCmdShow);

    // set up and initialize Direct3D
    
    gRenderer.reset(new Renderer(gGuiWindow));

    // set up and initialize our Windows common control API for the control window
    CommonControl controls(gControlWindow);

    controls.AddLabel(L"Select Continent:", 10, 12, 100, 20);

    std::vector<std::wstring> continents;
    continents.push_back(L"000 Azeroth");
    continents.push_back(L"001 Kalimdor");
    continents.push_back(L"013 Test");
    continents.push_back(L"025 Scott test");
    continents.push_back(L"029 Test");
    continents.push_back(L"030 PVPzone01 (Alterac Valley)");
    continents.push_back(L"033 Shadowfang");
    continents.push_back(L"034 stormwindjail");
    continents.push_back(L"035 stormwindprison");
    continents.push_back(L"036 deadminesInstance");
    continents.push_back(L"037 PVPZone02 (Azshara Crater)");
    continents.push_back(L"043 WailingCaverns");
    continents.push_back(L"489 PVPzone03 (Warsong Gulch)");
    continents.push_back(L"529 PVPzone04 (Arathi Basin)");
    continents.push_back(L"530 expansion01 (Outland");
    continents.push_back(L"571 Northrend");

    controls.AddComboBox(L"Continent", continents, 115, 10, 155, 150);

    controls.AddLabel(L"Position:", 10, 10, 300, 20);

    controls.AddLabel(L"X:", 10, 47, 20, 20);
    controls.AddTextBox(L"X", L"", 25, 45, 75, 20);

    controls.AddLabel(L"Y:", 10, 72, 20, 20);
    controls.AddTextBox(L"Y", L"", 25, 70, 75, 20);

    controls.AddLabel(L"Z:", 10, 97, 20, 20);
    controls.AddTextBox(L"Z", L"", 25, 95, 75, 20);

    controls.AddButton(L"Load Position", Buttons::LoadPosition, 115, 92, 100, 25);

    controls.AddLabel(L"X:", 10, 147, 20, 20);
    controls.AddTextBox(L"ADT_X", L"32", 25, 145, 75, 20);

    controls.AddLabel(L"Y:", 10, 172, 20, 20);
    controls.AddTextBox(L"ADT_Y", L"32", 25, 170, 75, 20);

    controls.AddButton(L"Load ADT", Buttons::LoadADT, 115, 167, 100, 25);

    // enter the main loop:

    MSG msg;

    while (true)
    {
        gRenderer->Render();

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                break;
        }
    };

    return msg.wParam;
}