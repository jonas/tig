/* Copyright (c) 2006-2017 Jonas Fonseca <jonas.fonseca@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef TIG_REGISTERS_H
#define TIG_REGISTERS_H

#include "tig/types.h"

/* Index 0 of the register array, corresponding to character key ASCII
 * space, is kept empty and reserved for internal use as "no register".
 */
#define REGISTER_KEY_MIN '!'	/* corresponding to index 1 */
#define REGISTER_KEY_MAX '~'	/* corresponding to index 94 */
#define REGISTER_KEY_OFFSET 0x20
#define SIZEOF_REGISTERS 1 + REGISTER_KEY_MAX - REGISTER_KEY_OFFSET

#define REGISTER_FLAG_OPEN_STR  "=("
#define REGISTER_FLAG_CLOSE_STR ")"
#define REGISTER_ESC_CHAR       '\\'

#define is_register_esc_char(ch) \
	((ch) == REGISTER_ESC_CHAR)

#define is_register_meta_char(ch) \
	(is_register_esc_char(ch) \
	 || ((ch) == REGISTER_FLAG_OPEN_STR[1]) \
	 || ((ch) == REGISTER_FLAG_CLOSE_STR[0]) \
	 || ((ch) == '"') \
	 || ((ch) == '\''))

#define at_register_flag_open(p) \
	(((p)[0] == REGISTER_FLAG_OPEN_STR[0]) && ((p)[1] == REGISTER_FLAG_OPEN_STR[1]))

#define at_register_flag_close(p) \
	((p)[0] == REGISTER_FLAG_CLOSE_STR[0])

#define at_register_escd_pair(p) \
	(is_register_esc_char((p)[0]) && is_register_meta_char((p)[1]))

#define register_key_to_index(key) \
	((((key) >= REGISTER_KEY_MIN) && ((key) <= REGISTER_KEY_MAX)) ? (unsigned int) (key) - REGISTER_KEY_OFFSET : 0)

bool register_set(const char key, const char *value);
const char *register_get(const char key);

/* metacharacters occur twice, once as an escaped sequence */
#define REGISTER_INFO(_) \
	_("\\\\",	'\\') \
	_("\\(",	'(') \
	_("\\)",	')') \
	_("\\\"",	'"') \
	_("\\'",	'\'') \
	_("!",	'!') \
	_("\"",	'"') \
	_("#",	'#') \
	_("$",	'$') \
	_("%",	'%') \
	_("&",	'&') \
	_("'",	'\'') \
	_("(",	'(') \
	_(")",	')') \
	_("*",	'*') \
	_("+",	'+') \
	_(",",	',') \
	_("-",	'-') \
	_(".",	'.') \
	_("/",	'/') \
	_("0",	'0') \
	_("1",	'1') \
	_("2",	'2') \
	_("3",	'3') \
	_("4",	'4') \
	_("5",	'5') \
	_("6",	'6') \
	_("7",	'7') \
	_("8",	'8') \
	_("9",	'9') \
	_(":",	':') \
	_(";",	';') \
	_("<",	'<') \
	_("=",	'=') \
	_(">",	'>') \
	_("?",	'?') \
	_("@",	'@') \
	_("A",	'A') \
	_("B",	'B') \
	_("C",	'C') \
	_("D",	'D') \
	_("E",	'E') \
	_("F",	'F') \
	_("G",	'G') \
	_("H",	'H') \
	_("I",	'I') \
	_("J",	'J') \
	_("K",	'K') \
	_("L",	'L') \
	_("M",	'M') \
	_("N",	'N') \
	_("O",	'O') \
	_("P",	'P') \
	_("Q",	'Q') \
	_("R",	'R') \
	_("S",	'S') \
	_("T",	'T') \
	_("U",	'U') \
	_("V",	'V') \
	_("W",	'W') \
	_("X",	'X') \
	_("Y",	'Y') \
	_("Z",	'Z') \
	_("[",	'[') \
	_("\\",	'\\') \
	_("]",	']') \
	_("^",	'^') \
	_("_",	'_') \
	_("`",	'`') \
	_("a",	'a') \
	_("b",	'b') \
	_("c",	'c') \
	_("d",	'd') \
	_("e",	'e') \
	_("f",	'f') \
	_("g",	'g') \
	_("h",	'h') \
	_("i",	'i') \
	_("j",	'j') \
	_("k",	'k') \
	_("l",	'l') \
	_("m",	'm') \
	_("n",	'n') \
	_("o",	'o') \
	_("p",	'p') \
	_("q",	'q') \
	_("r",	'r') \
	_("s",	's') \
	_("t",	't') \
	_("u",	'u') \
	_("v",	'v') \
	_("w",	'w') \
	_("x",	'x') \
	_("y",	'y') \
	_("z",	'z') \
	_("{",	'{') \
	_("|",	'|') \
	_("}",	'}') \
	_("~",	'~')

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
