#include <windows.h>
#include <mmsystem.h>
#include <Xinput.h>
#include <ViGEm/Client.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#define PERIOD 1
#define TOLERANCE 0.02

//using Clock = std::chrono::steady_clock; 

//using technique robustSleep from https://blog.bearcats.nl/perfect-sleep-function/
void robustSleep(double seconds) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto target = t0 + std::chrono::nanoseconds(int64_t(seconds * 1e9));

    // sleep
    double ms = seconds * 1000 - (PERIOD + TOLERANCE);
    int ticks = (int)(ms / PERIOD);
    if (ticks > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(ticks * PERIOD));

    // spin
    while (std::chrono::high_resolution_clock::now() < target)
        YieldProcessor();
}

struct TimedState {
    XINPUT_STATE state;
    std::chrono::time_point<std::chrono::steady_clock> timestamp; // using long form for clarity instead of alternative syntax ie Clock::time_point in header
};

std::atomic<bool> running = true;

// Ctrl+C or closing console will allow vigem and clocks to resolve
BOOL WINAPI HandlerRoutine(DWORD signal) {
    switch (signal) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
            running = false;  
            return TRUE;      
        default:
            return FALSE;     
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "Usage: ./Smash-Buffer.exe <delay_ms>\n";
        return 1;
    }

    int delayMs = atoi(argv[1]);
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 500)
    {
        std::cout << "Use a value 500 or below.\n";
        return 1;
    }

    std::vector<TimedState> gpad(delayMs + 1); //ie array size of 6 instead of delay 5ms for modulo 

    int write_index = 0;
    int read_index = 0;

    std::cout << "Delay: " << delayMs << " ms. To exit, use ctrl+c\n";

    // Enable high-resolution timer
    timeBeginPeriod(1);

    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    // --- ViGEm init ---
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client || !VIGEM_SUCCESS(vigem_connect(client))) {
        std::cerr << "ViGEm init failed. Did you download and install Windows vigem drivers?\n";
        timeEndPeriod(1);
        return 1;
    }

    PVIGEM_TARGET target = vigem_target_x360_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(client, target))) {
        std::cerr << "Failed to create virtual controller\n";
        vigem_free(client);
        timeEndPeriod(1);
        return 1;
    }

    Sleep(100); // Sleep allowing enough time for virtual xinput to initialize for XinputGetState below to find it

    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) 
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));
        DWORD result = XInputGetState(i, &state);
        if (result == ERROR_SUCCESS) 
        {
            std::cout << "Controller " << i << " is connected.\n";
        }
    }

    // Outer loop, one millisecond means one state
    while (running)
    {
        // Capture input
        XINPUT_STATE state{};
        if (XInputGetState(0, &state) == ERROR_SUCCESS)
        {
            gpad[write_index] = TimedState{ state, std::chrono::steady_clock::now() }; // Record timestamp
        }

            auto now = std::chrono::steady_clock::now(); // std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();

           
            double elapsed = std::chrono::duration<double, std::milli>(
                now - gpad[read_index].timestamp).count();

            if (elapsed >= delayMs)
            {
                XUSB_REPORT report{};
                report.wButtons = gpad[read_index].state.Gamepad.wButtons;
                report.bLeftTrigger = gpad[read_index].state.Gamepad.bLeftTrigger;
                report.bRightTrigger = gpad[read_index].state.Gamepad.bRightTrigger;
                report.sThumbLX = gpad[read_index].state.Gamepad.sThumbLX;
                report.sThumbLY = gpad[read_index].state.Gamepad.sThumbLY;
                report.sThumbRX = gpad[read_index].state.Gamepad.sThumbRX;
                report.sThumbRY = gpad[read_index].state.Gamepad.sThumbRY;

                vigem_target_x360_update(client, target, report);

                read_index = (read_index + 1) % gpad.size();
            }

        write_index = (write_index + 1) % gpad.size();
        
        robustSleep(0.001); // sleep 1 millisecond
    }

    // --- Cleanup ---
    vigem_target_remove(client, target);
    vigem_target_free(target);
    vigem_disconnect(client);
    vigem_free(client);

    timeEndPeriod(1);

    std::cout << "Exited cleanly.\n";
    return 0;
}