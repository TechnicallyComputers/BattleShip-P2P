#ifndef PORT_AUDIO_BRIDGE_H
#define PORT_AUDIO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Port-side audio asset loader.
 *
 * Replaces syAudioLoadAssets() for the PC port.  Loads audio BLOBs from the
 * .o2r archive, parses the big-endian N64 binary format (32-bit pointer
 * fields), and constructs native C structs with correct 64-bit pointer width.
 *
 * Called from the PORT guard in syAudioThreadMain (src/sys/audio.c).
 * After this returns the game's audio globals (sSYAudioSequenceBank1/2,
 * sSYAudioSeqFile, FGM tables, Acmd buffers) are populated and ready for
 * syAudioMakeBGMPlayers().
 */
void portAudioLoadAssets(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_AUDIO_BRIDGE_H */
