// RSP plugin entry points for the macOS SDL3 build. The RSP core itself lives in
// Project64-rsp-core; this file only exposes the zilmar-spec exports the frontend
// resolves with dlsym.
#include <Project64-rsp-core/RSPInfo.h>
#include <Project64-rsp-core/Version.h>
#include <Settings/Settings.h>
#include <stdio.h>

/*
Function: CloseDLL
Purpose: This function is called when the emulator is closing down allowing the DLL to de-initialize.
Input: None
Output: None
*/
EXPORT void CloseDLL(void)
{
    FreeRSP();
}

/*
Function: DllAbout
Purpose: This function is optional function that is provided to give further information about the DLL.
Input: A handle to the window that calls this function.
Output: None
*/
EXPORT void DllAbout(void * /*hParent*/)
{
}

/*
Function: GetDllInfo
Purpose: This function allows the emulator to gather information about the DLL by filling in the PluginInfo structure.
Input: A pointer to a PLUGIN_INFO structure that needs to be filled by the function. (see def above)
Output: None
*/
EXPORT void GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    PluginInfo->Version = RSP_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_RSP;
    snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "RSP Basic Plugin %s", VER_FILE_VERSION_STR);
    PluginInfo->Reserved2 = false;
    PluginInfo->Reserved1 = true;
}

/*
Function: InitiateRSP
Purpose: This function is called when the DLL is started to give information from the emulator that the N64 RSP interface needs.
Input: Rsp_Info is passed to this function which is defined above.
CycleCount is the number of cycles between switching control between the RSP and r4300i core.
Output: None
*/
EXPORT void InitiateRSP(RSP_INFO Rsp_Info, uint32_t * CycleCount)
{
    InitilizeRSP(Rsp_Info);
    *CycleCount = 0;
}

/*
Function: RomOpen
Purpose: This function is called when a ROM is opened.
Input: None
Output: None
*/
EXPORT void RomOpen(void)
{
    RspRomOpened();
}

/*
Function: RomClosed
Purpose: This function is called when a ROM is closed.
Input: None
Output: None
*/
EXPORT void RomClosed(void)
{
    RspRomClosed();
}

EXPORT void PluginLoaded(void)
{
    RspPluginLoaded();
}

void UseUnregisteredSetting(int /*SettingID*/)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}
