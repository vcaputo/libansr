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

/*
references:
https://en.wikipedia.org/wiki/Caret_notation
https://en.wikipedia.org/wiki/ISO_2022
https://en.wikipedia.org/wiki/C0_and_C1_control_codes
https://en.wikipedia.org/wiki/ANSI_escape_code
https://www.acid.org/info/sauce/sauce.htm
*/

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ansr.h"

#define ANSR_MIN_ALLOC_PARAMS	2
#define ANSR_MIN_ALLOC_ROWS	64
#define ANSR_MIN_ALLOC_COLS	80

#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

typedef enum ansr_state_t {
	ANSR_STATE_INPUT,
	ANSR_STATE_EOF,
	ANSR_STATE_ESCAPE,
	ANSR_STATE_CSI,
} ansr_state_t;

typedef struct _ansr_t {
	ansr_t			public;
	ansr_state_t		state;
	ansr_disp_state_t	disp_state;
	unsigned		cursor_x, cursor_y;
	size_t			n_params_allocated, n_params;
	unsigned		accumulator;
	char			*params;
	ansr_conf_t		conf;
} _ansr_t;


static ansr_conf_t ansr_conf_defaults = {
	.screen_width = 80,
	.screen_lines = 24,
};


/* create a new ansi renderer of width,height dimensions, starts completely cleared.
 * if input is non-NULL it will be applied to the newly created ans.
 */
ansr_t * ansr_new(ansr_conf_t *conf, char *input, size_t input_len)
{
	_ansr_t	*_ansr;

	assert(!input || input_len > 0);

	if (!conf)
		conf = &ansr_conf_defaults;

	_ansr = calloc(1, sizeof(*_ansr));
	if (!_ansr)
		return NULL;

	_ansr->conf = *conf;

	if (input && ansr_write(&_ansr->public, input, input_len) < 0)
		return ansr_free(&_ansr->public);

	return &_ansr->public;
}


static int _ansr_params_append(_ansr_t *_ansr, char p)
{
	assert(_ansr);

	if (_ansr->n_params + 1 > _ansr->n_params_allocated) {
		char	*new;
		size_t	newsize = MAX(ANSR_MIN_ALLOC_PARAMS, _ansr->n_params_allocated * 2);

		new = realloc(_ansr->params, newsize);
		if (!new)
			return -ENOMEM;

		_ansr->n_params_allocated = newsize;
		_ansr->params = new;
	}

	_ansr->params[_ansr->n_params++] = p;

	return 0;
}


static int _ansr_params_append_accumulator(_ansr_t *_ansr)
{
	int	r;

	if (_ansr->accumulator > 0xff)
		return -EOVERFLOW;

	r = _ansr_params_append(_ansr, _ansr->accumulator);
	if (r < 0)
		return r;

	_ansr->accumulator = 0;

	return 0;
}


static inline void _ansr_sgr_reset(_ansr_t *_ansr)
{
	_ansr->disp_state = (ansr_disp_state_t){ .colors = { .fg = ANSR_COLOR_WHITE } };
}


static inline void _ansr_sgr(_ansr_t *_ansr)
{
	if (!_ansr->n_params) /* SGR with zero params is assumed to be a reset; SGR 0 */
		return _ansr_sgr_reset(_ansr);

	for (size_t i = 0; i < _ansr->n_params; i++) {
		char	p = _ansr->params[i];

		switch (p) {
		case 0: /* reset */
			_ansr_sgr_reset(_ansr);
			break;

		case 1: /* Bold or increased intensity */
			_ansr->disp_state.attrs.bold = 1;
			break;

		case 2: /* Faint, decreased intensity, or dim */
			_ansr->disp_state.attrs.faint = 1;
			break;

		case 3: /* Italic */
			_ansr->disp_state.attrs.italic = 1;
			break;

		case 4: /* Underline */
			_ansr->disp_state.attrs.underline = 1;
			break;

		case 5: /* Slow blink (< 150 times per minute) */
			_ansr->disp_state.attrs.slow_blink = 1;
			break;

		case 6: /* rapid blink (>= 150 per minute) */
			_ansr->disp_state.attrs.rapid_blink = 1;
			break;

		case 7: /* reverse/invert video */
			_ansr->disp_state.attrs.invert = 1;
			break;

		case 8: /* conceal or hide */
			_ansr->disp_state.attrs.conceal = 1;
			break;

		case 9: /* strikeout */
			_ansr->disp_state.attrs.strikeout = 1;
			break;

		case 10: /* primary (default) font */
			assert(0);
			break;

		case 11 ... 19: /* alternative font 1-9 */
			assert(0);
			break;

		case 20: /* Fraktur (gothic) */
			assert(0);
			break;

		case 21: /* doubly underlined; or: not bold */
			_ansr->disp_state.attrs.double_underline = 1;
			break;

		case 22: /* normal intensity */
			_ansr->disp_state.attrs.bold = 0;
			break;

		case 23: /* neither italic, nor blackletter */
			_ansr->disp_state.attrs.italic = 0;
			break;

		case 24: /* Not underlined, neither singly nor doubly underlined */
			_ansr->disp_state.attrs.underline =
			_ansr->disp_state.attrs.double_underline = 0;
			break;

		case 25: /* not blinking (turn blinking off) */
			_ansr->disp_state.attrs.slow_blink =
			_ansr->disp_state.attrs.rapid_blink = 0;
			break;

		case 26: /* proportional spacing */
			_ansr->disp_state.attrs.proportional = 0;
			break;

		case 27: /* Not reversed */
			_ansr->disp_state.attrs.invert = 0;
			break;

		case 28: /* reveal; not concealed */
			_ansr->disp_state.attrs.conceal = 0;
			break;

		case 29: /* not crossed out */
			_ansr->disp_state.attrs.strikeout = 0;
			break;

		case 30 ... 37: /* set foreground color */
			_ansr->disp_state.colors.fg = p - 30;
			break;

		case 38: /* RGB set foreground color - Next arguments are 5;n or 2;r;g;b XXX ???? */
			assert(0); /* TODO */
/*
ESC[38;5;⟨n⟩m Select foreground color      where n is a number from the table below
ESC[48;5;⟨n⟩m Select background color
  0-  7:  standard colors (as in ESC [ 30–37 m)
  8- 15:  high intensity colors (as in ESC [ 90–97 m)
 16-231:  6 × 6 × 6 cube (216 colors): 16 + 36 × r + 6 × g + b (0 ≤ r, g, b ≤ 5)
232-255:  grayscale from dark to light in 24 steps
*/
			break;

		case 39: /* default foreground color - implementation defined */
			assert(0); /* TODO */
			break;

		case 40 ... 47: /* set background color */
			_ansr->disp_state.colors.bg = p - 40;
			break;

		case 48: /* RGB set background color - Next arguments are 5;n or 2;r;g;b  */
			assert(0); /* TODO */
			break;

		case 49: /* Default background color - implementatoin defined */
			assert(0); /* TODO */
			break;

		case 50: /* disable proportional spacing */
			_ansr->disp_state.attrs.proportional = 0;
			break;

		case 51: /* Framed - "emoji variation selector" in mintty apparently */
			_ansr->disp_state.attrs.framed = 1;
			break;

		case 52: /* encircled */
			_ansr->disp_state.attrs.encircled = 1;
			break;

		case 53: /* Overlined 	Not supported in Terminal.app */
			_ansr->disp_state.attrs.overlined = 1;
			break;

		case 54: /* Neither framed nor encircled 	 */
			_ansr->disp_state.attrs.framed =
			_ansr->disp_state.attrs.encircled = 0;
			break;

		case 55: /* Not overlined 	 */
			_ansr->disp_state.attrs.overlined = 0;
			break;

		case 58: /* Set underline color 	Not in standard; implemented in Kitty, VTE, mintty, and iTerm2.[40][41] Next arguments are 5;n or 2;r;g;b. */
			assert(0); /* TODO */
			break;

		case 59: /* Default underline color 	Not in standard; implemented in Kitty, VTE, mintty, and iTerm2.[40][41] */
			assert(0); /* TODO */
			break;

		case 60: /* Ideogram underline or right side line 	Rarely supported */
			_ansr->disp_state.attrs.ideogram_underline = 1;
			break;

		case 61: /* Ideogram double underline, or double line on the right side */
			_ansr->disp_state.attrs.ideogram_double_underline = 1;
			break;

		case 62: /* Ideogram overline or left side line */
			_ansr->disp_state.attrs.ideogram_overline = 1;
			break;

		case 63: /* Ideogram double overline, or double line on the left side */
			_ansr->disp_state.attrs.ideogram_double_overline = 1;
			break;

		case 64: /* Ideogram stress marking */
			_ansr->disp_state.attrs.ideogram_stress = 1;
			break;

		case 65: /* No ideogram attributes 	Reset the effects of all of 60–64 */
			_ansr->disp_state.attrs.ideogram_underline =
			_ansr->disp_state.attrs.ideogram_double_underline =
			_ansr->disp_state.attrs.ideogram_overline =
			_ansr->disp_state.attrs.ideogram_double_overline =
			_ansr->disp_state.attrs.ideogram_stress = 0;
			break;

		case 73: /* Superscript 	Implemented only in mintty[44] */
			_ansr->disp_state.attrs.superscript = 1;
			break;

		case 74: /* Subscript */
			_ansr->disp_state.attrs.subscript = 1;
			break;

		case 75: /* Neither superscript nor subscript */
			_ansr->disp_state.attrs.superscript =
			_ansr->disp_state.attrs.subscript = 0;
			break;

		case 90 ... 97: /* Set bright foreground color 	Not in standard; originally implemented by aixterm[29] */
			/* why is this distinguished from just setting the fg/bg colors and the bold intesnsity attribute? */
			assert(0); /* TODO */
			break;

		case 100 ... 107: /* Set bright background color  */
			assert(0); /* TODO */
			break;
		}
	}
}


/* add char c to _ansr at current cursor position */
/* expands _ansr->rows/cols as needed */
/* returns -errno on failure (ENOMEM) */
static inline int _ansr_add_char(_ansr_t *_ansr, char c)
{
	if (_ansr->conf.screen_width && _ansr->cursor_x == _ansr->conf.screen_width) {
		_ansr->cursor_x = 0;
		_ansr->cursor_y++;
	}

	/* XXX this is a quick and dirty hack implementation just to get things happening */

	if (_ansr->cursor_y >= _ansr->public.allocated_height) { /* expand rows */
		ansr_row_t	**new;
		size_t		new_height = MAX(ANSR_MIN_ALLOC_ROWS, _ansr->public.allocated_height * 2);

		new = realloc(_ansr->public.rows, new_height * sizeof(ansr_row_t *));
		if (!new)
			return -ENOMEM;

		memset(&new[_ansr->public.allocated_height], 0, sizeof(*new) * (new_height - _ansr->public.allocated_height));
		_ansr->public.allocated_height = new_height;
		_ansr->public.rows = new;
	}

	if (_ansr->cursor_y >= _ansr->public.height)
		_ansr->public.height = _ansr->cursor_y + 1;

	if (!_ansr->public.rows[_ansr->cursor_y] || _ansr->cursor_x >= _ansr->public.rows[_ansr->cursor_y]->allocated_width) { /* expand cols */
		size_t		old_width = _ansr->public.rows[_ansr->cursor_y] ? _ansr->public.rows[_ansr->cursor_y]->allocated_width : 0;
		size_t		new_width = MAX(old_width * 2, ANSR_MIN_ALLOC_COLS);
		ansr_row_t	*new;

		new = realloc(_ansr->public.rows[_ansr->cursor_y], sizeof(ansr_row_t) + new_width * sizeof(ansr_char_t));
		if (!new)
			return -ENOMEM;

		memset(&new->cols[old_width], 0, (new_width - old_width) * sizeof(ansr_char_t));
		if (!_ansr->public.rows[_ansr->cursor_y])
			new->width = 0;
		new->allocated_width = new_width;
		_ansr->public.rows[_ansr->cursor_y] = new;
	}

	_ansr->public.rows[_ansr->cursor_y]->cols[_ansr->cursor_x].code = c;
	_ansr->public.rows[_ansr->cursor_y]->cols[_ansr->cursor_x].disp_state = _ansr->disp_state;

	_ansr->cursor_x++;

	if (_ansr->public.rows[_ansr->cursor_y]->width < _ansr->cursor_x)
		_ansr->public.rows[_ansr->cursor_y]->width = _ansr->cursor_x;

	return 0;
}


/* returns negative value on error */
int ansr_write(ansr_t *ansr, char *input, size_t input_len)
{
	_ansr_t	*_ansr = (_ansr_t *)ansr;

	assert(ansr);
	assert(input);

	for (size_t i = 0; i < input_len; i++) {
		char	c = input[i];

		switch (_ansr->state) {
		case ANSR_STATE_INPUT:
			switch (c) {
			case 0x7: /* BELL - tingaling */
				break;

			case 0x8: /* Backspace - move cursor back horizontally */
				if (_ansr->cursor_x > 0)
					_ansr->cursor_x--;
				break;

			case 0x9: /* HT - horizontal tab */
				assert(0);
				break;

			case 0x0a: /* LF - move to next line, scroll display up if at bottom of screen, no horiz change */
				/* TODO: scrolling? for now we just always expand rows/cols to fit rendering */
				_ansr->cursor_y++;
				break;

			case 0x0c: /* FF - move to start of new page but not changing horizontally */
				assert(0); /* XXX: there isn't really a concept of a "page" when there's no screen dimensions */
				break;

			case 0x0d: /* CR - move the cursor to column 0 */
				_ansr->cursor_x = 0;
				break;

			case 0x1a: /* SUB / EOF */
				_ansr->state = ANSR_STATE_EOF;
				break;

			case 0x1b: /* ESC */
				_ansr->state = ANSR_STATE_ESCAPE;
				break;

			case 0x7f: /* DEL */
				break;

			case 0x20: /* SP - move cursor forward horizontally (we just add a space char which vis should treat as transparent) */
			default:
				_ansr_add_char(_ansr, c);
				break;
			}
			break;

		case ANSR_STATE_EOF:
			/* just discard everything after EOF.
			 * SAUCE parsing is deliberately not handled by libansr.
			 */
			break;

		case ANSR_STATE_ESCAPE:
			switch (c) {
			/* TODO: "Fe Escape sequences" / C0 C1 handling? */
			case 0x5b: /* '[' */
				_ansr->state = ANSR_STATE_CSI;
				_ansr->accumulator = 0;
				_ansr->n_params = 0;
				break;

			default:
				assert(0);
				break;
			}
			break;

		case ANSR_STATE_CSI:
			switch (c) {
			case 0x30 ... 0x39:	/* CSI "parameter bytes" 0-9 */
				_ansr->accumulator *= 10;
				_ansr->accumulator += c - 0x30;
				break;

			case 0x3a:		/* CSI "parameter bytes" ':' */
				/* TODO: what does ':' do anyways? */
				assert(0);
				break;

			case 0x3b:		/* CSI "parameter bytes" ';' */
				assert(!_ansr_params_append_accumulator(_ansr));
				break;

			case 0x3c ... 0x3f:	/* "private" CSI "parameter bytes" */
				assert(0);	/* TODO: maybe never */
				break;

			case 0x20 ... 0x2f:	/* "nF" sequence intermediate bytes */
				assert(0);	/* TODO: maybe never */
				break;

			case 0x40 ... 0x6f:	/* final bytes, append accumulator */
			case 0x70 ... 0x7e:	/* "private" final bytes */
				assert(!_ansr_params_append_accumulator(_ansr));
				break;

			default:
				assert(0);
				break;
			}

			/* final byte switch */
			switch (c) {
			/* 0x40 ... 0x6f:	final bytes */
			case 0x41: {		/* cursor up N bytes (default 1) */
				unsigned	n = 1;

				if (_ansr->n_params)
					n = _ansr->params[0];

				_ansr->cursor_y -= MIN(_ansr->cursor_y, n);
				_ansr->state = ANSR_STATE_INPUT;
				break;
			}

			case 0x42:		/* cursor down N bytes (default 1) */
				_ansr->cursor_y += _ansr->n_params ? _ansr->params[0] : 1;
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x43:		/* cursor forward N bytes (default 1) */
				_ansr->cursor_x += _ansr->n_params ? _ansr->params[0] : 1;
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x44:		/* cursor back N bytes (default 1) */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x45:		/* cursor start of next N line (default 1) */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x46:		/* cursor start of previous N line (default 1) */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x47:		/* cursor horiz absolute/column N (default 1) */
				_ansr->cursor_x = _ansr->n_params ? (_ansr->params[0] - 1) : 0;
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x48: {		/* cursor position row N col M (N;M) (defaults to 1 when omitted, 1-based coords) */
				unsigned	r = 0, c = 0;

				if (_ansr->n_params >= 1)
					r = _ansr->params[0] - 1;
				if (_ansr->n_params == 2)
					c = _ansr->params[1] - 1;

				_ansr->cursor_y = r;
				_ansr->cursor_x = c;
				_ansr->state = ANSR_STATE_INPUT;
				break;
			}

			case 0x49: /* ?? */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x4a:		/* erase in display, if n is 0 or missing erase from cursor to end of screen.  if n is 1 from cursor to beginning of screen, 2 entire screen, 3 entire and scrollback */
				/* TODO? we don't assert here because some ansis start with erase, but I'm not bothering with actually implementing it */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x4b:		/* erase in line, n=0 or missing erase to end of line,  n=1 to beginning of line, n=2 entire line.  cursor pos doesn't change */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x53:		/* scroll up */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x54:		/* scroll down */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x66:		/* horiz vert position - same as 0x48 */
				assert(0); /* TODO */
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x6d:		/* select graphic rendition n (SGR) */
				_ansr_sgr(_ansr);
				_ansr->state = ANSR_STATE_INPUT;
				break;

			case 0x70 ... 0x7e:	/* "private" final bytes */
				/* TODO: maybe never? or drop on floor (log something?) */
				_ansr->state = ANSR_STATE_INPUT;
				break;
			}

			break;

		default:
			assert(0);
			break;
		}
	}

	return 0;
}


ansr_t * ansr_free(ansr_t *ansr)
{
	_ansr_t	*_ansr = (_ansr_t *)ansr;

	if (_ansr)
		free(_ansr->params);

	free(_ansr);

	return NULL;
}
