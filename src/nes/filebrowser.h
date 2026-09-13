/* SD 卡文件浏览（只读）
 *
 * 上/下 移动（按住连续）  左/右 翻页  A 进入目录 / 运行 .nes / 查看文件  B 返回上一级
 * 在根目录按 B 退出 */
#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 1：用户选了一个 .nes 要运行，完整路径写进 out；返回 0：用户退出 */
int file_browser(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
