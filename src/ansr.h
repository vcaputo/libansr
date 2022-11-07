/*
 *  Copyright (C) 2022 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 3 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASNR_H
#define _ANSR_H

typedef struct ansr_conf_t {
	unsigned	screen_width, screen_lines;	/* explicit overrides, 0 for SAUCE or hard-coded (80x24) */
} ansr_conf_t;

typedef enum ansr_color_t {
	ANSR_COLOR_BLACK,
	ANSR_COLOR_RED,
	ANSR_COLOR_GREEN,
	ANSR_COLOR_YELLOW,
	ANSR_COLOR_BLUE,
	ANSR_COLOR_MAGENTA,
	ANSR_COLOR_CYAN,
	ANSR_COLOR_WHITE,
} ansr_color_t;

typedef struct ansr_disp_state_t {
	struct {
		ansr_color_t	fg, bg;
	} colors;
	struct {
		unsigned	bold:1;
		unsigned	faint:1;
		unsigned	italic:1;
		unsigned	underline:1;
		unsigned	slow_blink:1;
		unsigned	rapid_blink:1;
		unsigned	invert:1;
		unsigned	conceal:1;
		unsigned	strikeout:1;
		unsigned	double_underline:1;
		unsigned	proportional:1;
		unsigned	framed:1;
		unsigned	encircled:1;
		unsigned	overlined:1;
		unsigned	ideogram_underline:1;
		unsigned	ideogram_double_underline:1;
		unsigned	ideogram_overline:1;
		unsigned	ideogram_double_overline:1;
		unsigned	ideogram_stress:1;
		unsigned	superscript:1;
		unsigned	subscript:1;
	} attrs;
} ansr_disp_state_t;

typedef struct ansr_char_t {
	char			code;
	ansr_disp_state_t	disp_state;
} ansr_char_t;

typedef struct ansr_row_t {
	unsigned		width, allocated_width;
	ansr_char_t		cols[];
} ansr_row_t;

typedef struct ansr_t {
	ansr_conf_t		conf;
	unsigned		height, allocated_height;
	ansr_row_t		**rows;
} ansr_t;

ansr_t * ansr_new(ansr_conf_t *conf, char *input, size_t input_len);
int ansr_write(ansr_t *ansr, char *input, size_t input_len);
ansr_t * ansr_free(ansr_t *ansr);

#endif
