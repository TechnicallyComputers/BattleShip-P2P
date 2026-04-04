#define SDL_MAIN_HANDLED
#include "port.h"

#include <libultraship/libultraship.h>
#include <ship/resource/File.h>
#include <string>
#include <vector>

#include "resource/ResourceType.h"
#include "resource/RelocFileFactory.h"

static std::shared_ptr<Ship::Context> sContext;

extern "C" {

int PortInit(int argc, char* argv[]) {
	// Archive paths: the game will look for these O2R files at runtime.
	// "ssb64.o2r" contains extracted game assets (from Torch).
	// Additional O2R files can be added for port-specific resources.
	std::vector<std::string> archivePaths = { "ssb64.o2r" };

	sContext = Ship::Context::CreateInstance(
		"Super Smash Bros. 64",  // Window title
		"ssb64",                 // Short name (used for config paths)
		"ssb64.cfg.json",       // Config file path
		archivePaths
	);

	if (!sContext) {
		return 1;
	}

	// Register SSB64-specific resource factories
	auto loader = sContext->GetResourceManager()->GetResourceLoader();
	loader->RegisterResourceFactory(
		std::make_shared<ResourceFactoryBinaryRelocFileV0>(),
		RESOURCE_FORMAT_BINARY,
		"SSB64Reloc",
		static_cast<uint32_t>(SSB64::ResourceType::SSB64Reloc),
		0
	);

	return 0;
}

void PortShutdown(void) {
	sContext.reset();
}

int PortIsRunning(void) {
	return WindowIsRunning() ? 1 : 0;
}

} // extern "C"

int main(int argc, char* argv[]) {
	// We handle SDL initialization ourselves through libultraship.
	// SDL_MAIN_HANDLED is defined above to prevent SDL from hijacking main().

	// TODO: Initialize port, start game loop.
	// This will eventually call PortInit() followed by syMainLoop()
	// once the boot sequence is restructured for single-threaded PC execution.
	return PortInit(argc, argv);
}
