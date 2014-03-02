/* Copyright (c) 2006-2014 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/tig.h"
#include "tig/request.h"
#include "tig/util.h"

struct doc_action_iterator {
	bool end_group;
	const char *group;
};

static void
doc_action_group_name_print(const char *group)
{
	printf("%s\n", group);
	while (*group++)
		printf("^");
	printf("\n\n");
}

static void
doc_action_table_print(bool start)
{
	if (start)
		printf("[frame=\"none\",grid=\"none\",cols=\"25<m,75<\"]\n");
	printf("|=============================================================================\n");
}

static bool
doc_action_print(void *data, const struct request_info *req_info, const char *group)
{
	struct doc_action_iterator *iterator = data;

	if (iterator->group != group) {
		if (iterator->end_group) {
			doc_action_table_print(FALSE);
			printf("\n");
		}

		doc_action_group_name_print(group);
		doc_action_table_print(TRUE);

		iterator->group = group;
		iterator->end_group = TRUE;
	}

	printf("|%-24s|%s\n", enum_name(*req_info), req_info->help);
	return TRUE;
}

static void
doc_actions_print(void)
{
	struct doc_action_iterator iterator = { FALSE };

	foreach_request(doc_action_print, &iterator);
	doc_action_table_print(FALSE);
}

int
main(int argc, const char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "actions"))
			doc_actions_print();
	}

	return EXIT_SUCCESS;
}

/* vim: set ts=8 sw=8 noexpandtab: */
