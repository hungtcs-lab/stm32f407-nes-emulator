/*===================================================================*/
/*                                                                   */
/*  InfoNES_System.h : The function which depends on a system        */
/*                                                                   */
/*  2000/05/29  InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef InfoNES_SYSTEM_H_INCLUDED
#define InfoNES_SYSTEM_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES_Types.h"

/*-------------------------------------------------------------------*/
/*  Palette data                                                     */
/*-------------------------------------------------------------------*/
extern WORD NesPalette[];

/*-------------------------------------------------------------------*/
/*  Function prototypes                                              */
/*-------------------------------------------------------------------*/

/* Menu screen */
int InfoNES_Menu();

/* Read ROM image file */
int InfoNES_ReadRom( const char *pszFileName );

/* Release a memory for ROM */
void InfoNES_ReleaseRom();

/* Transfer the contents of work frame on the screen */
void InfoNES_LoadFrame();

/* [port] 渲染完一条扫描线后调用，pLine 为 256 个 RGB555 像素（bit15 = 背景透明标记）*/
void InfoNES_LoadLine( int nLine, const WORD *pLine );

/* [port] 这一行要不要渲染。返回 0 则跳过（屏幕上保留上一帧的内容），用于隔行渲染省时间 */
int InfoNES_ShouldDrawLine( int nLine );

/* Get a joypad state */
void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem );

/* memcpy */
void *InfoNES_MemoryCopy( void *dest, const void *src, int count );

/* memset */
void *InfoNES_MemorySet( void *dest, int c, int count );

/* Print debug message */
void InfoNES_DebugPrint( char *pszMsg );

/* Wait */
void InfoNES_Wait();

/* Sound Initialize */
void InfoNES_SoundInit( void );

/* Sound Open */
int InfoNES_SoundOpen( int samples_per_sync, int sample_rate );

/* Sound Close */
void InfoNES_SoundClose( void );

/* Sound Output 5 Waves - 2 Pulse, 1 Triangle, 1 Noise, 1 DPCM */
void InfoNES_SoundOutput(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5);

/* Print system message */
void InfoNES_MessageBox( char *pszMsg, ... );

#endif /* !InfoNES_SYSTEM_H_INCLUDED */
