/* Copyright (c) 2006-2015 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_REQUEST_H
#define TIG_REQUEST_H

#include "tig/tig.h"

/*
 * User requests
 */

#define VIEW_REQ(id, name) REQ_(VIEW_##id, "Show " #name " view")

#define REQ_INFO \
	REQ_GROUP("View switching") \
	VIEW_INFO(VIEW_REQ), \
	\
	REQ_GROUP("View manipulation") \
	REQ_(ENTER,		"Enter and open selected line"), \
	REQ_(BACK,		"Go back to the previous view state"), \
	REQ_(NEXT,		"Move to next"), \
	REQ_(PREVIOUS,		"Move to previous"), \
	REQ_(PARENT,		"Move to parent"), \
	REQ_(VIEW_NEXT,		"Move focus to the next view"), \
	REQ_(REFRESH,		"Reload and refresh view"), \
	REQ_(MAXIMIZE,		"Maximize the current view"), \
	REQ_(VIEW_CLOSE,	"Close the current view"), \
	REQ_(VIEW_CLOSE_NO_QUIT,	"Close the current view without quitting"), \
	REQ_(QUIT,		"Close all views and quit"), \
	\
	REQ_GROUP("View-specific actions") \
	REQ_(STATUS_UPDATE,	"Stage/unstage chunk or file changes"), \
	REQ_(STATUS_REVERT,	"Revert chunk or file changes"), \
	REQ_(STATUS_MERGE,	"Merge file using external tool"), \
	REQ_(STAGE_UPDATE_LINE,	"Stage/unstage single line"), \
	REQ_(STAGE_SPLIT_CHUNK,	"Split current diff chunk"), \
	\
	REQ_GROUP("Cursor navigation") \
	REQ_(MOVE_UP,		"Move cursor one line up"), \
	REQ_(MOVE_DOWN,		"Move cursor one line down"), \
	REQ_(MOVE_PAGE_DOWN,	"Move cursor one page down"), \
	REQ_(MOVE_PAGE_UP,	"Move cursor one page up"), \
	REQ_(MOVE_HALF_PAGE_DOWN,	"Move cursor half a page down"), \
	REQ_(MOVE_HALF_PAGE_UP,	"Move cursor half a page up"), \
	REQ_(MOVE_FIRST_LINE,	"Move cursor to first line"), \
	REQ_(MOVE_LAST_LINE,	"Move cursor to last line"), \
	REQ_(MOVE_NEXT_MERGE,	"Move cursor to next merge commit"), \
	REQ_(MOVE_PREV_MERGE,	"Move cursor to previous merge commit"), \
	\
	REQ_GROUP("Scrolling") \
	REQ_(SCROLL_LINE_UP,	"Scroll one line up"), \
	REQ_(SCROLL_LINE_DOWN,	"Scroll one line down"), \
	REQ_(SCROLL_PAGE_UP,	"Scroll one page up"), \
	REQ_(SCROLL_PAGE_DOWN,	"Scroll one page down"), \
	REQ_(SCROLL_FIRST_COL,	"Scroll to the first line columns"), \
	REQ_(SCROLL_LEFT,	"Scroll two columns left"), \
	REQ_(SCROLL_RIGHT,	"Scroll two columns right"), \
	\
	REQ_GROUP("Searching") \
	REQ_(SEARCH,		"Search the view"), \
	REQ_(SEARCH_BACK,	"Search backwards in the view"), \
	REQ_(FIND_NEXT,		"Find next search match"), \
	REQ_(FIND_PREV,		"Find previous search match"), \
	\
	REQ_GROUP("Misc") \
	REQ_(EDIT,		"Open in editor"), \
	REQ_(PROMPT,		"Open the prompt"), \
	REQ_(OPTIONS,		"Open the options menu"), \
	REQ_(SCREEN_REDRAW,	"Redraw the screen"), \
	REQ_(STOP_LOADING,	"Stop all loading views"), \
	REQ_(SHOW_VERSION,	"Show version information"), \
	REQ_(NONE,		"Do nothing")


/* User action requests. */
enum request {
#define REQ_GROUP(help)
#define REQ_(req, help) REQ_##req

	/* Offset all requests to avoid conflicts with ncurses getch values. */
	REQ_UNKNOWN = KEY_MAX + 1,
	REQ_OFFSET,
	REQ_INFO,

	/* Internal requests. */
	REQ_SCROLL_WHEEL_DOWN,
	REQ_SCROLL_WHEEL_UP,
	REQ_MOVE_WHEEL_DOWN,
	REQ_MOVE_WHEEL_UP,

	/* Start of the run request IDs */
	REQ_RUN_REQUESTS

#undef	REQ_GROUP
#undef	REQ_
};

struct request_info {
	enum request request;
	const char *name;
	int namelen;
	const char *help;
};

enum request get_request(const char *name);
const char *get_request_name(enum request request);
bool foreach_request(bool (*visitor)(void *data, const struct request_info *req_info, const char *group), void *data);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
