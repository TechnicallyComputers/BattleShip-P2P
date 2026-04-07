/**
 * gfx_trace_plugin.c — Mupen64Plus stub GFX plugin
 *
 * A minimal no-op video plugin required by M64P when using the RSP trace plugin.
 * This plugin does not render anything. The actual DL tracing is done by the
 * companion RSP plugin (mupen64plus-rsp-trace).
 */
#include "m64p_plugin_api.h"

#include <stdio.h>
#include <string.h>

static GFX_INFO sGfxInfo;
static int sInitialized = 0;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle handle,
                                     void *context,
                                     m64p_debug_callback debug_cb)
{
	if (sInitialized) return M64ERR_ALREADY_INIT;
	sInitialized = 1;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
	sInitialized = 0;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version,
                                        int *api_version, const char **name,
                                        int *caps)
{
	if (type)        *type = M64PLUGIN_GFX;
	if (version)     *version = 0x010000;
	if (api_version) *api_version = 0x020600;
	if (name)        *name = "Stub Video (for RSP Trace)";
	if (caps)        *caps = 0;
	return M64ERR_SUCCESS;
}

EXPORT int CALL InitiateGFX(GFX_INFO gfx_info)
{
	sGfxInfo = gfx_info;
	return 1;
}

EXPORT void CALL MoveScreen(int x, int y) {}
EXPORT void CALL ProcessDList(void) {}
EXPORT void CALL ProcessRDPList(void) {}
EXPORT void CALL ShowCFB(void) {}
EXPORT void CALL UpdateScreen(void) {}
EXPORT void CALL ViStatusChanged(void) {}
EXPORT void CALL ViWidthChanged(void) {}
EXPORT void CALL ChangeWindow(void) {}
EXPORT void CALL RomOpen(void) {}
EXPORT void CALL RomClosed(void) {}
EXPORT void CALL SetRenderingCallback(void (*callback)(int)) {}
EXPORT void CALL ResizeVideoOutput(int width, int height) {}
EXPORT void CALL FBRead(unsigned int addr) {}
EXPORT void CALL FBWrite(unsigned int addr, unsigned int size) {}
EXPORT void CALL FBGetFrameBufferInfo(void *info) {}

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
	if (width) *width = 320;
	if (height) *height = 240;
}
