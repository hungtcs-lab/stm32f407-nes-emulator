/*===================================================================*/
/*                                                                   */
/*  InfoNES_Types.h : Type definitions for InfoNES                   */
/*                                                                   */
/*  2000/5/4    InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef InfoNES_TYPES_H_INCLUDED
#define InfoNES_TYPES_H_INCLUDED

#include <stdint.h>

/* [port] 放到高速/专用内存区的数组（MCU 上指向 CCM RAM），主机上为空 */
#ifndef NES_FAST_RAM
#define NES_FAST_RAM
#endif

/* [port] 像素里的"背景色"标记位。上游是 RGB555 + bit15；
 * MCU 上改成 RGB565，标记放在绿色最低位 bit5（NesPalette 里该位恒为 0），推屏时不用再转换格式 */
#ifndef NES_BG_FLAG
#define NES_BG_FLAG 0x8000
#endif

/*-------------------------------------------------------------------*/
/*  Type definition                                                  */
/*-------------------------------------------------------------------*/
#ifndef DWORD
typedef uint32_t DWORD;   /* [port] 原为 unsigned long */
#endif /* !DWORD */

#ifndef WORD
typedef uint16_t WORD;
#endif /* !WORD */

#ifndef BYTE
typedef uint8_t  BYTE;
#endif /* !BYTE */

/*-------------------------------------------------------------------*/
/*  NULL definition                                                  */
/*-------------------------------------------------------------------*/
#ifndef NULL
#define NULL  0
#endif /* !NULL */

#endif /* !InfoNES_TYPES_H_INCLUDED */
