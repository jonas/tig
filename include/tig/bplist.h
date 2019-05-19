/* Copyright (c) 2019 Aurelien Aptel <aurelien.aptel@gmail.com>
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

#ifndef TIG_BPLIST_H
#define TIG_BPLIST_H

extern struct bplist global_bplist;

void bplist_init(struct bplist *bpl, size_t capacity, const char *fn);

const char * bplist_get_fn(struct bplist *bpl);
void bplist_set_fn(struct bplist *bpl, const char *fn);

bool bplist_has_rev(struct bplist *bpl, const char *rev);
void bplist_add_line(struct bplist *bpl, const char *line);
int bplist_add_rev(struct bplist *bpl, const char *rev, const char *sline);
void bplist_rem_rev(struct bplist *bpl, const char *rev);
bool bplist_toggle_rev(struct bplist *bpl, const char *rev);

int bplist_read(struct bplist *bpl, const char *fn);
int bplist_write(struct bplist *bpl, const char *fn);

void init_bplist(void);

#endif
