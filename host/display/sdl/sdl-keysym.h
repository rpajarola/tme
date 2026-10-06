/* host/display/sdl/sdl-keysym.h - X keysyms used by the SDL display: */

/* tme keyboards use X keysym values.  these are the ones the SDL
   display maps keys to, for hosts without <X11/keysym.h> or
   <rfb/keysym.h>, like macOS.  the values are from X11's
   keysymdef.h: */

#ifndef _HOST_DISPLAY_SDL_KEYSYM_H
#define _HOST_DISPLAY_SDL_KEYSYM_H

#define XK_Alt_L         0xffe9
#define XK_Alt_R         0xffea
#define XK_BackSpace     0xff08
#define XK_Caps_Lock     0xffe5
#define XK_Clear         0xff0b
#define XK_Control_L     0xffe3
#define XK_Control_R     0xffe4
#define XK_Delete        0xffff
#define XK_Down          0xff54
#define XK_End           0xff57
#define XK_Escape        0xff1b
#define XK_F1            0xffbe
#define XK_F10           0xffc7
#define XK_F11           0xffc8
#define XK_F12           0xffc9
#define XK_F13           0xffca
#define XK_F14           0xffcb
#define XK_F15           0xffcc
#define XK_F2            0xffbf
#define XK_F3            0xffc0
#define XK_F4            0xffc1
#define XK_F5            0xffc2
#define XK_F6            0xffc3
#define XK_F7            0xffc4
#define XK_F8            0xffc5
#define XK_F9            0xffc6
#define XK_Help          0xff6a
#define XK_Home          0xff50
#define XK_Insert        0xff63
#define XK_KP_0          0xffb0
#define XK_KP_1          0xffb1
#define XK_KP_2          0xffb2
#define XK_KP_3          0xffb3
#define XK_KP_4          0xffb4
#define XK_KP_5          0xffb5
#define XK_KP_6          0xffb6
#define XK_KP_7          0xffb7
#define XK_KP_8          0xffb8
#define XK_KP_9          0xffb9
#define XK_KP_Add        0xffab
#define XK_KP_Decimal    0xffae
#define XK_KP_Divide     0xffaf
#define XK_KP_Enter      0xff8d
#define XK_KP_Equal      0xffbd
#define XK_KP_Multiply   0xffaa
#define XK_KP_Subtract   0xffad
#define XK_Left          0xff51
#define XK_Mode_switch   0xff7e
#define XK_Num_Lock      0xff7f
#define XK_Page_Down     0xff56
#define XK_Page_Up       0xff55
#define XK_Pause         0xff13
#define XK_Print         0xff61
#define XK_Return        0xff0d
#define XK_Right         0xff53
#define XK_Scroll_Lock   0xff14
#define XK_Shift_L       0xffe1
#define XK_Shift_R       0xffe2
#define XK_Super_L       0xffeb
#define XK_Super_R       0xffec
#define XK_Sys_Req       0xff15
#define XK_Tab           0xff09
#define XK_Up            0xff52

#endif /* !_HOST_DISPLAY_SDL_KEYSYM_H */
