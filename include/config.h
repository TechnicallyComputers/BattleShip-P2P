#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * A catch-all file for configuring various bugfixes and other options
 */

// Endianness
#ifdef PORT
#define IS_BIG_ENDIAN 0
#else
#define IS_BIG_ENDIAN 1
#endif

// Screen Size Defines
#define GS_SCREEN_WIDTH_DEFAULT 320
#define GS_SCREEN_HEIGHT_DEFAULT 240

#endif /* CONFIG_H */
