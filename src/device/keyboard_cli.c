/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Keyboard input for CLI mode.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <signal.h>
#ifdef _WIN32
# include <windows.h>
# include <fcntl.h>
#else
# include <termios.h>
# include <unistd.h>
#endif
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/keyboard.h>
#include <86box/vid_text_render.h>
#include <86box/vnc.h>
#define KEYBOARD_CLI_DEBUG 1

/* Lookup tables for converting escape sequences to X11 keycodes. */
static const uint16_t csi_seqs[] = {
    [1]  = 0xff50, /* Home */
    [2]  = 0xff63, /* Insert */
    [3]  = 0xffff, /* Delete */
    [4]  = 0xff57, /* End */
    [5]  = 0xff55, /* Page Up */
    [6]  = 0xff56, /* Page Down */
    [11] = 0xffbe, /* F1 */
    [12] = 0xffbf, /* F2 */
    [13] = 0xffc0, /* F3 */
    [14] = 0xffc1, /* F4 */
    [15] = 0xffc2, /* F5 */
    [17] = 0xffc3, /* F6 */
    [18] = 0xffc4, /* F7 */
    [19] = 0xffc5, /* F8 */
    [20] = 0xffc6, /* F9 */
    [21] = 0xffc7, /* F10 */
    [23] = 0xffc8, /* F11 */
    [24] = 0xffc9, /* F12 */
    [25] = 0xfebe, /* F13 / Ctrl+F1 */
    [26] = 0xfebf, /* F14 / Ctrl+F2 */
    [28] = 0xfec0, /* F15 / Ctrl+F3 */
    [29] = 0xfec1, /* F16 / Ctrl+F4 */
    [31] = 0xfec2, /* F17 / Ctrl+F5 */
    [32] = 0xfec3, /* F18 / Ctrl+F6 */
    [33] = 0xfec4, /* F19 / Ctrl+F7 */
    [34] = 0xfec5, /* F20 / Ctrl+F8 */
};
static const uint16_t csi_modifiers[][4] = {
    [2]  = {0xffe1, 0,      0,      0     }, /* Shift */
    [3]  = {0xffe9, 0,      0,      0     }, /* Alt */
    [4]  = {0xffe1, 0xffe9, 0,      0     }, /* Shift+Alt */
    [5]  = {0xffe3, 0,      0,      0     }, /* Ctrl */
    [6]  = {0xffe1, 0xffe3, 0,      0     }, /* Shift+Ctrl */
    [7]  = {0xffe9, 0xffe3, 0,      0     }, /* Alt+Ctrl */
    [8]  = {0xffe1, 0xffe9, 0xffe3, 0     }, /* Shift+Alt+Ctrl */
    [9]  = {0xffe7, 0,      0,      0     }, /* Meta */
    [10] = {0xffe7, 0xffe1, 0,      0     }, /* Meta+Shift */
    [11] = {0xffe7, 0xffe9, 0,      0     }, /* Meta+Alt */
    [12] = {0xffe7, 0xffe9, 0xffe1, 0     }, /* Meta+Alt+Shift */
    [13] = {0xffe1, 0xffe3, 0,      0     }, /* Meta+Ctrl */
    [14] = {0xffe1, 0xffe3, 0xffe1, 0     }, /* Meta+Ctrl+Shift */
    [15] = {0xffe1, 0xffe3, 0xffe9, 0     }, /* Meta+Ctrl+Alt */
    [16] = {0xffe1, 0xffe9, 0xffe9, 0xffe1}, /* Meta+Ctrl+Alt+Shift */
};
static const uint16_t ss3_seqs[] = {
    [' '] = 0xff80, /* Space */
    ['j'] = 0xffaa, /* * */
    ['k'] = 0xffab, /* + */
    ['l'] = 0x002c, /* , */
    ['m'] = 0xffad, /* - */
    ['A'] = 0xff52, /* Up */
    ['B'] = 0xff54, /* Down */
    ['C'] = 0xff53, /* Right */
    ['D'] = 0xff51, /* Left */
    ['E'] = 0xff58, /* Begin / Keypad 5 */
    ['F'] = 0xff57, /* End */
    ['G'] = 0xff58, /* Begin / Keypad 5 (sent by PuTTY) */
    ['H'] = 0xff50, /* Home */
    ['I'] = 0xff89, /* Tab */
    ['M'] = 0xff8d, /* Enter */
    ['P'] = 0xffbe, /* F1 */
    ['Q'] = 0xffbf, /* F2 */
    ['R'] = 0xffc0, /* F3 */
    ['S'] = 0xffc1, /* F4 */
    ['X'] = 0xffbd, /* = */
};


static volatile thread_t *keyboard_cli_thread;
static int	in_escape = 0, escape_buf_pos = 0, quit_escape = 0;
static char	escape_buf[256];


static void
keyboard_cli_send(uint16_t key)
{
    /* Disarm quit escape sequence. */
    quit_escape = 0;

    /* Press key, then release after 50ms. */
    vnc_kbinput(1, key);
    plat_delay_ms(50);
    vnc_kbinput(0, key);
}


static void
keyboard_cli_signal(int sig)
{
    vnc_kbinput(1, 0xffe3);
    switch (sig) {
	case 0: /* EOF (special case caught in character reading loop) */
		keyboard_cli_send('d');
		break;
#ifdef SIGINT
	case SIGINT: /* Ctrl+C */
		keyboard_cli_send('c');
		break;
#endif
#ifdef SIGSTOP
	case SIGSTOP: /* Ctrl+S */
		keyboard_cli_send('s');
		break;
#endif
#ifdef SIGTSTP
	case SIGTSTP: /* Ctrl+Z */
		keyboard_cli_send('z');
		break;
#endif
#ifdef SIGQUIT
	case SIGQUIT: /* Ctrl+\ */
		keyboard_cli_send('\\');
		break;
#endif
    }
    vnc_kbinput(0, 0xffe3);
}


void
keyboard_cli_process(void *priv)
{
    char c;
    int i, elems, num, modifier;
    uint16_t code;

    while (1) {
	c = getchar();

	/* Check if we're in an escape sequence instead of receiving an arbitrary character. */
	if (in_escape) {
		if (((c >= '0') && (c <= '9')) || (c == ';') || (!escape_buf_pos && (c != 0x1b))) {
			/* Add character to escape sequence buffer. */
			if (escape_buf_pos < (sizeof(escape_buf) - 2))
				escape_buf[escape_buf_pos++] = c;
		} else {
			/* Finish escape sequence. */
			escape_buf[escape_buf_pos++] = c;
			escape_buf[escape_buf_pos] = '\0';
#ifdef KEYBOARD_CLI_DEBUG
			fprintf(TEXT_RENDER_OUTPUT, "\033]0;Escape: ");
			for (i = 0; i < strlen(escape_buf); i++) {
				if ((escape_buf[i] < 0x21) || (escape_buf[i] > 0x7e))
					fprintf(TEXT_RENDER_OUTPUT, "[%02X]", escape_buf[i]);
				else
					fprintf(TEXT_RENDER_OUTPUT, "%c", escape_buf[i]);
			}
			fprintf(TEXT_RENDER_OUTPUT, "\a");
			fflush(TEXT_RENDER_OUTPUT);
#endif

			/* Interpret escape sequence. */
			if (!strcasecmp(escape_buf, "qq")) { /* quit sequence */
				/* Quit on the second consecutive quit escape sequence. */
				if (++quit_escape >= 2) {
					/* Reset terminal. */
					fprintf(TEXT_RENDER_OUTPUT, "\033[0m\033[2J\033[3J\033[1;1H\033[25h\033c");
					fflush(TEXT_RENDER_OUTPUT);

					/* Initiate quit. */
					is_quit = 1;
					return;
				}
			} else {
				/* Disarm quit escape sequence. */
				quit_escape = 0;

				if (escape_buf[0] == 0x1b) { /* escaped Escape key */
					keyboard_cli_send(0xff1b);
				} else if (escape_buf_pos > 1) {
					if ((escape_buf[0] == '[') && (escape_buf[escape_buf_pos - 1] == '~')) { /* numeric CSI */
						/* Parse numeric CSI sequence with optional modifier. */
						elems = sscanf(escape_buf, "[%d;%d~", &num, &modifier);
						if (elems) {
							/* Remove modifier if invalid. */
							if ((elems < 2) ||
							    (modifier >= (sizeof(csi_modifiers) / sizeof(csi_modifiers[0]))) ||
							    !csi_modifiers[modifier][0])
								modifier = 0;

							/* Determine keycode. */
							if (num < (sizeof(csi_seqs) / sizeof(csi_seqs[0])))
								code = csi_seqs[num];
							else
								code = 0;

							/* Account for special Ctrl+Fx keycodes. */
							if ((code >> 8) == 0xfe) {
								code |= 0xff00;
								modifier = 5;
							}

							/* Press modifier keys. */
							if (modifier) {
								for (i = 0; i < 4; i++) {
									if (csi_modifiers[modifier][i])
										vnc_kbinput(1, csi_modifiers[modifier][i]);
								}
							}

							/* Press and release key. */
							if (code)
								keyboard_cli_send(code);

							/* Release modifier keys. */
							if (modifier) {
								for (i = 3; i >= 0; i--) {
									if (csi_modifiers[modifier][i])
										vnc_kbinput(0, csi_modifiers[modifier][i]);
								}
							}
						}
					} else if ((escape_buf[0] == 'O') || (escape_buf[0] == '[')) { /* SS3 or non-numeric CSI */
						/* Determine keycode. */
						if (escape_buf[1] < (sizeof(ss3_seqs) / sizeof(ss3_seqs[0])))
							code = ss3_seqs[(uint8_t) escape_buf[1]];
						else
							code = 0;

						/* Press and release key. */
						if (code)
							keyboard_cli_send(code);
					}
				}
			}

			/* Finish escape sequence. */
			in_escape = escape_buf_pos = 0;
		}
		continue;
	}

#ifdef KEYBOARD_CLI_DEBUG
	fprintf(TEXT_RENDER_OUTPUT, "\033]0;Key: %02X\a", c);
	fflush(TEXT_RENDER_OUTPUT);
#endif

	/* Receive arbitrary character. */
	switch (c) {
		case 0x0a: /* Enter */
			keyboard_cli_send(0xff0d);
			break;

		case 0x1b: /* Escape */
			in_escape = c;
			break;

		case 0x20 ... 0x7e: /* ASCII */
			keyboard_cli_send(c);
			break;

		case 0x7f: /* Backspace */
			keyboard_cli_send(0xff08);
			break;

		case EOF: /* EOF (Ctrl+D) */
			keyboard_cli_signal(0);
			break;
	}
    }
}


void
keyboard_cli_init()
{
    /* Enable raw input. */
#ifdef _WIN32
    HANDLE h;
    FILE *fp = NULL;
    int i;
    if ((h = GetStdHandle(STD_INPUT_HANDLE)) != NULL) {
	/* We got the handle, now open a file descriptor. */
	SetConsoleMode(h, 0);
	if ((i = _open_osfhandle((intptr_t)h, _O_TEXT)) != -1) {
		/* We got a file descriptor, now allocate a new stream. */
		if ((fp = _fdopen(i, "r")) != NULL) {
			/* Got the stream, re-initialize stdin without it. */
			(void)freopen("CONIN$", "r", stdin);
			setvbuf(stdin, NULL, _IONBF, 0);
		}
	}
    }
    if (fp != NULL) {
	fclose(fp);
	fp = NULL;
    }
#else
    struct termios ios;
    tcgetattr(STDIN_FILENO, &ios);
    ios.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &ios);
#endif

    /* Intercept signals to send their Ctrl+x sequences. */
#ifdef SIGINT
    signal(SIGINT, keyboard_cli_signal);
#endif
#ifdef SIGSTOP
    signal(SIGSTOP, keyboard_cli_signal);
#endif
#ifdef SIGTSTP
    signal(SIGTSTP, keyboard_cli_signal);
#endif
#ifdef SIGQUIT
    signal(SIGQUIT, keyboard_cli_signal);
#endif

    /* Start input processing thread. */
    keyboard_cli_thread = thread_create(keyboard_cli_process, NULL);
}
